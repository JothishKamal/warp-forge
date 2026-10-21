import React from "react";
import { AbsoluteFill, useCurrentFrame } from "remotion";
import { color, fonts, glow, heat, MARGIN, tween } from "./theme";

// Very small highlighter for WarpForge source, IR and PTX. Deliberately
// quiet: keywords cool blue, comments slate, everything else steel.
const KEYWORDS = /^(kernel|int|float|if|else|return|global_id|condbr|br|ret|phi|store|load|addr|lt|neg|ld|st|mov|setp|mad|mul|add|sub|xor|cvta|bra|\.visible|\.entry|\.param|\.reg|\.version|\.target|\.address_size)\b/;

export const CodeLine: React.FC<{ text: string; size?: number; tint?: string; shadow?: string }> = ({
  text,
  size = 30,
  tint,
  shadow,
}) => {
  const comment = text.indexOf("//");
  const body = comment >= 0 ? text.slice(0, comment) : text;
  const tail = comment >= 0 ? text.slice(comment) : "";
  const parts = body.split(/(\s+|[(),\[\]{};=<+*])/).filter((p) => p !== "");
  return (
    <div
      style={{
        fontFamily: fonts.mono,
        fontSize: size,
        lineHeight: 1.45,
        whiteSpace: "pre",
        color: tint ?? color.steel,
        textShadow: shadow,
        fontVariantLigatures: "none",
      }}
    >
      {parts.map((p, i) => {
        if (tint) return <span key={i}>{p}</span>;
        const word = p.replace(/^\t+/, "");
        const isKeyword = KEYWORDS.test(word); // \b also matches dotted opcodes: add.rn.f32
        return (
          <span key={i} style={{ color: isKeyword ? color.keyword : undefined }}>
            {p}
          </span>
        );
      })}
      {tail && <span style={{ color: color.slate }}>{tail}</span>}
    </div>
  );
};

// One caption line, top-left. It slides in once and stays put.
export const Caption: React.FC<{ children: React.ReactNode; delay?: number }> = ({ children, delay = 6 }) => {
  const frame = useCurrentFrame();
  const t = tween(frame, delay, delay + 22, [0, 1]);
  return (
    <div
      style={{
        position: "absolute",
        left: MARGIN,
        top: 96,
        fontFamily: fonts.display,
        fontWeight: 600,
        fontSize: 52,
        letterSpacing: -0.5,
        color: color.steel,
        opacity: t,
        transform: `translateX(${(1 - t) * -24}px)`,
        maxWidth: 1500,
      }}
    >
      {children}
    </div>
  );
};

// The thin vertical rule on the left that names where we are in the
// pipeline. It glows briefly when the stage changes.
export const StageRule: React.FC<{ label: string }> = ({ label }) => {
  const frame = useCurrentFrame();
  const hot = tween(frame, 0, 30, [1, 0]);
  const grow = tween(frame, 0, 26, [0, 1]);
  return (
    <>
      <div
        style={{
          position: "absolute",
          left: 64,
          top: 100,
          width: 3,
          height: 880 * grow,
          background: heat(hot * 0.8),
          boxShadow: glow(hot * 0.6),
          borderRadius: 2,
        }}
      />
      <div
        style={{
          position: "absolute",
          left: 46,
          bottom: 100,
          transform: "rotate(-90deg)",
          transformOrigin: "left bottom",
          fontFamily: fonts.mono,
          fontSize: 20,
          letterSpacing: 2,
          color: color.slate,
          opacity: grow,
        }}
      >
        {label}
      </div>
    </>
  );
};

// Scene entrance: a white-hot horizontal line sweeps down the frame and the
// scene is revealed above it. The only transition in the film.
export const HeatSweep: React.FC<{ children: React.ReactNode; duration?: number }> = ({ children, duration = 16 }) => {
  const frame = useCurrentFrame();
  const p = tween(frame, 0, duration, [0, 1]);
  const lineY = p * 1080;
  const fade = tween(frame, duration - 4, duration + 10, [1, 0]);
  return (
    <AbsoluteFill>
      <AbsoluteFill style={{ clipPath: `inset(0 0 ${100 - p * 100}% 0)` }}>{children}</AbsoluteFill>
      {fade > 0 && (
        <div
          style={{
            position: "absolute",
            left: 0,
            right: 0,
            top: lineY - 1,
            height: 2,
            background: color.heatWhite,
            boxShadow: `0 0 24px 4px ${color.heatAmber}, 0 0 80px 10px ${color.heatDeep}`,
            opacity: fade,
          }}
        />
      )}
    </AbsoluteFill>
  );
};

export const Panel: React.FC<{ children: React.ReactNode; style?: React.CSSProperties }> = ({ children, style }) => (
  <div
    style={{
      background: color.panel,
      borderRadius: 14,
      padding: "34px 44px",
      boxShadow: `inset 0 0 0 1px ${color.rule}`,
      ...style,
    }}
  >
    {children}
  </div>
);
