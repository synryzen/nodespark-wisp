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

document.querySelectorAll("[data-demo]").forEach((button) => {
  button.addEventListener("click", () => sendDemo(button.dataset.demo));
});

fetchState();
setInterval(fetchState, 650);

