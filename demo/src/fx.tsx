// Effects shared by every scene. All of them are pure functions of the frame
// number (Remotion renders frames out of order, in parallel), with randomness
// from remotion's seeded random().
import React from "react";
import { AbsoluteFill, random, useCurrentFrame } from "remotion";
import { color, heat } from "./theme";

// Intensity of the most recent hit at or before `frame`: 1 on the hit,
// decaying exponentially with time constant `decay` frames.
export const hitLevel = (frame: number, hits: readonly number[], decay = 10) => {
  let level = 0;
  for (const h of hits) if (frame >= h) level = Math.max(level, Math.exp(-(frame - h) / decay));
  return level;
};

// Camera shake: jitter and roll scaled by the current hit level.
export const Shake: React.FC<{ hits: readonly number[]; amp?: number; decay?: number; children: React.ReactNode }> = ({
  hits,
  amp = 26,
  decay = 9,
  children,
}) => {
  const frame = useCurrentFrame();
  const k = hitLevel(frame, hits, decay);
  const dx = (random(`sx${frame}`) - 0.5) * 2 * amp * k;
  const dy = (random(`sy${frame}`) - 0.5) * 2 * amp * k;
  const rot = (random(`sr${frame}`) - 0.5) * 1.6 * k;
  return <AbsoluteFill style={{ transform: `translate(${dx}px, ${dy}px) rotate(${rot}deg)` }}>{children}</AbsoluteFill>;
};

// Full-frame flash on hits (white-hot by default).
export const Flash: React.FC<{ hits: readonly number[]; decay?: number; tint?: string; max?: number }> = ({
  hits,
  decay = 5,
  tint = color.heatWhite,
  max = 0.9,
}) => {
  const frame = useCurrentFrame();
  const k = hitLevel(frame, hits, decay);
  if (k < 0.01) return null;
  return <AbsoluteFill style={{ background: tint, opacity: k * max, mixBlendMode: "screen" }} />;
};

// A burst of sparks from (x, y) at frame `at`: streaks with velocity, drag
// and gravity, cooling from white-hot to deep red as they die.
export const Sparks: React.FC<{
  at: number;
  x: number;
  y: number;
  count?: number;
  speed?: number;
  life?: number;
  spread?: number; // radians around `dir`
  dir?: number; // radians, 0 = right, -PI/2 = up
  gravity?: number;
  seed?: string;
  width?: number;
}> = ({ at, x, y, count = 90, speed = 38, life = 55, spread = Math.PI * 2, dir = -Math.PI / 2, gravity = 0.9, seed = "s", width = 3 }) => {
  const frame = useCurrentFrame();
  const t = frame - at;
  if (t < 0 || t > life * 1.6) return null;
  const streaks = [];
  for (let i = 0; i < count; i++) {
    const r = (k: string) => random(`${seed}-${at}-${i}-${k}`);
    const lifeI = life * (0.45 + 0.75 * r("l"));
    if (t > lifeI) continue;
    const a = dir + (r("a") - 0.5) * spread;
    const v = speed * (0.25 + 0.95 * r("v"));
    const drag = 0.92;
    // Closed-form position under per-frame drag plus gravity.
    const dist = (v * (1 - Math.pow(drag, t))) / (1 - drag);
    const px = x + Math.cos(a) * dist;
    const py = y + Math.sin(a) * dist + 0.5 * gravity * t * t * 0.35;
    const vx = Math.cos(a) * v * Math.pow(drag, t);
    const vy = Math.sin(a) * v * Math.pow(drag, t) + gravity * t * 0.35;
    const temp = 1 - t / lifeI;
    streaks.push(
      <line
        key={i}
        x1={px}
        y1={py}
        x2={px - vx * 1.4}
        y2={py - vy * 1.4}
        stroke={heat(0.25 + 0.75 * temp)}
        strokeWidth={width * (0.5 + temp)}
        strokeLinecap="round"
        opacity={Math.min(1, temp * 1.8)}
      />,
    );
  }
  return (
    <svg width={1920} height={1080} style={{ position: "absolute", inset: 0, overflow: "visible" }}>
      <g style={{ filter: "drop-shadow(0 0 6px #FF9F1C)" }}>{streaks}</g>
    </svg>
  );
};

// Red/amber channel split for big type, strongest on a hit.
export const chroma = (k: number) =>
  k < 0.02 ? "none" : `${-10 * k}px 0 ${color.heatDeep}, ${10 * k}px 0 ${color.heatAmber}, 0 0 ${30 * k}px ${heat(k)}`;

// Horizontal slice glitch: the content is drawn in bands, each shifted
// sideways by a random amount while `k` is non-zero.
export const Glitch: React.FC<{ k: number; seed: string; bands?: number; children: React.ReactNode }> = ({
  k,
  seed,
  bands = 9,
  children,
}) => {
  const frame = useCurrentFrame();
  if (k < 0.03) return <AbsoluteFill>{children}</AbsoluteFill>;
  const step = Math.floor(frame / 2); // glitches update every other frame
  return (
    <AbsoluteFill>
      {Array.from({ length: bands }).map((_, i) => {
        const top = (100 / bands) * i;
        const r = random(`${seed}-${step}-${i}`);
        const shift = r < 0.45 ? 0 : (random(`${seed}-${step}-${i}-x`) - 0.5) * 220 * k;
        return (
          <AbsoluteFill
            key={i}
            style={{
              clipPath: `inset(${top}% 0 ${100 - top - 100 / bands}% 0)`,
              transform: `translateX(${shift}px)`,
              filter: shift ? `drop-shadow(${6 * k}px 0 ${color.heatDeep})` : undefined,
            }}
          >
            {children}
          </AbsoluteFill>
        );
      })}
    </AbsoluteFill>
  );
};

// Radial heat glow that breathes with the kick drum.
export const BeatGlow: React.FC<{ kicks: readonly number[]; x?: string; y?: string; strength?: number }> = ({
  kicks,
  x = "50%",
  y = "55%",
  strength = 0.5,
}) => {
  const frame = useCurrentFrame();
  const k = hitLevel(frame, kicks, 8);
  return (
    <AbsoluteFill
      style={{
        background: `radial-gradient(ellipse 60% 55% at ${x} ${y}, rgba(179,38,30,${0.1 + strength * 0.35 * k}), rgba(14,19,34,0) 70%)`,
      }}
    />
  );
};

// Vignette and faint scanlines over everything: gives the frame depth.
export const Grade: React.FC = () => (
  <>
    <AbsoluteFill
      style={{
        background: "radial-gradient(ellipse 85% 80% at 50% 50%, rgba(0,0,0,0) 55%, rgba(0,0,0,0.55) 100%)",
        pointerEvents: "none",
      }}
    />
    <AbsoluteFill
      style={{
        backgroundImage: "repeating-linear-gradient(0deg, rgba(255,255,255,0.018) 0 1px, rgba(0,0,0,0) 1px 4px)",
        pointerEvents: "none",
      }}
    />
  </>
);
