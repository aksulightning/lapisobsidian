// SPDX-License-Identifier: GPL-3.0-only
import type { Settings } from "./settings";
export interface InputFrame {
  forward: number;
  strafe: number;
  vertical: number;
  lookX: number;
  lookY: number;
}
export class Input {
  active = false;
  private keys = new Set<string>();
  private look = [0, 0];
  private move = [0, 0];
  private rising = false;
  private joystickId?: number;
  private lookId?: number;
  private riseId?: number;
  private last = [0, 0];
  constructor(
    private canvas: HTMLCanvasElement,
    private settings: Settings,
    menu: () => void,
  ) {
    window.addEventListener("keydown", (e) => {
      if (!this.active) return;
      if (e.code === "Escape") {
        menu();
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
        if (this.active) menu();
      }
    });
    canvas.addEventListener("click", () => {
      if (this.active && matchMedia("(pointer: fine)").matches) {
        try {
          const p = canvas.requestPointerLock();
          p?.catch(() => {});
        } catch {
          /* WebView may not support pointer lock. */
        }
      }
    });
    document.addEventListener("mousemove", (e) => {
      if (this.active && document.pointerLockElement === canvas) {
        this.look[0] += e.movementX * this.settings.mouse;
        this.look[1] += e.movementY * this.settings.mouse;
      }
    });
    const joystick = document.querySelector<HTMLElement>("#joystick")!;
    const update = (e: PointerEvent): void => {
      const r = joystick.getBoundingClientRect();
      const x = (e.clientX - r.left - r.width / 2) / 40,
        y = (e.clientY - r.top - r.height / 2) / 40;
      const length = Math.max(1, Math.hypot(x, y));
      this.move = [x / length, -y / length];
    };
    joystick.addEventListener("pointerdown", (e) => {
      if (!this.active || this.joystickId !== undefined) return;
      this.joystickId = e.pointerId;
      joystick.setPointerCapture(e.pointerId);
      update(e);
    });
    joystick.addEventListener("pointermove", (e) => {
      if (e.pointerId === this.joystickId) update(e);
    });
    const endMove = (e: PointerEvent): void => {
      if (e.pointerId === this.joystickId) {
        this.joystickId = undefined;
        this.move = [0, 0];
      }
    };
    for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
      joystick.addEventListener(name, (e) => endMove(e as PointerEvent));
    const area = document.querySelector<HTMLElement>("#look-area")!;
    area.addEventListener("pointerdown", (e) => {
      if (!this.active || this.lookId !== undefined) return;
      this.lookId = e.pointerId;
      this.last = [e.clientX, e.clientY];
      area.setPointerCapture(e.pointerId);
    });
    area.addEventListener("pointermove", (e) => {
      if (e.pointerId !== this.lookId) return;
      this.look[0] += (e.clientX - this.last[0]) * this.settings.touch;
      this.look[1] += (e.clientY - this.last[1]) * this.settings.touch;
      this.last = [e.clientX, e.clientY];
    });
    for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
      area.addEventListener(name, (e) => {
        if ((e as PointerEvent).pointerId === this.lookId)
          this.lookId = undefined;
      });
    const rise = document.querySelector<HTMLElement>("#rise")!;
    rise.addEventListener("pointerdown", (e) => {
      if (!this.active || this.riseId !== undefined) return;
      this.riseId = e.pointerId;
      this.rising = true;
      rise.setPointerCapture(e.pointerId);
    });
    for (const name of ["pointerup", "pointercancel", "lostpointercapture"])
      rise.addEventListener(name, (e) => {
        if ((e as PointerEvent).pointerId === this.riseId) {
          this.riseId = undefined;
          this.rising = false;
        }
      });
  }
  frame(): InputFrame {
    if (!this.active)
      return { forward: 0, strafe: 0, vertical: 0, lookX: 0, lookY: 0 };
    const frame = {
      forward:
        Number(this.keys.has("KeyW")) -
        Number(this.keys.has("KeyS")) +
        this.move[1],
      strafe:
        Number(this.keys.has("KeyD")) -
        Number(this.keys.has("KeyA")) +
        this.move[0],
      vertical:
        Number(this.keys.has("Space") || this.rising) -
        Number(this.keys.has("ShiftLeft") || this.keys.has("ShiftRight")),
      lookX: this.look[0],
      lookY: this.look[1],
    };
    this.look = [0, 0];
    return frame;
  }
  stop(): void {
    this.active = false;
    this.reset();
    if (document.pointerLockElement === this.canvas) document.exitPointerLock();
  }
  private reset(): void {
    this.keys.clear();
    this.move = [0, 0];
    this.look = [0, 0];
    this.rising = false;
    this.joystickId = this.lookId = this.riseId = undefined;
  }
}
