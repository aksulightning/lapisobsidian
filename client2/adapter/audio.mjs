// Original synthesis design/output dedicated to CC0-1.0. No recordings or assets.
export function soundProfile(name) {
  if (name.includes("note_block")) {
    if (/basedrum/.test(name))
      return { seconds: 0.3, hz: 90, noise: 0.02, decay: 7 };
    if (/snare|hat/.test(name))
      return { seconds: 0.18, hz: 210, noise: 0.85, decay: 6 };
    return {
      seconds: 0.7,
      hz: name.endsWith(".bass") ? 130.81 : 523.25,
      noise: 0,
      decay: 5,
    };
  }
  if (/entity\./.test(name)) {
    const hz = /chicken/.test(name)
      ? 660
      : /pig/.test(name)
        ? 180
        : /cow/.test(name)
          ? 110
          : /sheep/.test(name)
            ? 260
            : /skeleton/.test(name)
              ? 520
              : /spider|creeper/.test(name)
                ? 85
                : 145;
    return {
      seconds: /death|blast/.test(name) ? 0.6 : 0.35,
      hz,
      noise: /spider|creeper|blast/.test(name) ? 0.6 : 0.12,
      decay: 4,
      warble: true,
    };
  }
  if (/pickup/.test(name))
    return { seconds: 0.16, hz: 880, noise: 0, decay: 4 };
  if (/bucket|water|lava/.test(name))
    return { seconds: 0.3, hz: 240, noise: 0.65, decay: 3 };
  return {
    seconds: 0.14,
    hz: /wood/.test(name)
      ? 240
      : /grass|sand|gravel/.test(name)
        ? 420
        : /glass|break/.test(name)
          ? 1600
          : /eat/.test(name)
            ? 330
            : 160,
    noise: /stone|wood/.test(name) ? 0.3 : 0.7,
    decay: 4,
  };
}
export function synthesize(name, rate, variant = 0) {
  const p = soundProfile(name),
    data = new Float32Array(Math.ceil(rate * p.seconds));
  let seed = Array.from(name).reduce(
      (n, c) => (n * 31 + c.charCodeAt(0)) >>> 0,
      7 + variant,
    ),
    phase = 0;
  for (let i = 0; i < data.length; i++) {
    seed ^= seed << 13;
    seed ^= seed >>> 17;
    seed ^= seed << 5;
    const t = i / rate,
      envelope =
        Math.min(1, t / 0.004) * Math.pow(1 - i / data.length, p.decay);
    phase +=
      (2 * Math.PI * p.hz * (1 + (p.warble ? 0.15 * Math.sin(t * 45) : 0))) /
      rate;
    data[i] =
      ((1 - p.noise) * (Math.sin(phase) + 0.2 * Math.sin(phase * 2)) * 0.5 +
        p.noise * ((seed >>> 0) / 4294967296 - 0.5)) *
      envelope;
  }
  return data;
}
