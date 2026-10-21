import { loadFont as loadArchivo } from "@remotion/google-fonts/Archivo";
import { loadFont as loadMono } from "@remotion/google-fonts/JetBrainsMono";
import { Easing, interpolate, interpolateColors } from "remotion";

const archivo = loadArchivo("normal", { weights: ["400", "600", "800"], subsets: ["latin"] });
const mono = loadMono("normal", { weights: ["400", "500", "700"], subsets: ["latin"] });

export const fonts = {
  display: archivo.fontFamily, // titles, captions, annotations
  mono: mono.fontFamily, // every line of code and terminal output
};

// Cool steel for everything at rest; heat only where code is being
// transformed. The heat ramp is the brand: code goes white-hot in the
// forge and cools into its new form.
export const color = {
  ink: "#0E1322", // background
  panel: "#1A2236", // code surface
  rule: "#2A3450", // hairlines
  steel: "#DCE1EA", // primary text
  slate: "#7E879A", // secondary text, comments
  keyword: "#9FB3D9", // cool syntax accent
  heatDeep: "#B3261E",
  heatAmber: "#FF9F1C",
  heatWhite: "#FFF4D6",
};

// 0 = cold steel, 1 = white-hot.
export const heat = (t: number) =>
  interpolateColors(Math.max(0, Math.min(1, t)), [0, 0.35, 0.7, 1], [color.steel, color.heatDeep, color.heatAmber, color.heatWhite]);

// Glow that grows with temperature; zero when cold. For display type.
export const glow = (t: number) => {
  const k = Math.max(0, Math.min(1, t));
  if (k === 0) return "none";
  return `0 0 ${6 + 18 * k}px ${heat(k)}, 0 0 ${2 + 4 * k}px ${heat(k)}`;
};

// A faint halo for code-sized text: big blurs smear small glyphs.
export const codeGlow = (t: number) => {
  const k = Math.max(0, Math.min(1, t));
  if (k < 0.05) return "none";
  return `0 0 ${3 + 5 * k}px ${heat(k)}`;
};

export const easeOut = Easing.bezier(0.16, 1, 0.3, 1); // expo-out: fast start, long settle
export const easeInOut = Easing.bezier(0.65, 0, 0.35, 1);

// Clamped interpolation with an easing, the workhorse for every animation.
export const tween = (frame: number, from: number, to: number, out: [number, number], easing = easeOut) =>
  interpolate(frame, [from, to], out, { easing, extrapolateLeft: "clamp", extrapolateRight: "clamp" });

export const FPS = 30;
export const WIDTH = 1920;
export const HEIGHT = 1080;
export const MARGIN = 120; // left edge of all content
