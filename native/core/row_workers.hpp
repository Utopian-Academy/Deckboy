// ═══════════════════════════════════════════════════════════════════════════
// row_workers.hpp — a band of worker threads that outlives the frame.
//
// Splitting a raster across cores is the right idea; creating the threads to
// do it is not free, and doing it EVERY FRAME is what turned a video synth
// cue into 38 GB of committed memory and a machine with 100 MB left.
//
// The arithmetic is unkind and scales the wrong way. Thread count follows core
// count, so on a 32-core machine the synth built 31 threads per rebuild, and
// it rebuilt on every render-loop tick rather than at display rate -- about
// 7,400 thread creations a second. Every Windows thread commits its stack, and
// the commit is not returned the instant the thread is joined, so private
// bytes ran away far faster than the OS could reclaim them. On a 4-core
// machine the same code makes 3 threads a frame and looks perfectly healthy,
// which is exactly why it was never noticed here.
//
// So: create them once, park them on a condition variable, and hand them a
// range when there is one. The pool is owned by whatever does the work, lives
// as long as it does, and costs nothing between frames.
//
// This is deliberately NOT a general task queue. It does one thing -- run
// fn(begin, end) over a contiguous range, split into bands, and wait -- which
// is the shape of every parallel loop in the renderer, and small enough to be
// read in one sitting and be sure of.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace deckboy {

class RowWorkers {
public:
  RowWorkers() = default;
  RowWorkers(const RowWorkers&) = delete;
  RowWorkers& operator=(const RowWorkers&) = delete;
  ~RowWorkers() { shutdown(); }

  // Run fn over [0, total) split into bands, and return when every band is
  // done. Runs one band on the CALLING thread, so a single-band job costs
  // nothing at all and the caller is never idle while workers run.
  //
  // minPerBand is the point below which threading is a loss: handing a
  // hundred rows to thirty threads costs more in wakeups than doing it
  // straight through. Callers pass what their own work is worth.
  void run(int total, int minPerBand, const std::function<void(int, int)>& fn) {
    if (total <= 0) {
      return;
    }
    int bands = total / std::max(1, minPerBand);
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) {
      hw = 1;
    }
    bands = std::clamp(bands, 1, static_cast<int>(hw));
    if (bands <= 1) {
      fn(0, total);
      return;
    }
    ensure(bands - 1);

    const int span = (total + bands - 1) / bands;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      work_ = &fn;
      span_ = span;
      total_ = total;
      // Band 0 belongs to the caller; the workers take 1..bands-1.
      nextBand_ = 1;
      lastBand_ = bands;
      outstanding_ = bands - 1;
      ++generation_;
    }
    wake_.notify_all();

    fn(0, std::min(total, span));

    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return outstanding_ == 0; });
    work_ = nullptr;
  }

  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (quit_) {
        return;
      }
      quit_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
    threads_.clear();
  }

private:
  // Grow to at least `want` workers. Never shrinks: a pool that gave threads
  // back between frames would be the per-frame creation this exists to avoid.
  void ensure(int want) {
    if (static_cast<int>(threads_.size()) >= want) {
      return;
    }
    while (static_cast<int>(threads_.size()) < want) {
      threads_.emplace_back([this] { loop(); });
    }
  }

  void loop() {
    std::uint64_t seen = 0;
    for (;;) {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this, &seen] { return quit_ || generation_ != seen; });
      if (quit_) {
        return;
      }
      seen = generation_;
      // Claim bands until this batch is exhausted. A worker that finds none
      // left simply goes back to sleep without touching the counter.
      for (;;) {
        if (nextBand_ >= lastBand_) {
          break;
        }
        const int band = nextBand_++;
        const int begin = std::min(total_, band * span_);
        const int end = std::min(total_, begin + span_);
        const std::function<void(int, int)>* fn = work_;
        lock.unlock();
        if (fn && begin < end) {
          (*fn)(begin, end);
        }
        lock.lock();
        if (--outstanding_ == 0) {
          lock.unlock();
          done_.notify_one();
          lock.lock();
        }
      }
    }
  }

  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable done_;
  const std::function<void(int, int)>* work_ = nullptr;
  std::uint64_t generation_ = 0;
  int span_ = 0;
  int total_ = 0;
  int nextBand_ = 0;
  int lastBand_ = 0;
  int outstanding_ = 0;
  bool quit_ = false;
};

}  // namespace deckboy
