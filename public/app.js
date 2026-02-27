const state = {
  project: null,
  draggedCueId: null,
  draggingOverCueId: null
};

const elements = {
  projectTitle: document.querySelector("#project-title"),
  saveProject: document.querySelector("#save-project"),
  importPick: document.querySelector("#import-pick"),
  importPaths: document.querySelector("#import-paths"),
  openOutput: document.querySelector("#open-output"),
  toggleFullscreen: document.querySelector("#toggle-fullscreen"),
  cueCount: document.querySelector("#cue-count"),
  cueList: document.querySelector("#cue-list"),
  nowPlayingTitle: document.querySelector("#now-playing-title"),
  nowPlayingMeta: document.querySelector("#now-playing-meta"),
  takeButton: document.querySelector("#take-button"),
  goButton: document.querySelector("#go-button"),
  stopButton: document.querySelector("#stop-button"),
  clearButton: document.querySelector("#clear-button"),
  transportStatus: document.querySelector("#transport-status"),
  timeReadout: document.querySelector("#time-readout"),
  scrub: document.querySelector("#scrub"),
  volume: document.querySelector("#volume"),
  volumeReadout: document.querySelector("#volume-readout"),
  selectedName: document.querySelector("#selected-name"),
  selectedPoster: document.querySelector("#selected-poster"),
  selectedFacts: document.querySelector("#selected-facts"),
  cueName: document.querySelector("#cue-name"),
  cueColor: document.querySelector("#cue-color"),
  cueNotes: document.querySelector("#cue-notes"),
  saveCue: document.querySelector("#save-cue"),
  deleteCue: document.querySelector("#delete-cue"),
  cueTemplate: document.querySelector("#cue-item-template")
};

function selectedCue() {
  return state.project?.cues.find((cue) => cue.id === state.project.selectedCueId) || null;
}

function activeCue() {
  return state.project?.cues.find((cue) => cue.id === state.project.activeCueId) || null;
}

function formatDuration(seconds) {
  if (!Number.isFinite(seconds) || seconds <= 0) {
    return "00:00.0";
  }

  const mins = Math.floor(seconds / 60);
  const secs = seconds - mins * 60;
  return `${String(mins).padStart(2, "0")}:${secs.toFixed(1).padStart(4, "0")}`;
}

function cueMeta(cue) {
  const parts = [];
  if (cue.kind === "image") {
    parts.push("Still");
  } else if (Number.isFinite(cue.duration)) {
    parts.push(formatDuration(cue.duration));
  }

  if (cue.width && cue.height) {
    parts.push(`${cue.width}x${cue.height}`);
  }

  if (cue.videoCodec) {
    parts.push(cue.videoCodec);
  }

  if (cue.hasAudio && cue.audioCodec) {
    parts.push(`+ ${cue.audioCodec}`);
  }

  return parts.join(" • ");
}

function posterUrl(cue) {
  if (!cue) {
    return "";
  }
  return cue.kind === "image" ? `/media/${cue.id}` : `/poster/${cue.id}`;
}

function selectedCueFieldsFromState() {
  const cue = selectedCue();
  const active = activeCue();
  elements.projectTitle.value = state.project?.title || "Playboy Show";
  elements.cueCount.textContent = `${state.project?.cues.length || 0} cues`;
  elements.volume.value = String(state.project?.transport.volume ?? 1);
  elements.volumeReadout.textContent = `${Math.round((state.project?.transport.volume ?? 1) * 100)}%`;

  if (!active) {
    elements.nowPlayingTitle.textContent = "No cue loaded";
    elements.nowPlayingMeta.textContent = "Open the output window, then take a cue.";
  } else {
    elements.nowPlayingTitle.textContent = active.name;
    elements.nowPlayingMeta.textContent = cueMeta(active) || "Ready";
  }

  const status = state.project?.transport.status || "stopped";
  elements.transportStatus.textContent = status[0].toUpperCase() + status.slice(1);
  elements.timeReadout.textContent = `${formatDuration(state.project?.transport.position || 0)} / ${formatDuration(
    state.project?.transport.duration || 0
  )}`;
  elements.scrub.max = String(state.project?.transport.duration || 1);
  elements.scrub.value = String(
    Math.min(state.project?.transport.position || 0, state.project?.transport.duration || 1)
  );

  if (!cue) {
    elements.selectedName.textContent = "Nothing selected";
    elements.selectedPoster.removeAttribute("src");
    elements.selectedFacts.innerHTML = "<div>Import media to start building a show.</div>";
    elements.cueName.value = "";
    elements.cueColor.value = "#ff9d48";
    elements.cueNotes.value = "";
    return;
  }

  elements.selectedName.textContent = cue.name;
  elements.selectedPoster.src = posterUrl(cue);
  elements.selectedPoster.alt = cue.name;
  elements.selectedFacts.innerHTML = [
    `<div>${cueMeta(cue) || "No media details available"}</div>`,
    `<div>${cue.path}</div>`,
    cue.formatName ? `<div>Format: ${cue.formatName}</div>` : ""
  ]
    .filter(Boolean)
    .join("");
  elements.cueName.value = cue.name;
  elements.cueColor.value = cue.color || "#ff9d48";
  elements.cueNotes.value = cue.notes || "";
}

function renderCueList() {
  const cues = state.project?.cues || [];
  elements.cueList.innerHTML = "";

  if (!cues.length) {
    const empty = document.createElement("div");
    empty.className = "muted";
    empty.textContent = "No cues yet. Import a few video files or stills to build the playlist.";
    elements.cueList.append(empty);
    return;
  }

  for (const cue of cues) {
    const fragment = elements.cueTemplate.content.cloneNode(true);
    const button = fragment.querySelector(".cue-item");
    const chip = fragment.querySelector(".cue-chip");
    const name = fragment.querySelector(".cue-name");
    const meta = fragment.querySelector(".cue-meta");

    button.dataset.cueId = cue.id;
    chip.style.background = cue.color;
    name.textContent = cue.name;
    meta.textContent = cueMeta(cue);

    if (cue.id === state.project.selectedCueId) {
      button.classList.add("active");
    }

    if (cue.id === state.project.activeCueId) {
      button.classList.add("playing");
    }

    button.addEventListener("click", () => updateCue(cue.id, { action: "select" }));
    button.addEventListener("dragstart", () => {
      state.draggedCueId = cue.id;
      button.classList.add("dragging");
    });
    button.addEventListener("dragend", () => {
      state.draggedCueId = null;
      button.classList.remove("dragging");
      state.draggingOverCueId = null;
    });
    button.addEventListener("dragover", (event) => {
      event.preventDefault();
      state.draggingOverCueId = cue.id;
    });
    button.addEventListener("drop", async (event) => {
      event.preventDefault();
      await reorderFromDrop(cue.id);
    });

    elements.cueList.append(button);
  }
}

async function fetchJson(path, options = {}) {
  const response = await fetch(path, {
    headers: {
      "Content-Type": "application/json"
    },
    ...options
  });
  const payload = await response.json();
  if (!response.ok) {
    throw new Error(payload.error || "Request failed");
  }
  return payload;
}

async function updateCue(cueId, payload) {
  await fetchJson(`/api/cues/${cueId}`, {
    method: "POST",
    body: JSON.stringify(payload)
  });
}

async function reorderFromDrop(targetCueId) {
  if (!state.draggedCueId || state.draggedCueId === targetCueId || !state.project) {
    return;
  }

  const orderedIds = state.project.cues.map((cue) => cue.id);
  const from = orderedIds.indexOf(state.draggedCueId);
  const to = orderedIds.indexOf(targetCueId);
  if (from < 0 || to < 0) {
    return;
  }

  orderedIds.splice(to, 0, orderedIds.splice(from, 1)[0]);
  await fetchJson("/api/cues/reorder", {
    method: "POST",
    body: JSON.stringify({ orderedIds })
  });
}

async function sendPlayback(action, extra = {}) {
  await fetchJson("/api/playback", {
    method: "POST",
    body: JSON.stringify({ action, ...extra })
  });
}

function render() {
  renderCueList();
  selectedCueFieldsFromState();
}

function applyState(nextState) {
  state.project = nextState;
  render();
}

function openOutputWindow() {
  window.open("/output", "playboy-output", "popup,width=1280,height=720");
}

async function importByPathPrompt() {
  const raw = window.prompt("Paste one absolute media path per line");
  if (!raw) {
    return;
  }

  const paths = raw
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);

  if (!paths.length) {
    return;
  }

  const result = await fetchJson("/api/import/paths", {
    method: "POST",
    body: JSON.stringify({ paths })
  });

  if (result.errors?.length) {
    window.alert(result.errors.map((item) => `${item.path}: ${item.error}`).join("\n"));
  }
}

function handleHotkeys(event) {
  if (event.defaultPrevented) {
    return;
  }

  const tag = document.activeElement?.tagName;
  if (tag === "INPUT" || tag === "TEXTAREA") {
    return;
  }

  if (event.key === "ArrowUp") {
    event.preventDefault();
    fetchJson("/api/cues/select-relative", {
      method: "POST",
      body: JSON.stringify({ direction: -1 })
    }).catch(showError);
    return;
  }

  if (event.key === "ArrowDown") {
    event.preventDefault();
    fetchJson("/api/cues/select-relative", {
      method: "POST",
      body: JSON.stringify({ direction: 1 })
    }).catch(showError);
    return;
  }

  if (event.key === "Enter") {
    event.preventDefault();
    const cue = selectedCue();
    if (cue) {
      sendPlayback("take", { cueId: cue.id, autoplay: false }).catch(showError);
    }
    return;
  }

  if (event.key === " ") {
    event.preventDefault();
    sendPlayback("toggle").catch(showError);
    return;
  }

  if (event.key.toLowerCase() === "s") {
    event.preventDefault();
    sendPlayback("stop").catch(showError);
    return;
  }

  if (event.key.toLowerCase() === "o") {
    event.preventDefault();
    openOutputWindow();
    return;
  }

  if (event.key.toLowerCase() === "f") {
    event.preventDefault();
    sendPlayback("fullscreen").catch(showError);
  }
}

function showError(error) {
  console.error(error);
  window.alert(error.message || "Something went wrong");
}

async function init() {
  const project = await fetchJson("/api/state", { method: "GET", headers: {} });
  applyState(project);

  const events = new EventSource("/events");
  events.addEventListener("state", (message) => {
    applyState(JSON.parse(message.data));
  });

  events.addEventListener("error", () => {
    console.warn("Event stream disconnected");
  });

  elements.saveProject.addEventListener("click", async () => {
    await fetchJson("/api/project", {
      method: "POST",
      body: JSON.stringify({ title: elements.projectTitle.value })
    });
  });

  elements.importPick.addEventListener("click", async () => {
    const result = await fetchJson("/api/import/pick", {
      method: "POST",
      body: "{}"
    });
    if (result.errors?.length) {
      window.alert(result.errors.map((item) => `${item.path}: ${item.error}`).join("\n"));
    }
  });

  elements.importPaths.addEventListener("click", () => {
    importByPathPrompt().catch(showError);
  });

  elements.openOutput.addEventListener("click", openOutputWindow);
  elements.toggleFullscreen.addEventListener("click", () => {
    sendPlayback("fullscreen").catch(showError);
  });

  elements.takeButton.addEventListener("click", () => {
    const cue = selectedCue();
    if (cue) {
      sendPlayback("take", { cueId: cue.id, autoplay: false }).catch(showError);
    }
  });

  elements.goButton.addEventListener("click", () => {
    sendPlayback("toggle").catch(showError);
  });

  elements.stopButton.addEventListener("click", () => {
    sendPlayback("stop").catch(showError);
  });

  elements.clearButton.addEventListener("click", () => {
    sendPlayback("clear").catch(showError);
  });

  elements.scrub.addEventListener("change", () => {
    sendPlayback("seek", { position: Number(elements.scrub.value) }).catch(showError);
  });

  elements.volume.addEventListener("input", () => {
    elements.volumeReadout.textContent = `${Math.round(Number(elements.volume.value) * 100)}%`;
  });

  elements.volume.addEventListener("change", () => {
    sendPlayback("set-volume", { volume: Number(elements.volume.value) }).catch(showError);
  });

  elements.saveCue.addEventListener("click", async () => {
    const cue = selectedCue();
    if (!cue) {
      return;
    }

    await updateCue(cue.id, {
      name: elements.cueName.value,
      color: elements.cueColor.value,
      notes: elements.cueNotes.value
    });
  });

  elements.deleteCue.addEventListener("click", async () => {
    const cue = selectedCue();
    if (!cue) {
      return;
    }

    if (!window.confirm(`Delete cue "${cue.name}"?`)) {
      return;
    }

    await fetchJson(`/api/cues/${cue.id}`, {
      method: "DELETE"
    });
  });

  window.addEventListener("keydown", handleHotkeys);
}

init().catch(showError);
