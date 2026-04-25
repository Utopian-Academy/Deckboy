/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Deckboy — Cross-platform subprocess implementation
 * Copyright 2025 James
 */

// ============================================================================
// subprocess.cpp — Implementation of cross-platform subprocess spawning.
//
// Windows: Uses CreateProcessW with explicit stdio handle redirection.
//   - Pipes are created with CreatePipe; read end is converted to a CRT fd
//     via _open_osfhandle so the rest of the codebase uses _read()/_close().
//   - ffmpeg/ffprobe are resolved via a custom search path that checks
//     DECKBOY_FFMPEG env, project tools/ dir, exe dir, and C:/ffmpeg/bin/.
//   - Command lines are built with proper quoting (backslash/quote escaping
//     per the CommandLineToArgvW convention).
//
// POSIX: Uses fork() + execvp() with pipe() for stdout redirection.
//   - Detached processes get their own session via setsid().
//   - stdin/stdout/stderr are redirected to /dev/null as needed.
//
// Both platforms: ChildProcess::stop() uses SIGKILL / TerminateProcess
// for immediate termination (no graceful shutdown — ffmpeg handles this fine).
// ============================================================================

#include "subprocess.hpp"
#include "paths.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <fcntl.h>      // _O_RDONLY, _O_BINARY
#include <io.h>          // _open_osfhandle, _read, _close
#else
#include <fcntl.h>       // O_RDONLY, O_WRONLY
#include <signal.h>      // kill, SIGKILL
#include <sys/types.h>   // pid_t
#include <sys/wait.h>    // waitpid
#include <unistd.h>      // fork, execvp, pipe, dup2, setsid, close
#endif

namespace fs = std::filesystem;

// ===========================================================================
// ChildProcess lifetime
// ===========================================================================

// Check if the child process handle is still valid (not yet stopped/cleaned up).
// Does NOT check if the process is actually still running — just that we
// haven't called stop() yet. For MediaEngine's purposes this is sufficient.
bool ChildProcess::running() const {
#ifdef _WIN32
  return hProcess != INVALID_HANDLE_VALUE;
#else
  return pid > 0;
#endif
}

// Kill the child process, close the pipe fd, and reset all handles to defaults.
// Order matters: close the pipe FIRST so any thread blocked on read() unblocks
// (gets EBADF or EOF), THEN kill the process and wait for it to exit.
// The 5000ms WaitForSingleObject timeout on Windows prevents deadlocks if
// TerminateProcess takes unusually long (shouldn't happen, but defensive).
void ChildProcess::stop() {
#ifdef _WIN32
  // Close pipe fds first — unblocks any thread in _read()/_write()
  if (writeFd >= 0) {
    _close(writeFd);
    writeFd = -1;
  }
  if (readFd >= 0) {
    _close(readFd);
    readFd = -1;
  }
  if (hProcess != INVALID_HANDLE_VALUE) {
    TerminateProcess(hProcess, 1);          // force-kill the child
    WaitForSingleObject(hProcess, 5000);    // wait up to 5s for exit
    CloseHandle(hProcess);                  // release the process handle
    hProcess = INVALID_HANDLE_VALUE;
  }
#else
  // Close pipe fds first — unblocks any thread in read()/write()
  if (writeFd >= 0) {
    close(writeFd);
    writeFd = -1;
  }
  if (readFd >= 0) {
    close(readFd);
    readFd = -1;
  }
  if (pid > 0) {
    pid_t target = pid;
    // SIGKILL is uncatchable — guarantees the child exits immediately.
    // For detached processes (processGroup=true), kill the entire group
    // using the negative PID convention.
    if (processGroup) {
      kill(-target, SIGKILL);   // kill entire process group
    } else {
      kill(target, SIGKILL);    // kill just the child
    }
    int status = 0;
    waitpid(target, &status, 0);  // reap zombie — prevents resource leak
    pid = -1;
    processGroup = false;
  }
#endif
}

void ChildProcess::killProcessOnly() {
#ifdef _WIN32
  if (hProcess != INVALID_HANDLE_VALUE) {
    TerminateProcess(hProcess, 1);
    // Do NOT close hProcess or readFd here — stop() will do that after the
    // reader thread exits.
  }
#else
  if (pid > 0) {
    if (processGroup) {
      kill(-pid, SIGKILL);
    } else {
      kill(pid, SIGKILL);
    }
  }
#endif
}

ChildProcess::~ChildProcess() {
  stop();
}

// ===========================================================================
// spawnProcess — unified entry point
// ===========================================================================

#ifdef _WIN32

// Convert a UTF-8 string to a wide (UTF-16) string for Windows API calls.
// Returns empty string for empty/null input.
static std::wstring utf8ToWide(const std::string& utf8) {
  int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (wlen <= 1) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(wlen), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), wlen);
  wide.pop_back();  // remove null terminator (std::wstring manages its own)
  return wide;
}

// ASCII-only lowercase for case-insensitive filename comparisons.
static std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

// Resolve a path to its absolute, canonical form. Tries weakly_canonical first
// (resolves symlinks where possible), falls back to absolute (just prepends cwd).
static fs::path normalizeAbsolutePath(const fs::path& path) {
  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    return canonical;
  }
  fs::path absolute = fs::absolute(path, ec);
  if (!ec && !absolute.empty()) {
    return absolute;
  }
  return path;  // return as-is if both attempts fail
}

// ---------------------------------------------------------------------------
// resolvePinnedMediaTool — Find ffmpeg.exe or ffprobe.exe on Windows.
//
// Unlike POSIX (where execvp searches PATH), Windows CreateProcessW needs
// an explicit application path for reliable behavior. This function searches
// a prioritized list of locations:
//   1. DECKBOY_FFMPEG / DECKBOY_FFPROBE env var (exact path)
//   2. DECKBOY_FFMPEG_DIR env var / <tool>.exe
//   3. Project root: tools/ffmpeg/bin/, tools/, bin/
//   4. Exe directory: alongside deckboy.exe, or in ffmpeg/bin/ subdirs
//   5. Hardcoded fallback: C:/ffmpeg/bin/ (common Windows install location)
//
// Returns nullopt if the command is not ffmpeg/ffprobe (let it use PATH).
// ---------------------------------------------------------------------------
static std::optional<fs::path> resolvePinnedMediaTool(const std::string& rawCommand) {
  fs::path commandPath(rawCommand);
  std::string toolStem = lowerAscii(commandPath.stem().string());
  if (toolStem != "ffmpeg" && toolStem != "ffprobe") {
    return std::nullopt;  // not a media tool — caller handles normally
  }

  const fs::path exeName = toolStem + ".exe";
  std::vector<fs::path> candidates;

  // Priority 1: explicit env var for the exact tool path
  const char* exactEnv = toolStem == "ffmpeg" ? std::getenv("DECKBOY_FFMPEG")
                                              : std::getenv("DECKBOY_FFPROBE");
  if (exactEnv && *exactEnv) {
    candidates.emplace_back(exactEnv);
  }
  // Priority 2: DECKBOY_FFMPEG_DIR env var (directory containing both tools)
  if (const char* toolDirEnv = std::getenv("DECKBOY_FFMPEG_DIR"); toolDirEnv && *toolDirEnv) {
    candidates.emplace_back(fs::path(toolDirEnv) / exeName);
  }

  // Priority 3–7: well-known directories relative to project root and exe dir
  fs::path projectRoot = deckboy::core::Paths::projectRoot();
  fs::path exeDir = deckboy::core::Paths::executablePath().parent_path();
  std::vector<fs::path> searchRoots {
    projectRoot / "tools" / "ffmpeg" / "bin",   // bundled in project
    projectRoot / "tools",
    projectRoot / "bin",
    exeDir,                                      // alongside the exe
    exeDir / "ffmpeg" / "bin",
    exeDir / "tools" / "ffmpeg" / "bin",
    fs::path("C:/ffmpeg/bin"),                   // common system install
  };
  for (const auto& dir : searchRoots) {
    if (!dir.empty()) {
      candidates.push_back(dir / exeName);
    }
  }

  // Return the first candidate that actually exists as a regular file
  for (const auto& candidate : candidates) {
    fs::path normalized = normalizeAbsolutePath(candidate);
    std::error_code ec;
    if (!normalized.empty() && fs::is_regular_file(normalized, ec) && !ec) {
      return normalized;
    }
  }
  return std::nullopt;  // not found in any search location
}

// Resolve the application path for a Windows subprocess launch.
// If the command is an absolute path or has a directory component, normalize it.
// Otherwise, check if it's ffmpeg/ffprobe and use the pinned media tool search.
static std::optional<fs::path> resolveWindowsApplicationPath(const std::string& rawCommand) {
  fs::path commandPath(rawCommand);
  if (commandPath.is_absolute() || commandPath.has_parent_path()) {
    return normalizeAbsolutePath(commandPath);  // already has a path — just normalize
  }
  return resolvePinnedMediaTool(rawCommand);    // bare name — try media tool search
}

// ---------------------------------------------------------------------------
// buildCommandLine — Build a properly quoted command-line string for CreateProcessW.
//
// Each argument is wrapped in double-quotes. Internal backslashes and
// double-quotes are escaped per the Windows CommandLineToArgvW conventions:
//   - Backslashes before a quote are doubled (then the quote is escaped)
//   - Trailing backslashes before the closing quote are doubled
//   - All other characters pass through unchanged
// ---------------------------------------------------------------------------
static std::wstring buildCommandLine(const std::vector<std::string>& args) {
  std::wstring cmdline;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0) cmdline += L' ';
    std::wstring warg = utf8ToWide(args[i]);

    // Quote the argument
    cmdline += L'"';
    int backslashes = 0;
    for (wchar_t ch : warg) {
      if (ch == L'\\') {
        ++backslashes;
      } else if (ch == L'"') {
        // Escape all preceding backslashes + the quote
        for (int j = 0; j < backslashes; ++j) cmdline += L'\\';
        cmdline += L'\\';
        backslashes = 0;
      } else {
        backslashes = 0;
      }
      cmdline += ch;
    }
    // Escape trailing backslashes before the closing quote
    for (int j = 0; j < backslashes; ++j) cmdline += L'\\';
    cmdline += L'"';
  }
  return cmdline;
}

bool spawnProcess(ChildProcess& process,
                  const std::vector<std::string>& args,
                  const SpawnOptions& options) {
  process.stop();
  if (args.empty()) return false;

  std::vector<std::string> launchArgs = args;
  std::wstring applicationPathW;
  if (auto resolved = resolveWindowsApplicationPath(args.front())) {
    if (!resolved->empty()) {
      applicationPathW = resolved->wstring();
    }
  } else {
    std::string toolStem = lowerAscii(fs::path(args.front()).stem().string());
    if (toolStem == "ffmpeg" || toolStem == "ffprobe") {
      return false;
    }
  }

  // --- Set up pipe for stdout if requested ---
  HANDLE hReadPipe  = INVALID_HANDLE_VALUE;
  HANDLE hWritePipe = INVALID_HANDLE_VALUE;
  if (options.stdoutMode == StdioMode::Pipe) {
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;  // write end is inherited by child
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
      return false;
    }
    // Make the read end non-inheritable
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
  }

  // --- Set up pipe for stdin if requested ---
  HANDLE hStdinReadPipe  = INVALID_HANDLE_VALUE;
  HANDLE hStdinWritePipe = INVALID_HANDLE_VALUE;
  if (options.stdinMode == StdioMode::Pipe) {
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;  // read end is inherited by child
    if (!CreatePipe(&hStdinReadPipe, &hStdinWritePipe, &sa, 0)) {
      if (hReadPipe != INVALID_HANDLE_VALUE) CloseHandle(hReadPipe);
      if (hWritePipe != INVALID_HANDLE_VALUE) CloseHandle(hWritePipe);
      return false;
    }
    // Make the write end non-inheritable
    SetHandleInformation(hStdinWritePipe, HANDLE_FLAG_INHERIT, 0);
  }

  // --- NUL handle for redirecting to nowhere ---
  HANDLE hNul = INVALID_HANDLE_VALUE;
  bool needNul = (options.stdinMode  == StdioMode::Null ||
                  options.stdoutMode == StdioMode::Null ||
                  options.stderrMode == StdioMode::Null);
  if (needNul) {
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    hNul = CreateFileW(L"NUL",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       &sa,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       nullptr);
  }

  // --- STARTUPINFO ---
  STARTUPINFOW si {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;

  // stdin
  if (options.stdinMode == StdioMode::Pipe) {
    si.hStdInput = hStdinReadPipe;
  } else if (options.stdinMode == StdioMode::Null && hNul != INVALID_HANDLE_VALUE) {
    si.hStdInput = hNul;
  } else {
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }

  // stdout
  if (options.stdoutMode == StdioMode::Pipe) {
    si.hStdOutput = hWritePipe;
  } else if (options.stdoutMode == StdioMode::Null && hNul != INVALID_HANDLE_VALUE) {
    si.hStdOutput = hNul;
  } else {
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  }

  // stderr
  if (options.stderrMode == StdioMode::Merge) {
    si.hStdError = si.hStdOutput;  // merge into stdout
  } else if (options.stderrMode == StdioMode::Null && hNul != INVALID_HANDLE_VALUE) {
    si.hStdError = hNul;
  } else {
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  }

  // --- Build command line ---
  std::wstring cmdline = buildCommandLine(launchArgs);

  // --- Launch ---
  DWORD creationFlags = CREATE_NO_WINDOW;
  PROCESS_INFORMATION pi {};
  BOOL ok = CreateProcessW(
    applicationPathW.empty() ? nullptr : applicationPathW.c_str(),
    cmdline.data(),
    nullptr,   // process security
    nullptr,   // thread security
    TRUE,      // inherit handles (needed for pipe)
    creationFlags,
    nullptr,   // inherit environment
    nullptr,   // inherit cwd
    &si,
    &pi
  );

  // Close handles we no longer need regardless of success/failure
  if (hWritePipe != INVALID_HANDLE_VALUE) CloseHandle(hWritePipe);
  if (hStdinReadPipe != INVALID_HANDLE_VALUE) CloseHandle(hStdinReadPipe);
  if (hNul != INVALID_HANDLE_VALUE)       CloseHandle(hNul);

  if (!ok) {
    if (hReadPipe != INVALID_HANDLE_VALUE) CloseHandle(hReadPipe);
    if (hStdinWritePipe != INVALID_HANDLE_VALUE) CloseHandle(hStdinWritePipe);
    return false;
  }

  // We don't need the thread handle
  CloseHandle(pi.hThread);

  // Convert the read HANDLE to a CRT file descriptor so the rest of the
  // codebase can use _read() / _close() on it uniformly.
  int crtFd = -1;
  if (hReadPipe != INVALID_HANDLE_VALUE) {
    crtFd = _open_osfhandle(reinterpret_cast<intptr_t>(hReadPipe), _O_RDONLY | _O_BINARY);
    if (crtFd < 0) {
      CloseHandle(hReadPipe);
      TerminateProcess(pi.hProcess, 1);
      CloseHandle(pi.hProcess);
      return false;
    }
    // hReadPipe is now owned by the CRT fd; don't CloseHandle it separately
  }

  // Convert the stdin write HANDLE to a CRT fd for _write()
  int crtWriteFd = -1;
  if (hStdinWritePipe != INVALID_HANDLE_VALUE) {
    crtWriteFd = _open_osfhandle(reinterpret_cast<intptr_t>(hStdinWritePipe), _O_BINARY);
    if (crtWriteFd < 0) {
      CloseHandle(hStdinWritePipe);
      // Non-fatal: process still runs, just can't write to stdin
    }
  }

  process.hProcess = pi.hProcess;
  process.readFd   = crtFd;
  process.writeFd  = crtWriteFd;
  return true;
}

#else // !_WIN32

bool spawnProcess(ChildProcess& process,
                  const std::vector<std::string>& args,
                  const SpawnOptions& options) {
  process.stop();
  if (args.empty()) return false;

  // Create pipe if stdout is piped
  int pipeFd[2] = {-1, -1};
  if (options.stdoutMode == StdioMode::Pipe) {
    if (pipe(pipeFd) != 0) return false;
  }

  // Create pipe if stdin is piped (parent writes, child reads)
  int stdinPipeFd[2] = {-1, -1};
  if (options.stdinMode == StdioMode::Pipe) {
    if (pipe(stdinPipeFd) != 0) {
      if (pipeFd[0] >= 0) { close(pipeFd[0]); close(pipeFd[1]); }
      return false;
    }
  }

  pid_t childPid = fork();
  if (childPid < 0) {
    if (pipeFd[0] >= 0) { close(pipeFd[0]); close(pipeFd[1]); }
    if (stdinPipeFd[0] >= 0) { close(stdinPipeFd[0]); close(stdinPipeFd[1]); }
    return false;
  }

  if (childPid == 0) {
    // --- child ---

    if (options.detached) {
      setsid();
    }

    // stdin
    if (options.stdinMode == StdioMode::Pipe) {
      dup2(stdinPipeFd[0], STDIN_FILENO);
      close(stdinPipeFd[0]);
      close(stdinPipeFd[1]);
    } else if (options.stdinMode == StdioMode::Null) {
      int fd = open("/dev/null", O_RDONLY);
      if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); }
    }

    // stdout
    if (options.stdoutMode == StdioMode::Pipe) {
      dup2(pipeFd[1], STDOUT_FILENO);
      close(pipeFd[0]);
      close(pipeFd[1]);
    } else if (options.stdoutMode == StdioMode::Null) {
      int fd = open("/dev/null", O_WRONLY);
      if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
    }
    // StdioMode::Inherit → leave as-is

    // stderr
    if (options.stderrMode == StdioMode::Null) {
      int fd = open("/dev/null", O_WRONLY);
      if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); }
    } else if (options.stderrMode == StdioMode::Merge) {
      dup2(STDOUT_FILENO, STDERR_FILENO);
    }
    // StdioMode::Inherit → leave as-is

    // exec
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  // --- parent ---
  if (pipeFd[1] >= 0) close(pipeFd[1]);       // close stdout write end
  if (stdinPipeFd[0] >= 0) close(stdinPipeFd[0]); // close stdin read end

  process.pid = childPid;
  process.readFd = pipeFd[0];         // -1 if no stdout pipe
  process.writeFd = stdinPipeFd[1];   // -1 if no stdin pipe
  process.processGroup = options.detached;
  return true;
}

#endif // _WIN32

// ===========================================================================
// readAllText — run, capture, wait
// ===========================================================================

std::optional<std::string> readAllText(const std::vector<std::string>& args) {
  if (args.empty()) return std::nullopt;

  ChildProcess proc;
  if (!spawnProcess(proc, args, SpawnOptions::captureAll())) {
    return std::nullopt;
  }

  std::string output;
  std::array<char, 4096> buffer{};

#ifdef _WIN32
  int bytesRead = 0;
  while ((bytesRead = _read(proc.readFd, buffer.data(), static_cast<unsigned int>(buffer.size()))) > 0) {
    output.append(buffer.data(), static_cast<size_t>(bytesRead));
  }
  _close(proc.readFd);
  proc.readFd = -1;

  WaitForSingleObject(proc.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(proc.hProcess, &exitCode);
  CloseHandle(proc.hProcess);
  proc.hProcess = INVALID_HANDLE_VALUE;

  if (exitCode != 0) {
    return std::nullopt;
  }
#else
  ssize_t bytesRead = 0;
  while ((bytesRead = read(proc.readFd, buffer.data(), buffer.size())) > 0) {
    output.append(buffer.data(), static_cast<size_t>(bytesRead));
  }
  close(proc.readFd);
  proc.readFd = -1;

  int status = 0;
  waitpid(proc.pid, &status, 0);
  pid_t savedPid = proc.pid;
  proc.pid = -1; // prevent double-kill in destructor

  (void) savedPid;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return std::nullopt;
  }
#endif
  return output;
}

// ===========================================================================
// Legacy convenience wrappers
// ===========================================================================

bool spawnPipeProcess(ChildProcess& process,
                      const std::vector<std::string>& args) {
  return spawnProcess(process, args, SpawnOptions::pipedStdout());
}

bool spawnDetachedProcess(ChildProcess& process,
                          const std::vector<std::string>& args) {
  return spawnProcess(process, args, SpawnOptions::detachedSilent());
}
