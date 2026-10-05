// SPDX-License-Identifier: GPL-3.0-only
export type Settings = {
  quality: "low" | "medium" | "high";
  fps: number;
  mouse: number;
  touch: number;
  scale: number;
  distance: number;
  host: string;
  port: number;
  username: string;
};
export const touchDevice =
  matchMedia("(pointer: coarse)").matches || navigator.maxTouchPoints > 0;
const defaults: Settings = {
  quality: touchDevice ? "low" : "medium",
  fps: touchDevice ? 30 : 60,
  mouse: 1,
  touch: 1,
  scale: 1,
  distance: touchDevice ? 2 : 4,
  host: "",
  port: 25565,
  username: "Explorer",
};
let persist: ((settings: Settings) => void) | undefined;
export function setPersistence(callback: (settings: Settings) => void): void {
  persist = callback;
}
const key = "lapis-obsidian-client.settings.v1";
export function validHost(host: string): boolean {
  if (!host || host.length > 253 || !/^[\x21-\x7e]+$/.test(host)) return false;
  if (host.includes(":")) {
    try {
      return new URL(`http://[${host}]/`).hostname === `[${host}]`;
    } catch {
      return false;
    }
  }
  return host
    .split(".")
    .every(
      (part) =>
        part.length <= 63 &&
        /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/.test(part),
    );
}
export function validateSettings(value: unknown): Settings {
  if (!value || typeof value !== "object" || Array.isArray(value))
    return { ...defaults };
  const stored = value as Partial<Settings>,
    s = { ...defaults };
  if (["low", "medium", "high"].includes(String(stored.quality)))
    s.quality = stored.quality!;
  for (const field of ["mouse", "touch", "scale"] as const) {
    const number = stored[field],
      [min, max] = { mouse: [0.2, 3], touch: [0.2, 3], scale: [0.85, 1.2] }[
        field
      ];
    if (
      typeof number === "number" &&
      Number.isFinite(number) &&
      number >= min &&
      number <= max
    )
      s[field] = number;
  }
  if ([30, 60, 120].includes(stored.fps!)) s.fps = stored.fps!;
  if ([2, 4, 8].includes(stored.distance!)) s.distance = stored.distance!;
  if (
    Number.isInteger(stored.port) &&
    stored.port! >= 1 &&
    stored.port! <= 65535
  )
    s.port = stored.port!;
  if (
    typeof stored.host === "string" &&
    (stored.host === "" || validHost(stored.host))
  )
    s.host = stored.host;
  if (
    typeof stored.username === "string" &&
    /^[A-Za-z0-9_]{1,15}$/.test(stored.username)
  )
    s.username = stored.username;
  return s;
}
export function loadSettings(): Settings {
  try {
    return validateSettings(JSON.parse(localStorage.getItem(key) ?? "{}"));
  } catch {
    return { ...defaults };
  }
}
export function saveSettings(settings: Settings): boolean {
  try {
    persist?.(settings);
    localStorage.setItem(key, JSON.stringify(settings));
    return true;
  } catch {
    return false;
  }
}
