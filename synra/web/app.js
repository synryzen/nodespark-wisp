const stage = document.querySelector(".synra-stage");
const stateLabel = document.getElementById("stateLabel");
const messageText = document.getElementById("messageText");
const subtitleText = document.getElementById("subtitleText");
const cardStyle = document.getElementById("cardStyle");
const cardTitle = document.getElementById("cardTitle");
const cardBody = document.getElementById("cardBody");
const cardDetail = document.getElementById("cardDetail");
const progressBar = document.getElementById("progressBar");
const modeStrip = document.getElementById("modeStrip");
const listenButton = document.getElementById("listenButton");
const cameraButton = document.getElementById("cameraButton");
const voiceNote = document.getElementById("voiceNote");
const micStatus = document.getElementById("micStatus");
const cameraStatus = document.getElementById("cameraStatus");
const presenceVideo = document.getElementById("presenceVideo");

let lastSpeechId = "";
let recognition = null;
let isListening = false;
let cameraStream = null;
let faceDetector = null;
let audioLevel = 0;
let targetMotion = { x: 0, y: 0, rotate: 0, scale: 1, mouth: 0 };
let currentMotion = { x: 0, y: 0, rotate: 0, scale: 1, mouth: 0 };
let blinkUntil = 0;
let nextBlink = performance.now() + 1600;
let lastStateMode = "idle";

const demoText = {
  listening: "I’m listening. Tell me what you want NodeSparkHub to do.",
  thinking: "Give me a second. I’m tracing the best workflow path.",
  speaking: "NodeSparkHub is online. I’m ready to help.",
  success: "Done. That workflow landed cleanly."
};

async function fetchState() {
  try {
    const response = await fetch("/api/state", { cache: "no-store" });
    const contentType = response.headers.get("content-type") || "";
    if (!response.ok || !contentType.includes("application/json")) return;
    const data = await response.json();
    if (data.ok) renderState(data.state);
  } catch (error) {
    renderState({
      mode: "error",
      expression: "concerned",
      message: "Synra lost contact with her local daemon.",
      subtitle: String(error),
      card: {
        title: "Local API Offline",
        body: "Check the nodespark-synra service.",
        detail: "The monitor UI is still running.",
        style: "error"
      }
    });
  }
}

function renderState(state) {
  stage.dataset.mode = state.mode || "idle";
  stage.dataset.expression = state.expression || "soft_smile";
  lastStateMode = state.mode || "idle";
  stateLabel.textContent = state.mode || "idle";
  messageText.textContent = state.message || "NodeSparkHub is waiting for a workflow.";
  subtitleText.textContent = state.subtitle || "Ready";

  const card = state.card || {};
  cardStyle.textContent = card.style || "info";
  cardTitle.textContent = card.title || "NodeSparkHub";
  cardBody.textContent = card.body || "Synra is ready.";
  cardDetail.textContent = card.detail || "Waiting for your next command.";
  const progress = typeof card.progress === "number" ? Math.max(0, Math.min(1, card.progress)) : null;
  progressBar.style.width = progress === null ? "36%" : `${Math.round(progress * 100)}%`;

  [...modeStrip.children].forEach((item) => {
    const active = item.textContent === state.mode;
    item.style.borderColor = active ? "rgba(76, 201, 255, 0.9)" : "";
    item.style.background = active ? "rgba(76, 201, 255, 0.16)" : "";
  });

  maybeSpeak(state);
}

function maybeSpeak(state) {
  const speechText = (state.speech_text || "").trim();
  const speechId = state.speech_id || "";
  if (!speechText || !speechId || speechId === lastSpeechId) return;
  lastSpeechId = speechId;
  if (!("speechSynthesis" in window)) return;

  window.speechSynthesis.cancel();
  const utterance = new SpeechSynthesisUtterance(speechText);
  utterance.rate = 0.96;
  utterance.pitch = 1.08;
  utterance.volume = 1.0;
  utterance.onstart = () => {
    targetMotion.mouth = 1;
    stage.dataset.mode = "speaking";
  };
  utterance.onend = () => {
    targetMotion.mouth = 0;
    stage.dataset.mode = lastStateMode || "idle";
  };
  const voices = window.speechSynthesis.getVoices();
  const preferred = voices.find((voice) => /female|samantha|zira|google us english/i.test(voice.name));
  if (preferred) utterance.voice = preferred;
  window.speechSynthesis.speak(utterance);
}

async function sendDemo(mode) {
  const payload = {
    type: mode === "success" ? "success" : "setState",
    mode,
    expression: mode === "success" ? "wink" : mode === "thinking" ? "focused" : mode === "listening" ? "attentive" : "bright",
    message: demoText[mode] || "Synra is ready.",
    subtitle: "Local demo",
    card: {
      title: mode === "success" ? "Workflow Complete" : "Synra Demo",
      body: demoText[mode] || "State changed.",
      detail: "Local monitor command",
      style: mode
    }
  };
  await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
  fetchState();
}

async function setRemoteState(payload) {
  await fetch("/api/state", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload)
  });
  fetchState();
}

async function askAssistant(text) {
  const trimmed = text.trim();
  if (!trimmed) {
    await setRemoteState({
      mode: "warning",
      expression: "concerned",
      message: "I did not catch that.",
      subtitle: "Try again",
      card: {
        title: "No Voice Input",
        body: "Synra did not receive a transcript.",
        detail: "Microphone input ended without speech.",
        style: "warning"
      }
    });
    return;
  }

  await setRemoteState({
    mode: "thinking",
    expression: "focused",
    message: `Thinking about: ${trimmed}`,
    subtitle: "NodeSparkHub Assistant",
    card: {
      title: "Voice Request",
      body: trimmed,
      detail: "Sending to NodeSparkHub",
      style: "thinking"
    }
  });

  await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      id: `voice-${Date.now()}`,
      type: "assistant",
      text: trimmed
    })
  });
  fetchState();
}

function speechRecognitionConstructor() {
  return window.SpeechRecognition || window.webkitSpeechRecognition || null;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function setCssNumber(name, value, unit = "") {
  stage.style.setProperty(name, `${value.toFixed(3)}${unit}`);
}

function scheduleBlink(now) {
  if (now < nextBlink) return;
  blinkUntil = now + 130;
  nextBlink = now + 2200 + Math.random() * 3600;
}

function updateMotion() {
  const now = performance.now();
  scheduleBlink(now);

  const listeningBoost = isListening ? 1 : 0;
  const speakingBoost = stage.dataset.mode === "speaking" ? 1 : 0;
  const thinkingBoost = stage.dataset.mode === "thinking" || stage.dataset.mode === "workflow_running" ? 1 : 0;
  const idleX = Math.sin(now / 2700) * 4;
  const idleY = Math.cos(now / 3100) * 3;
  const idleRot = Math.sin(now / 4300) * 0.42;

  targetMotion.x = clamp(targetMotion.x * 0.94 + idleX * 0.06, -18, 18);
  targetMotion.y = clamp(targetMotion.y * 0.94 + idleY * 0.06, -14, 14);
  targetMotion.rotate = clamp(targetMotion.rotate * 0.94 + idleRot * 0.06, -1.6, 1.6);
  targetMotion.scale = 1 + listeningBoost * 0.006 + thinkingBoost * 0.004 + audioLevel * 0.012;
  if (!speakingBoost && !isListening) targetMotion.mouth *= 0.9;

  currentMotion.x += (targetMotion.x - currentMotion.x) * 0.08;
  currentMotion.y += (targetMotion.y - currentMotion.y) * 0.08;
  currentMotion.rotate += (targetMotion.rotate - currentMotion.rotate) * 0.08;
  currentMotion.scale += (targetMotion.scale - currentMotion.scale) * 0.08;
  currentMotion.mouth += (targetMotion.mouth - currentMotion.mouth) * 0.16;

  const talking = Math.max(currentMotion.mouth, audioLevel);
  const mouthWave = talking ? (0.35 + Math.abs(Math.sin(now / 92)) * 0.65) * talking : 0;
  const blink = now < blinkUntil ? 1 : 0;

  setCssNumber("--rig-x", currentMotion.x, "px");
  setCssNumber("--rig-y", currentMotion.y, "px");
  setCssNumber("--rig-rotate", currentMotion.rotate, "deg");
  setCssNumber("--rig-scale", currentMotion.scale);
  setCssNumber("--mouth-open", clamp(mouthWave, 0, 1));
  setCssNumber("--blink", blink);

  requestAnimationFrame(updateMotion);
}

function handlePointerMove(event) {
  const rect = stage.getBoundingClientRect();
  const x = ((event.clientX - rect.left) / rect.width - 0.5) * 2;
  const y = ((event.clientY - rect.top) / rect.height - 0.5) * 2;
  targetMotion.x = clamp(x * 12, -18, 18);
  targetMotion.y = clamp(y * 9, -14, 14);
  targetMotion.rotate = clamp(x * 1.1, -1.6, 1.6);
}

async function enumerateDevices() {
  if (!navigator.mediaDevices?.enumerateDevices) {
    micStatus.textContent = "Mic unavailable";
    cameraStatus.textContent = "Cam unavailable";
    return;
  }
  const devices = await navigator.mediaDevices.enumerateDevices();
  const mics = devices.filter((device) => device.kind === "audioinput");
  const cameras = devices.filter((device) => device.kind === "videoinput");
  const mic = mics[0];
  const camera = cameras[0];
  micStatus.textContent = mic ? `Mic ${mic.label || "detected"}` : "Mic not found";
  cameraStatus.textContent = camera ? `Cam ${camera.label || "detected"}` : "Cam not found";
  return { mics, cameras };
}

function preferredDevice(devices, patterns) {
  return devices.find((device) => patterns.some((pattern) => pattern.test(device.label || ""))) || devices[0] || null;
}

function watchAudioLevel(stream) {
  const AudioContext = window.AudioContext || window.webkitAudioContext;
  if (!AudioContext) return;
  const audioContext = new AudioContext();
  const source = audioContext.createMediaStreamSource(stream);
  const analyser = audioContext.createAnalyser();
  analyser.fftSize = 512;
  source.connect(analyser);
  const samples = new Uint8Array(analyser.frequencyBinCount);

  function sampleAudio() {
    analyser.getByteFrequencyData(samples);
    const average = samples.reduce((sum, value) => sum + value, 0) / samples.length;
    audioLevel = clamp((average - 8) / 72, 0, 1);
    if (cameraStream) requestAnimationFrame(sampleAudio);
  }
  sampleAudio();
}

async function activateCameraAndMic() {
  if (!navigator.mediaDevices?.getUserMedia) {
    voiceNote.textContent = "Media devices unavailable";
    return;
  }
  try {
    let devices = await enumerateDevices();
    let preferredMic = preferredDevice(devices?.mics || [], [/emeet/i, /piko/i, /usb/i, /external/i]);
    let preferredCamera = preferredDevice(devices?.cameras || [], [/emeet/i, /piko/i, /usb/i, /external/i]);
    const firstStream = await navigator.mediaDevices.getUserMedia({
      audio: {
        ...(preferredMic?.deviceId ? { deviceId: { ideal: preferredMic.deviceId } } : {}),
        echoCancellation: true,
        noiseSuppression: true,
        autoGainControl: true
      },
      video: {
        ...(preferredCamera?.deviceId ? { deviceId: { ideal: preferredCamera.deviceId } } : {}),
        width: { ideal: 640 },
        height: { ideal: 360 },
        facingMode: "user"
      }
    });
    cameraStream = firstStream;
    devices = await enumerateDevices();
    preferredMic = preferredDevice(devices?.mics || [], [/emeet/i, /piko/i, /usb/i, /external/i]);
    preferredCamera = preferredDevice(devices?.cameras || [], [/emeet/i, /piko/i, /usb/i, /external/i]);
    const activeAudio = cameraStream.getAudioTracks()[0]?.label || "";
    const activeVideo = cameraStream.getVideoTracks()[0]?.label || "";
    const shouldRestartForPreferred =
      (preferredMic?.label && activeAudio && preferredMic.label !== activeAudio) ||
      (preferredCamera?.label && activeVideo && preferredCamera.label !== activeVideo);
    if (shouldRestartForPreferred) {
      cameraStream.getTracks().forEach((track) => track.stop());
      cameraStream = await navigator.mediaDevices.getUserMedia({
        audio: {
          ...(preferredMic?.deviceId ? { deviceId: { exact: preferredMic.deviceId } } : {}),
          echoCancellation: true,
          noiseSuppression: true,
          autoGainControl: true
        },
        video: {
          ...(preferredCamera?.deviceId ? { deviceId: { exact: preferredCamera.deviceId } } : {}),
          width: { ideal: 640 },
          height: { ideal: 360 },
          frameRate: { ideal: 30 }
        }
      });
    }
    watchAudioLevel(cameraStream);
    if (presenceVideo) {
      presenceVideo.srcObject = cameraStream;
      await presenceVideo.play().catch(() => {});
      watchPresence();
    }
    voiceNote.textContent = "Webcam mic active";
    if (preferredMic || preferredCamera) {
      micStatus.textContent = preferredMic ? `Mic ${preferredMic.label || "active"}` : micStatus.textContent;
      cameraStatus.textContent = preferredCamera ? `Cam ${preferredCamera.label || "active"}` : cameraStatus.textContent;
    }
    stage.dataset.expression = "bright";
  } catch (error) {
    voiceNote.textContent = `Cam/mic error: ${error.name || "blocked"}`;
  }
}

async function watchPresence() {
  if (!presenceVideo || !cameraStream) return;
  if (!faceDetector && "FaceDetector" in window) {
    try {
      faceDetector = new FaceDetector({ fastMode: true, maxDetectedFaces: 1 });
    } catch {
      faceDetector = null;
    }
  }

  if (faceDetector && presenceVideo.readyState >= 2) {
    try {
      const faces = await faceDetector.detect(presenceVideo);
      const face = faces[0]?.boundingBox;
      if (face && presenceVideo.videoWidth && presenceVideo.videoHeight) {
        const centerX = (face.x + face.width / 2) / presenceVideo.videoWidth - 0.5;
        const centerY = (face.y + face.height / 2) / presenceVideo.videoHeight - 0.5;
        targetMotion.x = clamp(centerX * -18, -16, 16);
        targetMotion.y = clamp(centerY * -12, -12, 12);
        targetMotion.rotate = clamp(centerX * -1.4, -1.4, 1.4);
      }
    } catch {
      faceDetector = null;
    }
  }

  requestAnimationFrame(watchPresence);
}

function startVoiceLoop() {
  if (isListening) return;
  const Recognition = speechRecognitionConstructor();
  if (!Recognition) {
    const fallback = window.prompt("Ask Synra");
    if (fallback) askAssistant(fallback);
    return;
  }

  isListening = true;
  listenButton.classList.add("active");
  listenButton.textContent = "Listening";
  voiceNote.textContent = "Microphone active";
  targetMotion.mouth = 0.24;

  recognition = new Recognition();
  recognition.lang = navigator.language || "en-US";
  recognition.continuous = false;
  recognition.interimResults = true;
  recognition.maxAlternatives = 1;

  let finalTranscript = "";
  let latestTranscript = "";
  let hadError = false;

  setRemoteState({
    mode: "listening",
    expression: "attentive",
    message: "I’m listening.",
    subtitle: "Microphone active",
    card: {
      title: "Voice Input",
      body: "Listening for your request...",
      detail: "Speak naturally to NodeSpark Synra",
      style: "listening"
    }
  });

  recognition.onresult = (event) => {
    latestTranscript = "";
    for (let index = event.resultIndex; index < event.results.length; index += 1) {
      const transcript = event.results[index][0].transcript;
      latestTranscript += transcript;
      if (event.results[index].isFinal) finalTranscript += transcript;
    }
    const preview = (finalTranscript || latestTranscript).trim();
    if (preview) {
      setRemoteState({
        mode: "listening",
        expression: "attentive",
        message: preview,
        subtitle: "Listening...",
        card: {
          title: "Voice Input",
          body: preview,
          detail: "Capturing speech",
          style: "listening"
        }
      });
    }
  };

  recognition.onerror = (event) => {
    hadError = true;
    isListening = false;
    listenButton.classList.remove("active");
    listenButton.textContent = "Talk";
    voiceNote.textContent = `Mic error: ${event.error || "unknown"}`;
    targetMotion.mouth = 0;
    setRemoteState({
      mode: "error",
      expression: "concerned",
      message: `Microphone error: ${event.error || "unknown"}`,
      subtitle: "Voice input",
      card: {
        title: "Voice Input Error",
        body: event.error || "The browser could not capture speech.",
        detail: "Check Chromium microphone permission",
        style: "error"
      }
    });
  };

  recognition.onend = () => {
    if (hadError) return;
    const transcript = (finalTranscript || latestTranscript).trim();
    isListening = false;
    listenButton.classList.remove("active");
    listenButton.textContent = "Talk";
    voiceNote.textContent = "Voice loop ready";
    targetMotion.mouth = 0;
    askAssistant(transcript);
  };

  recognition.start();
}

document.querySelectorAll("[data-demo]").forEach((button) => {
  button.addEventListener("click", () => sendDemo(button.dataset.demo));
});

listenButton.addEventListener("click", startVoiceLoop);
cameraButton.addEventListener("click", activateCameraAndMic);
window.addEventListener("pointermove", handlePointerMove, { passive: true });
navigator.mediaDevices?.addEventListener?.("devicechange", enumerateDevices);

enumerateDevices();
updateMotion();
fetchState();
setInterval(fetchState, 650);
