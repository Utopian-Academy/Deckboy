const http = require("http");
const fs = require("fs");
const fsp = fs.promises;
const path = require("path");
const os = require("os");
const crypto = require("crypto");
const { spawn } = require("child_process");

const ROOT_DIR = path.resolve(__dirname, "..");
const PUBLIC_DIR = path.join(ROOT_DIR, "public");
const DATA_DIR = path.join(ROOT_DIR, "data");
const POSTER_DIR = path.join(DATA_DIR, "posters");
const PROJECT_FILE = path.join(DATA_DIR, "project.json");
const HOST = process.env.DECKBOY_HOST || "127.0.0.1";
const PORT = Number(process.env.DECKBOY_PORT || 5050);

const IMAGE_EXTENSIONS = new Set([
  ".png",
  ".jpg",
  ".jpeg",
  ".webp",
  ".gif",
  ".bmp",
  ".tif",
  ".tiff",
  ".avif"
]);

const MIME_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "application/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".webp": "image/webp",
  ".gif": "image/gif",
  ".svg": "image/svg+xml",
  ".mp4": "video/mp4",
  ".mov": "video/quicktime",
  ".m4v": "video/x-m4v",
  ".webm": "video/webm",
  ".mkv": "video/x-matroska",
  ".avi": "video/x-msvideo",
  ".mpg": "video/mpeg",
  ".mpeg": "video/mpeg",
  ".ts": "video/mp2t",
  ".mp3": "audio/mpeg",
  ".wav": "audio/wav",
  ".aac": "audio/aac"
};

const eventClients = new Set();
let saveQueue = Promise.resolve();
let state = createDefaultState();

function createDefaultState() {
  const now = new Date().toISOString();
  return {
    schemaVersion: 1,
    title: "Deckboy Show",
    cues: [],
    selectedCueId: null,
    activeCueId: null,
    transport: {
      status: "stopped",
      currentCueId: null,
      position: 0,
      duration: 0,
      volume: 1,
      updatedAt: now
    },
    output: {
      ready: false,
      fullscreen: false,
      lastSeenAt: null
    },
    updatedAt: now
  };
}

function sanitizeState(raw) {
  const next = createDefaultState();
  if (!raw || typeof raw !== "object") {
    return next;
  }

  next.title = typeof raw.title === "string" && raw.title.trim() ? raw.title.trim() : next.title;
  next.cues = Array.isArray(raw.cues) ? raw.cues.map(sanitizeCue).filter(Boolean) : [];
  next.selectedCueId = typeof raw.selectedCueId === "string" ? raw.selectedCueId : null;
  next.activeCueId = typeof raw.activeCueId === "string" ? raw.activeCueId : null;

  if (raw.transport && typeof raw.transport === "object") {
    next.transport.status = sanitizeStatus(raw.transport.status);
    next.transport.currentCueId =
      typeof raw.transport.currentCueId === "string" ? raw.transport.currentCueId : null;
    next.transport.position = clampNumber(raw.transport.position, 0, Number.MAX_SAFE_INTEGER, 0);
    next.transport.duration = clampNumber(raw.transport.duration, 0, Number.MAX_SAFE_INTEGER, 0);
    next.transport.volume = clampNumber(raw.transport.volume, 0, 1, 1);
    next.transport.updatedAt =
      typeof raw.transport.updatedAt === "string" ? raw.transport.updatedAt : next.transport.updatedAt;
  }

  if (raw.output && typeof raw.output === "object") {
    next.output.ready = Boolean(raw.output.ready);
    next.output.fullscreen = Boolean(raw.output.fullscreen);
    next.output.lastSeenAt =
      typeof raw.output.lastSeenAt === "string" ? raw.output.lastSeenAt : null;
  }

  next.updatedAt = typeof raw.updatedAt === "string" ? raw.updatedAt : next.updatedAt;

  if (next.selectedCueId && !next.cues.some((cue) => cue.id === next.selectedCueId)) {
    next.selectedCueId = next.cues[0]?.id || null;
  }

  if (next.activeCueId && !next.cues.some((cue) => cue.id === next.activeCueId)) {
    next.activeCueId = null;
  }

  if (next.transport.currentCueId && !next.cues.some((cue) => cue.id === next.transport.currentCueId)) {
    next.transport.currentCueId = null;
  }

  return next;
}

function sanitizeCue(raw) {
  if (!raw || typeof raw !== "object" || typeof raw.id !== "string" || typeof raw.path !== "string") {
    return null;
  }

  const kind = raw.kind === "image" ? "image" : "video";

  return {
    id: raw.id,
    kind,
    name:
      typeof raw.name === "string" && raw.name.trim()
        ? raw.name.trim().slice(0, 120)
        : path.parse(raw.path).name,
    path: raw.path,
    duration:
      kind === "image" ? null : nullableNumber(raw.duration, 0, Number.MAX_SAFE_INTEGER),
    width: nullableNumber(raw.width, 1, 100000),
    height: nullableNumber(raw.height, 1, 100000),
    frameRate: nullableNumber(raw.frameRate, 0, 480),
    formatName: typeof raw.formatName === "string" ? raw.formatName : "",
    videoCodec: typeof raw.videoCodec === "string" ? raw.videoCodec : "",
    audioCodec: typeof raw.audioCodec === "string" ? raw.audioCodec : "",
    hasAudio: Boolean(raw.hasAudio),
    sizeBytes: nullableNumber(raw.sizeBytes, 0, Number.MAX_SAFE_INTEGER),
    notes: typeof raw.notes === "string" ? raw.notes.slice(0, 4000) : "",
    color: sanitizeColor(raw.color),
    createdAt: typeof raw.createdAt === "string" ? raw.createdAt : new Date().toISOString(),
    updatedAt: typeof raw.updatedAt === "string" ? raw.updatedAt : new Date().toISOString()
  };
}

function sanitizeStatus(value) {
  return ["stopped", "playing", "paused"].includes(value) ? value : "stopped";
}

function sanitizeColor(value) {
  return /^#[0-9a-f]{6}$/i.test(value || "") ? value.toLowerCase() : "#ff9d48";
}

function clampNumber(value, min, max, fallback) {
  const num = Number(value);
  if (!Number.isFinite(num)) {
    return fallback;
  }
  return Math.min(max, Math.max(min, num));
}

function nullableNumber(value, min, max) {
  const num = Number(value);
  if (!Number.isFinite(num)) {
    return null;
  }
  return Math.min(max, Math.max(min, num));
}

function markStateUpdated() {
  const now = new Date().toISOString();
  state.updatedAt = now;
  state.transport.updatedAt = now;
}

function getCueById(cueId) {
  return state.cues.find((cue) => cue.id === cueId) || null;
}

function getCueIndex(cueId) {
  return state.cues.findIndex((cue) => cue.id === cueId);
}

function getPublicState() {
  return JSON.parse(JSON.stringify(state));
}

async function ensureProjectFiles() {
  await fsp.mkdir(DATA_DIR, { recursive: true });
  await fsp.mkdir(POSTER_DIR, { recursive: true });
  try {
    const raw = await fsp.readFile(PROJECT_FILE, "utf8");
    state = sanitizeState(JSON.parse(raw));
  } catch (error) {
    if (error.code !== "ENOENT") {
      throw error;
    }
    await persistState();
  }
}

function persistState() {
  saveQueue = saveQueue.then(async () => {
    const tempFile = `${PROJECT_FILE}.tmp`;
    await fsp.writeFile(tempFile, JSON.stringify(state, null, 2));
    await fsp.rename(tempFile, PROJECT_FILE);
  });
  return saveQueue;
}

function sendJson(res, statusCode, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store"
  });
  res.end(body);
}

function sendText(res, statusCode, body) {
  res.writeHead(statusCode, {
    "Content-Type": "text/plain; charset=utf-8",
    "Content-Length": Buffer.byteLength(body)
  });
  res.end(body);
}

async function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    let body = "";

    req.on("data", (chunk) => {
      body += chunk;
      if (body.length > 1024 * 1024) {
        reject(new Error("Request body too large"));
        req.destroy();
      }
    });

    req.on("end", () => {
      if (!body) {
        resolve({});
        return;
      }

      try {
        resolve(JSON.parse(body));
      } catch (error) {
        reject(new Error("Invalid JSON body"));
      }
    });

    req.on("error", reject);
  });
}

function broadcast(eventName, payload) {
  const message = `event: ${eventName}\ndata: ${JSON.stringify(payload)}\n\n`;

  for (const client of eventClients) {
    client.write(message);
  }
}

function syncState() {
  broadcast("state", getPublicState());
}

function sendCommand(command) {
  broadcast("command", command);
}

function parseFrameRate(rate) {
  if (!rate || typeof rate !== "string" || !rate.includes("/")) {
    return null;
  }

  const [num, den] = rate.split("/").map(Number);
  if (!Number.isFinite(num) || !Number.isFinite(den) || den === 0) {
    return null;
  }

  return Number((num / den).toFixed(3));
}

function inferMimeType(filePath) {
  return MIME_TYPES[path.extname(filePath).toLowerCase()] || "application/octet-stream";
}

async function runProcess(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      stdio: ["ignore", "pipe", "pipe"],
      ...options
    });

    let stdout = "";
    let stderr = "";

    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString();
    });

    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });

    child.on("error", reject);
    child.on("close", (code) => {
      resolve({ code, stdout, stderr });
    });
  });
}

async function analyzeMedia(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  const stat = await fsp.stat(filePath);
  const probe = await runProcess("ffprobe", [
    "-v",
    "error",
    "-print_format",
    "json",
    "-show_entries",
    "format=duration,format_name:format_tags=title:stream=index,codec_type,codec_name,width,height,r_frame_rate",
    filePath
  ]);

  if (probe.code !== 0) {
    throw new Error(probe.stderr.trim() || "ffprobe could not read the file");
  }

  const metadata = JSON.parse(probe.stdout || "{}");
  const streams = Array.isArray(metadata.streams) ? metadata.streams : [];
  const videoStream = streams.find((stream) => stream.codec_type === "video") || null;
  const audioStream = streams.find((stream) => stream.codec_type === "audio") || null;
  const format = metadata.format || {};
  const isImage =
    IMAGE_EXTENSIONS.has(ext) ||
    typeof format.format_name === "string" && format.format_name.includes("image2");

  if (!videoStream && !isImage) {
    throw new Error("Only video and image files are supported in this build");
  }

  const kind = isImage ? "image" : "video";
  const duration = kind === "image" ? null : nullableNumber(format.duration, 0, Number.MAX_SAFE_INTEGER);
  const title = typeof format.tags?.title === "string" && format.tags.title.trim()
    ? format.tags.title.trim()
    : path.parse(filePath).name;
  const now = new Date().toISOString();

  return {
    kind,
    name: title.slice(0, 120),
    path: filePath,
    duration,
    width: nullableNumber(videoStream?.width, 1, 100000),
    height: nullableNumber(videoStream?.height, 1, 100000),
    frameRate: kind === "image" ? null : parseFrameRate(videoStream?.r_frame_rate),
    formatName: typeof format.format_name === "string" ? format.format_name : "",
    videoCodec: typeof videoStream?.codec_name === "string" ? videoStream.codec_name : "",
    audioCodec: typeof audioStream?.codec_name === "string" ? audioStream.codec_name : "",
    hasAudio: Boolean(audioStream),
    sizeBytes: stat.size,
    notes: "",
    color: "#ff9d48",
    createdAt: now,
    updatedAt: now
  };
}

async function generatePoster(cue) {
  if (cue.kind !== "video") {
    return;
  }

  const posterPath = path.join(POSTER_DIR, `${cue.id}.jpg`);
  const seekTime = cue.duration ? Math.min(Math.max(cue.duration * 0.1, 0.25), 3) : 0.25;
  const result = await runProcess("ffmpeg", [
    "-hide_banner",
    "-loglevel",
    "error",
    "-y",
    "-ss",
    seekTime.toFixed(2),
    "-i",
    cue.path,
    "-frames:v",
    "1",
    "-vf",
    "scale=960:-1:force_original_aspect_ratio=decrease",
    posterPath
  ]);

  if (result.code !== 0) {
    await fsp.rm(posterPath, { force: true });
  }
}

async function importPaths(paths) {
  const added = [];
  const errors = [];

  for (const item of paths) {
    const filePath = path.resolve(String(item || "").trim());
    if (!filePath) {
      continue;
    }

    try {
      await fsp.access(filePath, fs.constants.R_OK);
      const cue = await analyzeMedia(filePath);
      cue.id = crypto.randomUUID();
      await generatePoster(cue);
      state.cues.push(cue);
      added.push(cue);
    } catch (error) {
      errors.push({
        path: filePath,
        error: error.message
      });
    }
  }

  if (added.length) {
    if (!state.selectedCueId) {
      state.selectedCueId = added[0].id;
    }
    markStateUpdated();
    await persistState();
    syncState();
  }

  return { added, errors };
}

async function pickFiles() {
  if (process.platform === "linux") {
    const result = await runProcess("zenity", [
      "--file-selection",
      "--multiple",
      "--separator=|",
      "--title=Import media into Deckboy"
    ]);

    if (result.code === 1) {
      return [];
    }

    if (result.code !== 0) {
      throw new Error(result.stderr.trim() || "zenity failed to open");
    }

    return result.stdout
      .trim()
      .split("|")
      .map((value) => value.trim())
      .filter(Boolean);
  }

  if (process.platform === "darwin") {
    const result = await runProcess("osascript", [
      "-e",
      'set filesPicked to choose file with multiple selections allowed true',
      "-e",
      'set outputLines to {}',
      "-e",
      'repeat with currentFile in filesPicked',
      "-e",
      'set end of outputLines to POSIX path of currentFile',
      "-e",
      'end repeat',
      "-e",
      'set AppleScript\'s text item delimiters to linefeed',
      "-e",
      'return outputLines as text'
    ]);

    if (result.code === 1) {
      return [];
    }

    if (result.code !== 0) {
      throw new Error(result.stderr.trim() || "osascript failed to open");
    }

    return result.stdout
      .trim()
      .split(/\r?\n/)
      .map((value) => value.trim())
      .filter(Boolean);
  }

  if (process.platform === "win32") {
    const script = [
      "Add-Type -AssemblyName System.Windows.Forms",
      "$dialog = New-Object System.Windows.Forms.OpenFileDialog",
      '$dialog.Multiselect = $true',
      '$dialog.Title = "Import media into Deckboy"',
      "if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 }",
      "$dialog.FileNames -join \"`n\""
    ].join(";");
    const result = await runProcess("powershell.exe", ["-NoProfile", "-Command", script]);

    if (result.code === 1) {
      return [];
    }

    if (result.code !== 0) {
      throw new Error(result.stderr.trim() || "PowerShell file picker failed to open");
    }

    return result.stdout
      .trim()
      .split(/\r?\n/)
      .map((value) => value.trim())
      .filter(Boolean);
  }

  throw new Error(`File picker not implemented for ${process.platform}`);
}

async function streamFile(req, res, filePath) {
  const stat = await fsp.stat(filePath);
  const mimeType = inferMimeType(filePath);
  const range = req.headers.range;

  if (!range) {
    res.writeHead(200, {
      "Content-Type": mimeType,
      "Content-Length": stat.size,
      "Accept-Ranges": "bytes",
      "Cache-Control": "no-store"
    });

    if (req.method === "HEAD") {
      res.end();
      return;
    }

    fs.createReadStream(filePath).pipe(res);
    return;
  }

  const match = range.match(/bytes=(\d*)-(\d*)/);
  if (!match) {
    sendText(res, 416, "Invalid range");
    return;
  }

  const start = match[1] ? Number(match[1]) : 0;
  const end = match[2] ? Number(match[2]) : stat.size - 1;

  if (!Number.isFinite(start) || !Number.isFinite(end) || start > end || end >= stat.size) {
    sendText(res, 416, "Range not satisfiable");
    return;
  }

  res.writeHead(206, {
    "Content-Type": mimeType,
    "Content-Length": end - start + 1,
    "Content-Range": `bytes ${start}-${end}/${stat.size}`,
    "Accept-Ranges": "bytes",
    "Cache-Control": "no-store"
  });

  if (req.method === "HEAD") {
    res.end();
    return;
  }

  fs.createReadStream(filePath, { start, end }).pipe(res);
}

async function serveStaticFile(res, filePath) {
  const ext = path.extname(filePath).toLowerCase();
  const contentType = MIME_TYPES[ext] || "application/octet-stream";
  const body = await fsp.readFile(filePath);
  res.writeHead(200, {
    "Content-Type": contentType,
    "Content-Length": body.length,
    "Cache-Control": "no-store"
  });
  res.end(body);
}

async function handlePlaybackAction(body) {
  const action = String(body.action || "");
  const cueId = typeof body.cueId === "string" ? body.cueId : state.selectedCueId;
  const cue = cueId ? getCueById(cueId) : null;

  if (action === "take") {
    if (!cue) {
      throw new Error("Cue not found");
    }

    const autoplay = Boolean(body.autoplay);
    state.activeCueId = cue.id;
    state.selectedCueId = cue.id;
    state.transport.currentCueId = cue.id;
    state.transport.position = 0;
    state.transport.duration = cue.duration || 0;
    state.transport.status = cue.kind === "image" ? "paused" : autoplay ? "playing" : "paused";
    state.output.ready = false;
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({
      name: "take",
      cueId: cue.id,
      autoplay,
      nonce: crypto.randomUUID()
    });
    return;
  }

  if (action === "play") {
    if (!state.activeCueId && state.selectedCueId) {
      await handlePlaybackAction({
        action: "take",
        cueId: state.selectedCueId,
        autoplay: true
      });
      return;
    }

    state.transport.status = "playing";
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({ name: "play", nonce: crypto.randomUUID() });
    return;
  }

  if (action === "pause") {
    state.transport.status = "paused";
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({ name: "pause", nonce: crypto.randomUUID() });
    return;
  }

  if (action === "toggle") {
    if (!state.activeCueId && state.selectedCueId) {
      await handlePlaybackAction({
        action: "take",
        cueId: state.selectedCueId,
        autoplay: true
      });
      return;
    }

    state.transport.status = state.transport.status === "playing" ? "paused" : "playing";
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({ name: "toggle", nonce: crypto.randomUUID() });
    return;
  }

  if (action === "stop") {
    state.transport.status = "stopped";
    state.transport.position = 0;
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({ name: "stop", nonce: crypto.randomUUID() });
    return;
  }

  if (action === "seek") {
    const position = clampNumber(body.position, 0, Number.MAX_SAFE_INTEGER, 0);
    state.transport.position = position;
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({ name: "seek", position, nonce: crypto.randomUUID() });
    return;
  }

  if (action === "set-volume") {
    const volume = clampNumber(body.volume, 0, 1, 1);
    state.transport.volume = volume;
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({ name: "set-volume", volume, nonce: crypto.randomUUID() });
    return;
  }

  if (action === "fullscreen") {
    sendCommand({ name: "fullscreen", nonce: crypto.randomUUID() });
    return;
  }

  if (action === "clear") {
    state.activeCueId = null;
    state.transport.currentCueId = null;
    state.transport.position = 0;
    state.transport.duration = 0;
    state.transport.status = "stopped";
    state.output.ready = false;
    markStateUpdated();
    await persistState();
    syncState();
    sendCommand({ name: "clear", nonce: crypto.randomUUID() });
    return;
  }

  throw new Error("Unsupported playback action");
}

function selectCue(cueId) {
  const cue = getCueById(cueId);
  if (!cue) {
    throw new Error("Cue not found");
  }

  state.selectedCueId = cue.id;
  markStateUpdated();
}

function selectRelativeCue(direction) {
  if (!state.cues.length) {
    return null;
  }

  const currentIndex = state.selectedCueId ? getCueIndex(state.selectedCueId) : -1;
  const nextIndex = currentIndex < 0
    ? 0
    : Math.min(state.cues.length - 1, Math.max(0, currentIndex + direction));
  state.selectedCueId = state.cues[nextIndex].id;
  markStateUpdated();
  return state.selectedCueId;
}

async function handleOutputEvent(body) {
  const type = String(body.type || "");
  const cueId = typeof body.cueId === "string" ? body.cueId : state.activeCueId;
  const cue = cueId ? getCueById(cueId) : null;
  state.output.lastSeenAt = new Date().toISOString();

  if (type === "ready") {
    state.output.ready = true;
  } else if (type === "loaded") {
    state.output.ready = true;
    if (cue) {
      state.activeCueId = cue.id;
      state.transport.currentCueId = cue.id;
      state.transport.duration = clampNumber(body.duration, 0, Number.MAX_SAFE_INTEGER, cue.duration || 0);
      state.transport.position = 0;
      state.transport.status = cue.kind === "image" ? "paused" : "paused";
    }
  } else if (type === "play") {
    state.transport.status = "playing";
  } else if (type === "pause") {
    if (state.transport.status !== "stopped") {
      state.transport.status = "paused";
    }
    state.transport.position = clampNumber(
      body.position,
      0,
      Number.MAX_SAFE_INTEGER,
      state.transport.position
    );
  } else if (type === "timeupdate") {
    state.transport.position = clampNumber(
      body.position,
      0,
      Number.MAX_SAFE_INTEGER,
      state.transport.position
    );
    state.transport.duration = clampNumber(
      body.duration,
      0,
      Number.MAX_SAFE_INTEGER,
      state.transport.duration
    );
  } else if (type === "ended") {
    state.transport.status = "stopped";
    state.transport.position = state.transport.duration;
    if (cue) {
      const index = getCueIndex(cue.id);
      const nextCue = state.cues[index + 1];
      if (nextCue) {
        state.selectedCueId = nextCue.id;
      }
    }
  } else if (type === "stop") {
    state.transport.status = "stopped";
    state.transport.position = 0;
  } else if (type === "cleared") {
    state.activeCueId = null;
    state.transport.currentCueId = null;
    state.transport.position = 0;
    state.transport.duration = 0;
    state.transport.status = "stopped";
  } else if (type === "volumechange") {
    state.transport.volume = clampNumber(body.volume, 0, 1, state.transport.volume);
  } else if (type === "fullscreenchange") {
    state.output.fullscreen = Boolean(body.fullscreen);
  }

  markStateUpdated();
  await persistState();
  syncState();
}

async function deleteCue(cueId) {
  const index = getCueIndex(cueId);
  if (index < 0) {
    throw new Error("Cue not found");
  }

  const [removed] = state.cues.splice(index, 1);
  await fsp.rm(path.join(POSTER_DIR, `${removed.id}.jpg`), { force: true });

  if (state.selectedCueId === removed.id) {
    state.selectedCueId = state.cues[index]?.id || state.cues[index - 1]?.id || null;
  }

  if (state.activeCueId === removed.id) {
    state.activeCueId = null;
    state.transport.currentCueId = null;
    state.transport.position = 0;
    state.transport.duration = 0;
    state.transport.status = "stopped";
    sendCommand({ name: "clear", nonce: crypto.randomUUID() });
  }

  markStateUpdated();
  await persistState();
  syncState();
}

async function handleRequest(req, res) {
  const url = new URL(req.url, `http://${req.headers.host || HOST}`);
  const pathname = decodeURIComponent(url.pathname);

  if (req.method === "GET" && pathname === "/events") {
    res.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache, no-transform",
      Connection: "keep-alive",
      "X-Accel-Buffering": "no"
    });

    res.write(`event: state\ndata: ${JSON.stringify(getPublicState())}\n\n`);
    eventClients.add(res);

    const heartbeat = setInterval(() => {
      res.write(": ping\n\n");
    }, 15000);

    req.on("close", () => {
      clearInterval(heartbeat);
      eventClients.delete(res);
    });
    return;
  }

  if (req.method === "GET" && pathname === "/api/state") {
    sendJson(res, 200, getPublicState());
    return;
  }

  if (req.method === "POST" && pathname === "/api/project") {
    const body = await readJsonBody(req);
    const title = typeof body.title === "string" ? body.title.trim().slice(0, 120) : "";
    if (!title) {
      sendJson(res, 400, { error: "Project title is required" });
      return;
    }

    state.title = title;
    markStateUpdated();
    await persistState();
    syncState();
    sendJson(res, 200, { ok: true, state: getPublicState() });
    return;
  }

  if (req.method === "POST" && pathname === "/api/import/paths") {
    const body = await readJsonBody(req);
    const paths = Array.isArray(body.paths) ? body.paths : [];
    const result = await importPaths(paths);
    sendJson(res, 200, result);
    return;
  }

  if (req.method === "POST" && pathname === "/api/import/pick") {
    const files = await pickFiles();
    const result = await importPaths(files);
    sendJson(res, 200, result);
    return;
  }

  if (req.method === "POST" && pathname === "/api/playback") {
    const body = await readJsonBody(req);
    await handlePlaybackAction(body);
    sendJson(res, 200, { ok: true, state: getPublicState() });
    return;
  }

  if (req.method === "POST" && pathname === "/api/output/event") {
    const body = await readJsonBody(req);
    await handleOutputEvent(body);
    sendJson(res, 200, { ok: true });
    return;
  }

  if (req.method === "POST" && pathname === "/api/cues/select-relative") {
    const body = await readJsonBody(req);
    const direction = clampNumber(body.direction, -1, 1, 0);
    const selectedCueId = selectRelativeCue(direction);
    await persistState();
    syncState();
    sendJson(res, 200, { ok: true, selectedCueId });
    return;
  }

  const cueMatch = pathname.match(/^\/api\/cues\/([^/]+)$/);
  if (cueMatch) {
    const cueId = cueMatch[1];

    if (req.method === "POST") {
      const body = await readJsonBody(req);
      if (body.action === "select") {
        selectCue(cueId);
        await persistState();
        syncState();
        sendJson(res, 200, { ok: true, state: getPublicState() });
        return;
      }

      const cue = getCueById(cueId);
      if (!cue) {
        sendJson(res, 404, { error: "Cue not found" });
        return;
      }

      if (typeof body.name === "string" && body.name.trim()) {
        cue.name = body.name.trim().slice(0, 120);
      }

      if (typeof body.notes === "string") {
        cue.notes = body.notes.slice(0, 4000);
      }

      if (typeof body.color === "string") {
        cue.color = sanitizeColor(body.color);
      }

      cue.updatedAt = new Date().toISOString();
      markStateUpdated();
      await persistState();
      syncState();
      sendJson(res, 200, { ok: true, cue });
      return;
    }

    if (req.method === "DELETE") {
      await deleteCue(cueId);
      sendJson(res, 200, { ok: true, state: getPublicState() });
      return;
    }
  }

  if (req.method === "POST" && pathname === "/api/cues/reorder") {
    const body = await readJsonBody(req);
    const orderedIds = Array.isArray(body.orderedIds) ? body.orderedIds : [];
    if (orderedIds.length !== state.cues.length) {
      sendJson(res, 400, { error: "Ordered id list is incomplete" });
      return;
    }

    const currentById = new Map(state.cues.map((cue) => [cue.id, cue]));
    if (orderedIds.some((id) => !currentById.has(id))) {
      sendJson(res, 400, { error: "Ordered id list contains unknown cues" });
      return;
    }

    state.cues = orderedIds.map((id) => currentById.get(id));
    markStateUpdated();
    await persistState();
    syncState();
    sendJson(res, 200, { ok: true, state: getPublicState() });
    return;
  }

  const mediaMatch = pathname.match(/^\/media\/([^/]+)$/);
  if (mediaMatch && ["GET", "HEAD"].includes(req.method)) {
    const cue = getCueById(mediaMatch[1]);
    if (!cue) {
      sendJson(res, 404, { error: "Cue not found" });
      return;
    }

    await streamFile(req, res, cue.path);
    return;
  }

  const posterMatch = pathname.match(/^\/poster\/([^/]+)$/);
  if (posterMatch && ["GET", "HEAD"].includes(req.method)) {
    const cue = getCueById(posterMatch[1]);
    if (!cue) {
      sendJson(res, 404, { error: "Cue not found" });
      return;
    }

    const posterPath = path.join(POSTER_DIR, `${cue.id}.jpg`);
    try {
      await streamFile(req, res, posterPath);
    } catch (error) {
      if (cue.kind === "image") {
        await streamFile(req, res, cue.path);
        return;
      }
      sendText(res, 404, "Poster not found");
    }
    return;
  }

  const staticPath = pathname === "/"
    ? path.join(PUBLIC_DIR, "index.html")
    : pathname === "/output"
      ? path.join(PUBLIC_DIR, "output.html")
      : path.join(PUBLIC_DIR, pathname.slice(1));

  if (staticPath.startsWith(PUBLIC_DIR)) {
    try {
      await serveStaticFile(res, staticPath);
      return;
    } catch (error) {
      if (error.code !== "ENOENT") {
        throw error;
      }
    }
  }

  sendText(res, 404, "Not found");
}

async function start() {
  await ensureProjectFiles();

  const server = http.createServer((req, res) => {
    handleRequest(req, res).catch((error) => {
      console.error(error);
      if (!res.headersSent) {
        sendJson(res, 500, { error: error.message || "Internal server error" });
      } else {
        res.destroy();
      }
    });
  });

  server.listen(PORT, HOST, () => {
    console.log(`Deckboy listening on http://${HOST}:${PORT}`);
  });
}

start().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
