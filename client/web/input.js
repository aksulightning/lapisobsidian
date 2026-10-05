// SPDX-License-Identifier: GPL-3.0-only
// Input produces intentions; it never sends packets or moves the player.
export class Input {
  constructor(canvas, settings, action) {
    this.canvas = canvas;
    this.settings = settings;
    this.action = action;
    this.active = false;
    this.keys = new Set();
    this.pointers = new Map();
    this.reset();
    window.addEventListener("keydown", (e) => {
      if (e.code === "Escape") {
        action("menu");
        return;
      }
      if (e.target.matches("input,textarea,select")) return;
      if (!this.active) return;
      const binding = {
        KeyT: "chat",
        Enter: "chat",
        KeyE: "inventory",
        KeyF: "fullscreen",
        KeyQ: "drop",
      }[e.code];
      if (binding && !e.repeat) {
        e.preventDefault();
        action(binding);
        return;
      }
      if (/^Digit[1-9]$/.test(e.code)) {
        action("slot", Number(e.code.slice(-1)) - 1);
        return;
      }
      if (
        [
          "KeyW",
          "KeyA",
          "KeyS",
          "KeyD",
          "Space",
          "ShiftLeft",
          "ShiftRight",
          "ControlLeft",
        ].includes(e.code)
      ) {
        e.preventDefault();
        this.keys.add(e.code);
      }
    });
    window.addEventListener("keyup", (e) => this.keys.delete(e.code));
    window.addEventListener("blur", () => this.reset());
    document.addEventListener("visibilitychange", () => {
      if (document.hidden) this.reset();
    });
    document.addEventListener("pointerlockchange", () => {
      if (!document.pointerLockElement) {
        this.reset();
        if (this.active && !matchMedia("(pointer:coarse)").matches)
          action("menu");
      }
    });
    document.addEventListener("mousemove", (e) => {
      if (this.active && document.pointerLockElement === canvas) {
        this.lookX += e.movementX * settings.mouse;
        this.lookY += e.movementY * settings.mouse;
      }
    });
    canvas.addEventListener("pointerdown", (e) => {
      if (!this.active || e.pointerType === "touch") return;
      if (document.pointerLockElement !== canvas) {
        this.lock();
        return;
      }
      if (e.button === 0) this.primary = true;
      if (e.button === 2) this.secondary = true;
    });
    window.addEventListener("pointerup", (e) => {
      if (e.pointerType !== "touch") {
        if (e.button === 0) this.primary = false;
        if (e.button === 2) this.secondary = false;
      }
    });
    canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    canvas.addEventListener(
      "wheel",
      (e) => {
        if (this.active) {
          e.preventDefault();
          action("wheel", Math.sign(e.deltaY));
        }
      },
      { passive: false },
    );
    const bind = (id, start, move, end) => {
      const el = document.querySelector(id);
      el.addEventListener("pointerdown", (e) => {
        if (!this.active || this.pointers.has(id)) return;
        e.preventDefault();
        this.pointers.set(id, e.pointerId);
        el.setPointerCapture(e.pointerId);
        start(e, el);
      });
      el.addEventListener("pointermove", (e) => {
        if (this.pointers.get(id) === e.pointerId) move?.(e, el);
      });
      for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
        el.addEventListener(name, (e) => {
          if (this.pointers.get(id) === e.pointerId) {
            this.pointers.delete(id);
            end();
          }
        });
    };
    const joystick = (e, el) => {
      const r = el.getBoundingClientRect(),
        x = (e.clientX - r.left - r.width / 2) / (r.width * 0.35),
        y = (e.clientY - r.top - r.height / 2) / (r.height * 0.35),
        n = Math.max(1, Math.hypot(x, y));
      this.strafe = x / n;
      this.forward = -y / n;
    };
    bind("#joystick", joystick, joystick, () => {
      this.strafe = this.forward = 0;
    });
    let lastX = 0,
      lastY = 0;
    bind(
      "#look-area",
      (e) => {
        lastX = e.clientX;
        lastY = e.clientY;
      },
      (e) => {
        this.lookX += (e.clientX - lastX) * settings.touch;
        this.lookY += (e.clientY - lastY) * settings.touch;
        lastX = e.clientX;
        lastY = e.clientY;
      },
      () => {},
    );
    for (const [id, field] of [
      ["#jump", "jump"],
      ["#primary", "primary"],
      ["#secondary", "secondary"],
      ["#sneak", "sneak"],
    ])
      bind(
        id,
        () => (this[field] = true),
        null,
        () => (this[field] = false),
      );
  }
  lock() {
    try {
      this.canvas
        .requestPointerLock()
        ?.catch(() =>
          this.action(
            "notice",
            "Pointer lock is unavailable. Touch controls remain available in Settings.",
          ),
        );
    } catch {
      this.action("notice", "Pointer lock is unavailable in this browser.");
    }
  }
  reset() {
    this.keys.clear();
    this.pointers.clear();
    this.forward = this.strafe = this.lookX = this.lookY = 0;
    this.jump = this.sneak = this.primary = this.secondary = false;
  }
  stop() {
    this.active = false;
    this.reset();
    if (document.pointerLockElement) document.exitPointerLock();
  }
  frame() {
    const f = this.active
      ? {
          forward:
            this.forward +
            Number(this.keys.has("KeyW")) -
            Number(this.keys.has("KeyS")),
          strafe:
            this.strafe +
            Number(this.keys.has("KeyD")) -
            Number(this.keys.has("KeyA")),
          jump: this.jump || this.keys.has("Space"),
          sneak:
            this.sneak ||
            this.keys.has("ShiftLeft") ||
            this.keys.has("ShiftRight"),
          primary: this.primary,
          secondary: this.secondary,
          lookX: this.lookX,
          lookY: this.lookY,
        }
      : {
          forward: 0,
          strafe: 0,
          jump: false,
          sneak: false,
          primary: false,
          secondary: false,
          lookX: 0,
          lookY: 0,
        };
    this.lookX = this.lookY = 0;
    return f;
  }
}
