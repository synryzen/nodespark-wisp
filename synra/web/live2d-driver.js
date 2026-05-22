const SYNRA_MODEL_URL = "/assets/live2d/synra/synra.model3.json";
const SYNRA_VENDOR_FILES = [
  "/assets/vendor/live2d/live2dcubismcore.min.js",
  "/assets/vendor/live2d/pixi.min.js",
  "/assets/vendor/live2d/pixi-live2d-display.min.js"
];

const expressionMap = {
  attentive: "attentive",
  bright: "happy",
  concerned: "concerned",
  focused: "focused",
  raised_brow: "curious",
  soft_smile: "neutral",
  wink: "wink"
};

const modeMotionMap = {
  approval_needed: "approval",
  error: "concerned",
  idle: "idle",
  listening: "listen",
  speaking: "talk",
  success: "success",
  thinking: "think",
  warning: "concerned",
  workflow_running: "think"
};

function live2dSetStatus(status, label) {
  document.documentElement.dataset.live2d = status;
  window.dispatchEvent(new CustomEvent("synra:live2d-status", { detail: { status, label } }));
}

async function urlExists(url) {
  try {
    const response = await fetch(url, { cache: "no-store" });
    return response.ok;
  } catch {
    return false;
  }
}

function loadScript(src) {
  return new Promise((resolve, reject) => {
    const existing = document.querySelector(`script[src="${src}"]`);
    if (existing) {
      resolve();
      return;
    }

    const script = document.createElement("script");
    script.src = src;
    script.async = false;
    script.onload = resolve;
    script.onerror = () => reject(new Error(`Unable to load ${src}`));
    document.head.appendChild(script);
  });
}

function setCubismParameter(model, id, value, blend = 1) {
  const core = model?.internalModel?.coreModel;
  if (!core) return;
  if (typeof core.setParameterValueById === "function") {
    core.setParameterValueById(id, value, blend);
  } else if (typeof core.addParameterValueById === "function") {
    core.addParameterValueById(id, value, blend);
  }
}

class SynraLive2DController {
  constructor() {
    this.app = null;
    this.canvas = document.getElementById("synraLive2DCanvas");
    this.container = document.getElementById("live2dLayer");
    this.expression = "";
    this.model = null;
    this.mode = "idle";
    this.mouth = 0;
    this.ready = false;
  }

  async boot() {
    if (!this.canvas || !this.container) return;

    live2dSetStatus("checking", "Checking Live2D model");
    const hasModel = await urlExists(SYNRA_MODEL_URL);
    if (!hasModel) {
      live2dSetStatus("missing-model", "Drop Synra model pack into assets/live2d/synra");
      return;
    }

    const vendorState = await Promise.all(SYNRA_VENDOR_FILES.map((file) => urlExists(file)));
    if (vendorState.some((exists) => !exists)) {
      live2dSetStatus("missing-runtime", "Install the Live2D browser runtime vendor files");
      return;
    }

    try {
      live2dSetStatus("loading", "Loading Synra Live2D");
      for (const file of SYNRA_VENDOR_FILES) {
        await loadScript(file);
      }

      const PIXI = window.PIXI;
      const Live2DModel = PIXI?.live2d?.Live2DModel;
      if (!PIXI || !Live2DModel) throw new Error("PIXI Live2D runtime did not register");

      this.app = new PIXI.Application({
        antialias: true,
        autoDensity: true,
        backgroundAlpha: 0,
        resizeTo: this.container,
        view: this.canvas
      });
      this.model = await Live2DModel.from(SYNRA_MODEL_URL, { autoInteract: false });
      this.model.anchor?.set?.(0.5, 0.56);
      this.app.stage.addChild(this.model);
      this.resize();
      window.addEventListener("resize", () => this.resize(), { passive: true });
      this.ready = true;
      live2dSetStatus("ready", "Synra Live2D online");
      this.playMotion("idle");
      this.setExpression("soft_smile");
    } catch (error) {
      console.error(error);
      live2dSetStatus("error", "Live2D failed to load");
    }
  }

  resize() {
    if (!this.model || !this.container) return;
    const bounds = this.container.getBoundingClientRect();
    const modelWidth = this.model.width || 1;
    const modelHeight = this.model.height || 1;
    const scale = Math.min(bounds.width / modelWidth, bounds.height / modelHeight) * 1.08;
    this.model.scale.set(scale);
    this.model.x = bounds.width * 0.5;
    this.model.y = bounds.height * 0.56;
  }

  setState(state = {}) {
    const nextMode = state.mode || "idle";
    const nextExpression = state.expression || "soft_smile";
    if (nextMode !== this.mode) {
      this.mode = nextMode;
      this.playMotion(modeMotionMap[nextMode] || "idle");
    }
    if (nextExpression !== this.expression) {
      this.setExpression(nextExpression);
    }
  }

  setExpression(expression) {
    this.expression = expression;
    if (!this.ready || !this.model) return;
    const name = expressionMap[expression] || expression || "neutral";
    try {
      if (typeof this.model.expression === "function") {
        this.model.expression(name);
      }
    } catch {
      // Expression names come from the exported Synra model pack, so missing
      // optional expressions should not break the assistant.
    }
  }

  playMotion(group) {
    if (!this.ready || !this.model || !group) return;
    try {
      if (typeof this.model.motion === "function") {
        this.model.motion(group, 0, 2);
      }
    } catch {
      // Motion groups are optional while the model is being authored.
    }
  }

  setSpeaking(active) {
    this.mouth = active ? 1 : 0;
    if (active) this.playMotion("talk");
  }

  update(motion = {}) {
    if (!this.ready || !this.model) return;
    const mouth = Math.max(this.mouth, motion.mouth || 0);
    setCubismParameter(this.model, "ParamAngleX", (motion.x || 0) * 1.45);
    setCubismParameter(this.model, "ParamAngleY", (motion.y || 0) * -1.2);
    setCubismParameter(this.model, "ParamAngleZ", (motion.rotate || 0) * 6);
    setCubismParameter(this.model, "ParamBodyAngleX", (motion.x || 0) * 0.45);
    setCubismParameter(this.model, "ParamEyeBallX", (motion.x || 0) / 18);
    setCubismParameter(this.model, "ParamEyeBallY", (motion.y || 0) / -14);
    setCubismParameter(this.model, "ParamMouthOpenY", mouth);
    setCubismParameter(this.model, "ParamBreath", 0.45 + Math.sin(performance.now() / 900) * 0.2);
  }
}

window.synraLive2D = new SynraLive2DController();
window.addEventListener("DOMContentLoaded", () => window.synraLive2D.boot());
