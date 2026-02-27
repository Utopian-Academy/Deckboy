const elements = {
  stage: document.querySelector("#output-stage"),
  video: document.querySelector("#output-video"),
  image: document.querySelector("#output-image"),
  slate: document.querySelector("#output-slate"),
  overlay: document.querySelector("#output-overlay"),
  overlayTitle: document.querySelector("#overlay-title"),
  overlayStatus: document.querySelector("#overlay-status")
};

const outputState = {
  project: null,
  activeCueId: null,
  overlayTimer: null,
  lastTimeSentAt: 0
};

function cueById(cueId) {
  return outputState.project?.cues.find((cue) => cue.id === cueId) || null;
}

function setOverlay(title, status) {
  elements.overlayTitle.textContent = title;
  elements.overlayStatus.textContent = status;
  elements.overlay.classList.add("visible");
  clearTimeout(outputState.overlayTimer);
  outputState.overlayTimer = window.setTimeout(() => {
    elements.overlay.classList.remove("visible");
  }, 1400);
}

async function postEvent(type, extra = {}) {
  await fetch("/api/output/event", {
    method: "POST",
    headers: {
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ type, cueId: outputState.activeCueId, ...extra })
  });
}

function showSlate() {
  elements.video.pause();
  elements.video.removeAttribute("src");
  elements.video.load();
  elements.video.style.display = "none";
  elements.image.style.display = "none";
  elements.slate.style.display = "grid";
  outputState.activeCueId = null;
  setOverlay("Black", "Idle");
}

async function showCue(cue, autoplay = false) {
  outputState.activeCueId = cue.id;
  elements.slate.style.display = "none";

  if (cue.kind === "image") {
    elements.video.pause();
    elements.video.removeAttribute("src");
    elements.video.load();
    elements.video.style.display = "none";
    elements.image.src = `/media/${cue.id}`;
    elements.image.style.display = "block";
    await postEvent("loaded", { duration: 0 });
    await postEvent("pause", { position: 0 });
    setOverlay(cue.name, "Still");
    return;
  }

  elements.image.style.display = "none";
  elements.video.style.display = "block";
  elements.video.poster = `/poster/${cue.id}`;
  elements.video.src = `/media/${cue.id}`;
  elements.video.load();
  await new Promise((resolve) => {
    const onLoaded = () => {
      elements.video.removeEventListener("loadedmetadata", onLoaded);
      resolve();
    };
    elements.video.addEventListener("loadedmetadata", onLoaded);
  });
  await postEvent("loaded", { duration: elements.video.duration || cue.duration || 0 });
  setOverlay(cue.name, autoplay ? "Take + play" : "Loaded");

  if (autoplay) {
    try {
      await elements.video.play();
    } catch (error) {
      console.error(error);
    }
  }
}

async function toggleFullscreen() {
  if (document.fullscreenElement) {
    await document.exitFullscreen();
    return;
  }

  await elements.stage.requestFullscreen();
}

async function restoreFromState() {
  const cue = cueById(outputState.project?.activeCueId);
  if (!cue) {
    showSlate();
    return;
  }

  await showCue(cue, outputState.project.transport.status === "playing");
  elements.video.volume = outputState.project.transport.volume ?? 1;

  if (cue.kind === "video" && Number.isFinite(outputState.project.transport.position)) {
    try {
      elements.video.currentTime = outputState.project.transport.position;
    } catch (error) {
      console.error(error);
    }
  }

  if (cue.kind === "video" && outputState.project.transport.status === "paused") {
    elements.video.pause();
  }
}

async function handleCommand(command) {
  if (!outputState.project) {
    return;
  }

  const cue = cueById(command.cueId || outputState.project.activeCueId);

  if (command.name === "take") {
    if (cue) {
      await showCue(cue, Boolean(command.autoplay));
    }
    return;
  }

  if (command.name === "play") {
    if (elements.video.style.display === "block") {
      await elements.video.play().catch((error) => console.error(error));
      setOverlay(cue?.name || "Cue", "Playing");
    }
    return;
  }

  if (command.name === "pause") {
    if (elements.video.style.display === "block") {
      elements.video.pause();
      setOverlay(cue?.name || "Cue", "Paused");
    }
    return;
  }

  if (command.name === "toggle") {
    if (elements.video.style.display === "none") {
      return;
    }

    if (elements.video.paused) {
      await elements.video.play().catch((error) => console.error(error));
      setOverlay(cue?.name || "Cue", "Playing");
    } else {
      elements.video.pause();
      setOverlay(cue?.name || "Cue", "Paused");
    }
    return;
  }

  if (command.name === "stop") {
    if (elements.video.style.display === "block") {
      elements.video.pause();
      elements.video.currentTime = 0;
    }
    await postEvent("stop");
    setOverlay(cue?.name || "Cue", "Stopped");
    return;
  }

  if (command.name === "seek") {
    if (elements.video.style.display === "block" && Number.isFinite(command.position)) {
      elements.video.currentTime = command.position;
    }
    return;
  }

  if (command.name === "set-volume") {
    elements.video.volume = Number.isFinite(command.volume) ? command.volume : 1;
    await postEvent("volumechange", { volume: elements.video.volume });
    return;
  }

  if (command.name === "fullscreen") {
    await toggleFullscreen().catch((error) => console.error(error));
    return;
  }

  if (command.name === "clear") {
    showSlate();
    await postEvent("cleared");
  }
}

async function init() {
  const response = await fetch("/api/state");
  outputState.project = await response.json();
  await restoreFromState();
  await postEvent("ready");

  const events = new EventSource("/events");
  events.addEventListener("state", (message) => {
    outputState.project = JSON.parse(message.data);
  });
  events.addEventListener("command", (message) => {
    handleCommand(JSON.parse(message.data)).catch((error) => console.error(error));
  });

  elements.video.addEventListener("play", () => {
    postEvent("play").catch((error) => console.error(error));
  });

  elements.video.addEventListener("pause", () => {
    postEvent("pause", { position: elements.video.currentTime }).catch((error) => console.error(error));
  });

  elements.video.addEventListener("ended", () => {
    postEvent("ended", { position: elements.video.duration || 0 }).catch((error) => console.error(error));
    setOverlay(cueById(outputState.activeCueId)?.name || "Cue", "Ended");
  });

  elements.video.addEventListener("timeupdate", () => {
    const now = Date.now();
    if (now - outputState.lastTimeSentAt < 250) {
      return;
    }
    outputState.lastTimeSentAt = now;
    postEvent("timeupdate", {
      position: elements.video.currentTime,
      duration: elements.video.duration
    }).catch((error) => console.error(error));
  });

  document.addEventListener("fullscreenchange", () => {
    postEvent("fullscreenchange", { fullscreen: Boolean(document.fullscreenElement) }).catch((error) =>
      console.error(error)
    );
  });

  window.addEventListener("keydown", (event) => {
    if (event.key.toLowerCase() === "f") {
      event.preventDefault();
      toggleFullscreen().catch((error) => console.error(error));
    }
  });
}

init().catch((error) => {
  console.error(error);
  showSlate();
});
