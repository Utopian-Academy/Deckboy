/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Web Monitor uses a single muxed HLS timeline for picture and sound. */
(function () {
  'use strict';
  var root = document.getElementById('monitor');
  var video = document.getElementById('v');
  var sound = document.getElementById('sound');
  var fullscreen = document.getElementById('fullscreen');
  var reconnect = document.getElementById('reconnect');
  var status = document.getElementById('status');
  var source = root.getAttribute('data-source');
  var hls = null;
  var retry = null;
  var stopped = false;
  var mediaRecovered = false;
  var lastProgress = Date.now();
  var lastTime = -1;
  var hiddenAt = 0;

  function message(text) { status.textContent = text; status.hidden = !text; }
  function play() {
    var result = video.play();
    if (result && result.catch) result.catch(function () { message('Press SOUND ON to play.'); });
  }
  function scheduleReconnect() {
    if (stopped || retry) return;
    message('Reconnecting to programme…');
    retry = setTimeout(function () { retry = null; connect(); }, 2000);
  }
  function connect() {
    if (stopped) return;
    clearTimeout(retry); retry = null;
    if (hls) { hls.destroy(); hls = null; }
    video.removeAttribute('src');
    video.load();
    message('Connecting to programme…');
    lastProgress = Date.now();
    // The bundled player first, wherever Media Source exists (iOS 17+ through
    // ManagedMediaSource): it holds the live edge and recovers by itself.
    // Android Chrome also answers "maybe" to native HLS, and its native
    // player does neither, so native is only the fallback for browsers and
    // TVs without Media Source.
    if (window.Hls && window.Hls.isSupported()) {
      hls = new window.Hls({
        enableWorker: false,
        lowLatencyMode: false,
        liveSyncDuration: 3,
        liveMaxLatencyDuration: 10,
        // The wall-clock steering below sets the rate; two controllers
        // pulling on one playbackRate would fight.
        maxLiveSyncPlaybackRate: 1,
        maxBufferLength: 8,
        maxMaxBufferLength: 12,
        backBufferLength: 10,
        startFragPrefetch: true
      });
      hls.on(window.Hls.Events.MANIFEST_PARSED, play);
      hls.on(window.Hls.Events.ERROR, function (event, data) {
        if (!data.fatal) return;
        // A decoder hiccup is recoverable in place, once; anything else, or a
        // second one, starts the connection again.
        if (data.type === window.Hls.ErrorTypes.MEDIA_ERROR && !mediaRecovered) {
          mediaRecovered = true;
          hls.recoverMediaError();
        } else {
          scheduleReconnect();
        }
      });
      mediaRecovered = false;
      hls.loadSource(source);
      hls.attachMedia(video);
    } else if (video.canPlayType('application/vnd.apple.mpegurl')) {
      video.src = source;
      play();
    } else {
      message('This browser cannot play the live feed. Open the stream link in an HLS player.');
    }
  }
  sound.onclick = function () {
    video.muted = !video.muted;
    sound.textContent = video.muted ? 'SOUND ON' : 'MUTE';
    sound.setAttribute('aria-pressed', String(!video.muted));
    play();
  };
  fullscreen.onclick = function () {
    var operation;
    if (document.fullscreenElement) operation = document.exitFullscreen();
    else if (root.requestFullscreen) operation = root.requestFullscreen();
    else if (video.webkitEnterFullscreen) video.webkitEnterFullscreen();
    else { message('Full screen is controlled by this browser.'); return; }
    if (operation && operation.catch) operation.catch(function () { message('Full screen is unavailable.'); });
  };
  document.addEventListener('fullscreenchange', function () {
    fullscreen.textContent = document.fullscreenElement ? 'EXIT FULL SCREEN' : 'FULL SCREEN';
  });
  reconnect.onclick = connect;
  video.addEventListener('playing', function () { message(''); });
  video.addEventListener('waiting', function () { message('Buffering programme…'); });
  video.addEventListener('error', scheduleReconnect);
  video.addEventListener('ended', scheduleReconnect);
  // TV remotes can move between the same controls used by touch and keyboard.
  var controls = [sound, fullscreen, reconnect, document.getElementById('stream-link')];
  document.addEventListener('keydown', function (event) {
    var direction = event.keyCode === 39 || event.keyCode === 40 ? 1
                  : event.keyCode === 37 || event.keyCode === 38 ? -1 : 0;
    if (!direction) return;
    var index = controls.indexOf(document.activeElement);
    controls[(index + direction + controls.length) % controls.length].focus();
    event.preventDefault();
  });
  // IN STEP WITH EVERY OTHER SCREEN. Each fragment carries the wall-clock
  // time it was made (PROGRAM-DATE-TIME), and every player aims at the same
  // moment: now, minus a fixed delay. Phones and laptops keep network time,
  // so screens that each hold that target show the same frame. Small errors
  // are steered out by nudging the speed (at most 3%, which nobody hears);
  // only a large one jumps.
  var kDelayMs = 4000;
  function playingDateMs() {
    if (hls && hls.playingDate) return hls.playingDate.getTime();
    if (video.getStartDate) {
      var start = video.getStartDate();
      if (start && !isNaN(start.getTime())) return start.getTime() + video.currentTime * 1000;
    }
    return null;
  }
  setInterval(function () {
    if (stopped || video.paused || video.readyState < 3) return;
    var showing = playingDateMs();
    if (showing === null) return;
    var errorMs = (Date.now() - kDelayMs) - showing;   // > 0: behind the target
    window.deckboyMonitorSyncErrorMs = errorMs;         // for the test, and for curious eyes
    if (Math.abs(errorMs) > 1000) {
      video.currentTime += errorMs / 1000;
      video.playbackRate = 1;
    } else {
      video.playbackRate = 1 + Math.max(-0.03, Math.min(0.03, errorMs / 1000 * 0.5));
    }
  }, 500);

  // A STALL THAT RAISES NO ERROR. Phones and busy Wi-Fi can leave a player
  // "buffering" forever without failing; if the picture has not moved for
  // eight seconds while it should be playing, start again at the live edge.
  setInterval(function () {
    if (stopped || retry || video.paused) { lastProgress = Date.now(); return; }
    if (video.currentTime !== lastTime) {
      lastTime = video.currentTime;
      lastProgress = Date.now();
    } else if (Date.now() - lastProgress > 8000) {
      scheduleReconnect();
    }
  }, 1000);
  // A tab put to sleep comes back holding old pictures and old sound. Rejoin
  // live rather than play out a stale buffer behind the show.
  document.addEventListener('visibilitychange', function () {
    if (document.hidden) { hiddenAt = Date.now(); return; }
    if (hiddenAt && Date.now() - hiddenAt > 5000) connect();
    hiddenAt = 0;
  });
  window.addEventListener('pagehide', function () {
    stopped = true;
    clearTimeout(retry);
    if (hls) hls.destroy();
  });
  connect();
}());
