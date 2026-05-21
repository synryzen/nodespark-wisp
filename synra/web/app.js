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
const voiceNote = document.getElementById("voiceNote");

let lastSpeechId = "";
let recognition = null;
let isListening = false;

const demoText = {
  listening: "I’m listening. Tell me what you want NodeSparkHub to do.",
  thinking: "Give me a second. I’m tracing the best workflow path.",
  speaking: "NodeSparkHub is online. I’m ready to help.",
  success: "Done. That workflow landed cleanly."
};

async function fetchState() {
  try {
    const response = await fetch("/api/state", { cache: "no-store" });
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
    askAssistant(transcript);
  };

  recognition.start();
}

document.querySelectorAll("[data-demo]").forEach((button) => {
  button.addEventListener("click", () => sendDemo(button.dataset.demo));
});

listenButton.addEventListener("click", startVoiceLoop);

fetchState();
setInterval(fetchState, 650);
