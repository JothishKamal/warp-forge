// The nine scenes. Every scene reads the absolute frame (the composition
// doesn't wrap them in Sequences), so all timing comes straight from
// timeline.json, the same numbers the soundtrack is built from.
import React from "react";
import { AbsoluteFill, random, useCurrentFrame } from "remotion";
import { content } from "./content.generated";
import { BeatGlow, chroma, Flash, Glitch, hitLevel, Shake, Sparks } from "./fx";
import { CodeLine } from "./parts";
import { codeGlow, color, easeInOut, fonts, glow, heat, tween } from "./theme";
import { KICKS, T } from "./timeline";

const W = 1920;
const H = 1080;

// Entrance for a hard cut: a quick zoom-settle and smear.
const Enter: React.FC<{ at: number; children: React.ReactNode }> = ({ at, children }) => {
  const frame = useCurrentFrame();
  const p = tween(frame, at, at + 14, [0, 1]);
  return (
    <AbsoluteFill style={{ transform: `scale(${1.12 - 0.12 * p})`, filter: p < 1 ? `blur(${(1 - p) * 8}px)` : undefined, opacity: 0.3 + 0.7 * p }}>
      {children}
    </AbsoluteFill>
  );
};

// Big kinetic word: slams in from 2.4x with a channel split.
const Slam: React.FC<{
  at: number;
  children: React.ReactNode;
  size: number;
  x?: number;
  y?: number;
  font?: string;
  weight?: number;
  rotate?: number;
  align?: "left" | "center";
  tint?: string;
}> = ({ at, children, size, x = 120, y = 120, font = fonts.display, weight = 800, rotate = 0, align = "left", tint }) => {
  const frame = useCurrentFrame();
  if (frame < at) return null;
  const p = tween(frame, at, at + 12, [0, 1]);
  const k = hitLevel(frame, [at], 9);
  return (
    <div
      style={{
        position: "absolute",
        left: align === "center" ? 0 : x,
        right: align === "center" ? 0 : undefined,
        top: y,
        textAlign: align,
        fontFamily: font,
        fontWeight: weight,
        fontSize: size,
        lineHeight: 1,
        letterSpacing: font === fonts.display ? -size * 0.03 : 0,
        color: tint ?? heat(k * 0.9),
        textShadow: chroma(k),
        transform: `scale(${2.4 - 1.4 * p}) rotate(${rotate}deg)`,
        transformOrigin: align === "center" ? "50% 50%" : "0% 50%",
        opacity: Math.min(1, p * 3),
        whiteSpace: "nowrap",
      }}
    >
      {children}
    </div>
  );
};

const Sub: React.FC<{ at: number; children: React.ReactNode; x?: number; y: number; size?: number; align?: "left" | "center" }> = ({
  at,
  children,
  x = 120,
  y,
  size = 40,
  align = "left",
}) => {
  const frame = useCurrentFrame();
  const t = tween(frame, at, at + 18, [0, 1]);
  return (
    <div
      style={{
        position: "absolute",
        left: align === "center" ? 0 : x,
        right: align === "center" ? 0 : undefined,
        textAlign: align,
        top: y,
        fontFamily: fonts.display,
        fontSize: size,
        color: color.slate,
        opacity: t,
        transform: `translateY(${(1 - t) * 20}px)`,
        whiteSpace: "nowrap",
      }}
    >
      {children}
    </div>
  );
};

// ------------------------------------------------------------- 1. intro

export const Intro: React.FC = () => {
  const frame = useCurrentFrame();
  const { start, end, text } = T.typing;
  const per = (end - start) / text.length;
  const tension = tween(frame, 196, 240, [0, 1], (x) => x * x);
  const jitter = tension * 16;
  const dx = (random(`ix${frame}`) - 0.5) * 2 * jitter;
  const dy = (random(`iy${frame}`) - 0.5) * 2 * jitter;
  const push = tween(frame, 0, 240, [1, 1.14], easeInOut);
  const size = 120;
  const charW = size * 0.6;
  const x0 = (W - text.length * charW) / 2;
  return (
    <AbsoluteFill style={{ transform: `scale(${push}) translate(${dx}px, ${dy}px)` }}>
      <div style={{ position: "absolute", left: x0, top: 470, display: "flex", fontFamily: fonts.mono, fontSize: size, fontWeight: 700 }}>
        {text.split("").map((ch, i) => {
          const at = start + i * per;
          if (frame < at) return null;
          const pop = tween(frame, at, at + 7, [1.45, 1]);
          const temp = Math.max(tween(frame, at, at + 22, [1, 0]), tension);
          return (
            <span
              key={i}
              style={{
                width: charW,
                display: "inline-block",
                textAlign: "center",
                color: heat(temp),
                textShadow: glow(temp * 0.8),
                transform: `scale(${pop})`,
              }}
            >
              {ch}
            </span>
          );
        })}
        {frame < end + 20 && frame % 30 < 18 && (
          <span style={{ width: 14, height: size * 0.9, background: color.heatAmber, alignSelf: "center", boxShadow: glow(0.6) }} />
        )}
      </div>
      <Sub at={70} y={630} align="center" size={34}>
        one line of a GPU kernel
      </Sub>
      {[206, 218, 228, 236].map((at, i) => (
        <Sparks key={at} at={at} x={x0 + (i * 0.27 + 0.1) * text.length * charW} y={540} count={14 + i * 8} speed={14} dir={Math.PI / 2} spread={1.6} life={40} seed="drip" />
      ))}
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------- 2. title

const Wordmark: React.FC<{ at: number; size: number; scatter?: boolean }> = ({ at, size, scatter = false }) => {
  const frame = useCurrentFrame();
  return (
    <div style={{ display: "flex", justifyContent: "center", fontFamily: fonts.display, fontWeight: 800, fontSize: size, letterSpacing: -size * 0.035 }}>
      {"WarpForge".split("").map((ch, i) => {
        const f = frame - at - i * 2;
        const p = tween(f, 0, 20, [0, 1]);
        const temp = tween(f, 0, 50, [1, 0]);
        const sx = scatter ? (random(`wx${i}`) - 0.5) * 1800 * (1 - p) : 0;
        const sy = scatter ? (random(`wy${i}`) - 0.5) * 1000 * (1 - p) : (1 - p) * size * 0.25;
        const rot = scatter ? (random(`wr${i}`) - 0.5) * 220 * (1 - p) : 0;
        return (
          <span
            key={i}
            style={{
              display: "inline-block",
              color: heat(temp),
              textShadow: temp > 0.02 ? `${chroma(hitLevel(frame, [at], 10))}, ${glow(temp)}` : "none",
              opacity: f >= 0 ? 1 : 0,
              transform: `translate(${sx}px, ${sy}px) rotate(${rot}deg)`,
            }}
          >
            {ch}
          </span>
        );
      })}
    </div>
  );
};

export const Title: React.FC = () => {
  const frame = useCurrentFrame();
  const drop = T.drops[0];
  const slam = tween(frame, drop, drop + 16, [0, 1]);
  const chips = ["SSA", "SCCP", "out-of-SSA", "bit-exact on GPU"];
  return (
    <Shake hits={[drop, ...KICKS.filter((k) => k > drop && k < 480).map((k) => k)]} amp={frame < drop + 30 ? 40 : 5} decay={frame < drop + 30 ? 12 : 4}>
      <BeatGlow kicks={KICKS} strength={0.9} />
      <AbsoluteFill style={{ justifyContent: "center", transform: `scale(${2.2 - 1.2 * slam})` }}>
        <div style={{ marginTop: -120 }}>
          <Wordmark at={drop} size={250} />
        </div>
      </AbsoluteFill>
      <Sub at={300} y={660} align="center" size={52}>
        C-like kernels in. NVIDIA PTX out.
      </Sub>
      <div style={{ position: "absolute", top: 770, left: 0, right: 0, display: "flex", justifyContent: "center", gap: 28 }}>
        {chips.map((c, i) => {
          const at = 360 + i * 30;
          if (frame < at) return null;
          const pop = tween(frame, at, at + 10, [1.5, 1]);
          const temp = tween(frame, at, at + 26, [1, 0]);
          return (
            <div
              key={c}
              style={{
                fontFamily: fonts.mono,
                fontSize: 32,
                padding: "12px 26px",
                borderRadius: 999,
                color: heat(temp * 0.9),
                boxShadow: `inset 0 0 0 2px ${heat(Math.max(temp, 0.3))}, ${glow(temp * 0.5)}`,
                transform: `scale(${pop})`,
              }}
            >
              {c}
            </div>
          );
        })}
      </div>
      <Sparks at={drop} x={W / 2} y={470} count={240} speed={62} life={90} gravity={0.7} seed="title" width={4} />
      <Sparks at={drop + 8} x={W / 2} y={470} count={90} speed={34} life={70} seed="title2" />
      <Flash hits={[drop]} decay={6} />
    </Shake>
  );
};

// --------------------------------------------------------- 3. lex + parse

const SRC_X = 120;
const SRC_Y = 330;
const SRC_SIZE = 28;
const CHAR_W = SRC_SIZE * 0.6;
const LINE_H = SRC_SIZE * 1.5;
const KIND_COLOR: Record<string, string> = { keyword: color.keyword, ident: color.steel, number: color.heatAmber, punct: color.slate };

type AstNode = { id: string; label: string; x: number; y: number; parent?: string };
const AST: AstNode[] = [
  { id: "k", label: "Kernel vector_add", x: 1390, y: 210 },
  { id: "p", label: "Params a b c n", x: 1110, y: 340, parent: "k" },
  { id: "b", label: "Block", x: 1650, y: 340, parent: "k" },
  { id: "d", label: "Decl int i", x: 1440, y: 470, parent: "b" },
  { id: "if", label: "If", x: 1770, y: 470, parent: "b" },
  { id: "g", label: "global_id()", x: 1440, y: 600, parent: "d" },
  { id: "lt", label: "i < n", x: 1640, y: 600, parent: "if" },
  { id: "st", label: "Store c[i]", x: 1820, y: 600, parent: "if" },
  { id: "add", label: "a[i] + b[i]", x: 1760, y: 730, parent: "st" },
];

export const Lex: React.FC = () => {
  const frame = useCurrentFrame();
  const [s0] = T.scenes.lex;
  const { start, end } = T.tokens;
  const snap = T.astSnap;
  const toks = content.tokens;
  const label = frame < snap ? "lex" : "parse";
  const labelAt = frame < snap ? s0 : snap;
  return (
    <Shake hits={[snap]} amp={22} decay={8}>
      <Enter at={s0}>
        <BeatGlow kicks={KICKS} x="70%" strength={0.6} />
        <Slam at={labelAt} size={150} font={fonts.mono} weight={700} x={120} y={90}>
          {label}
        </Slam>
        <Sub at={s0 + 10} y={250} size={34}>
          {frame < snap ? "50 tokens, straight out of the lexer" : "a typed AST, checked by sema"}
        </Sub>
        {/* the source, dimming as its tokens leave */}
        <div style={{ position: "absolute", left: SRC_X, top: SRC_Y, opacity: tween(frame, start, end, [1, 0.18]) }}>
          {content.source.map((l, i) => (
            <div key={i} style={{ height: LINE_H }}>
              <CodeLine text={l} size={SRC_SIZE} />
            </div>
          ))}
        </div>
        {/* tokens fly from the source into a stream, then collapse into the AST */}
        {toks.map((tk, i) => {
          const launch = start + ((end - start) * i) / toks.length;
          if (frame < launch) return null;
          const fly = tween(frame, launch, launch + 22, [0, 1]);
          const fromX = SRC_X + tk.col * CHAR_W;
          const fromY = SRC_Y + tk.line * LINE_H + 6;
          const toX = 1170 + (i % 7) * 102;
          const toY = 330 + Math.floor(i / 7) * 62;
          const arc = Math.sin(fly * Math.PI) * -90;
          const gone = tween(frame, snap, snap + 14, [0, 1]);
          const target = AST[i % AST.length];
          const x = fromX + (toX - fromX) * fly + (target.x - 60 - toX) * gone;
          const y = fromY + (toY - fromY) * fly + arc + (target.y - toY) * gone;
          const temp = tween(frame, launch, launch + 26, [1, 0]);
          return (
            <div
              key={i}
              style={{
                position: "absolute",
                left: x,
                top: y,
                fontFamily: fonts.mono,
                fontSize: 22,
                padding: "5px 10px",
                borderRadius: 8,
                background: color.panel,
                color: temp > 0.05 ? heat(temp) : KIND_COLOR[tk.kind],
                boxShadow: `inset 0 0 0 1px ${temp > 0.05 ? heat(temp) : color.rule}`,
                opacity: 1 - gone,
                transform: `scale(${1 - 0.6 * gone})`,
                whiteSpace: "pre",
              }}
            >
              {tk.text}
            </div>
          );
        })}
        {/* the AST */}
        {frame >= snap && (
          <svg width={W} height={H} style={{ position: "absolute", inset: 0 }}>
            {AST.filter((n) => n.parent).map((n, i) => {
              const p = AST.find((m) => m.id === n.parent)!;
              const grow = tween(frame, snap + 4 + i * 4, snap + 18 + i * 4, [0, 1]);
              const len = Math.hypot(n.x - p.x, n.y - p.y);
              return (
                <line
                  key={n.id}
                  x1={p.x}
                  y1={p.y + 26}
                  x2={n.x}
                  y2={n.y - 26}
                  stroke={heat(0.45)}
                  strokeWidth={3}
                  strokeDasharray={len}
                  strokeDashoffset={len * (1 - grow)}
                />
              );
            })}
          </svg>
        )}
        {frame >= snap &&
          AST.map((n, i) => {
            const at = snap + i * 4;
            if (frame < at) return null;
            const pop = tween(frame, at, at + 10, [1.6, 1]);
            const temp = tween(frame, at, at + 30, [1, 0]);
            return (
              <div
                key={n.id}
                style={{
                  position: "absolute",
                  left: n.x,
                  top: n.y,
                  transform: `translate(-50%, -50%) scale(${pop})`,
                  fontFamily: fonts.mono,
                  fontSize: 24,
                  padding: "10px 18px",
                  borderRadius: 10,
                  background: color.panel,
                  color: heat(temp * 0.8),
                  boxShadow: `inset 0 0 0 2px ${heat(Math.max(0.25, temp))}, ${glow(temp * 0.4)}`,
                  whiteSpace: "nowrap",
                }}
              >
                {n.label}
              </div>
            );
          })}
        <Sparks at={snap} x={1390} y={210} count={120} speed={40} life={60} seed="ast" />
        <Flash hits={[snap]} decay={4} max={0.35} />
      </Enter>
    </Shake>
  );
};

// ---------------------------------------------------------------- 4. SSA

const BOX_W = 230;
export const Ssa: React.FC = () => {
  const frame = useCurrentFrame();
  const [s0] = T.scenes.ssa;
  const [d0] = T.cfgDraw;
  const phi = T.phiPop;
  // Lay the blocks out by longest-path level, left to right.
  const byLevel: Record<number, string[]> = {};
  content.cfg.forEach((b) => (byLevel[b.level] ??= []).push(b.name));
  const pos: Record<string, { x: number; y: number }> = {};
  content.cfg.forEach((b) => {
    const col = byLevel[b.level];
    const idx = col.indexOf(b.name);
    pos[b.name] = { x: 80 + b.level * 262, y: 710 + (idx - (col.length - 1) / 2) * 250 };
  });
  const pan = tween(frame, s0, s0 + 240, [30, -30], easeInOut);
  const phiK = hitLevel(frame, [phi], 14);
  return (
    <Shake hits={[phi]} amp={14} decay={8}>
      <Enter at={s0}>
        <BeatGlow kicks={KICKS} strength={0.5} />
        <Slam at={s0} size={150} weight={800} x={120} y={90}>
          SSA
        </Slam>
        <Sub at={s0 + 12} y={250} size={34}>
          built on the fly with Braun et al.: a phi wherever values meet
        </Sub>
        <AbsoluteFill style={{ transform: `translateX(${pan}px)` }}>
          <svg width={W} height={H} style={{ position: "absolute", inset: 0 }}>
            {content.cfg.flatMap((b) =>
              b.succs.map((s) => {
                const a = pos[b.name];
                const z = pos[s];
                const at = d0 + b.level * 12 + 6;
                const grow = tween(frame, at, at + 16, [0, 1]);
                const x1 = a.x + BOX_W;
                const x2 = z.x;
                const mid = (x1 + x2) / 2;
                // Edges that skip levels arc over the top instead of cutting through blocks.
                const span = content.cfg.find((c) => c.name === s)!.level - b.level;
                const lift = span >= 3 ? 250 + span * 18 : 0;
                const d = lift
                  ? `M ${x1} ${a.y} C ${x1 + 160} ${a.y - lift}, ${x2 - 160} ${z.y - lift}, ${x2} ${z.y}`
                  : `M ${x1} ${a.y} C ${mid} ${a.y}, ${mid} ${z.y}, ${x2} ${z.y}`;
                return (
                  <path
                    key={`${b.name}-${s}`}
                    d={d}
                    fill="none"
                    stroke={heat(0.4)}
                    strokeWidth={3}
                    pathLength={1}
                    strokeDasharray={1}
                    strokeDashoffset={1 - grow}
                    opacity={0.85}
                  />
                );
              }),
            )}
          </svg>
          {content.cfg.map((b) => {
            const at = d0 + b.level * 12;
            if (frame < at) return null;
            const pop = tween(frame, at, at + 10, [1.5, 1]);
            const phis = b.insts.filter((l) => l.includes("phi"));
            const shown = phis.length ? phis : b.insts.slice(0, 1);
            const temp = phis.length ? Math.max(phiK, tween(frame, at, at + 20, [1, 0]) * 0.6) : tween(frame, at, at + 20, [0.7, 0]);
            return (
              <div
                key={b.name}
                style={{
                  position: "absolute",
                  left: pos[b.name].x,
                  top: pos[b.name].y,
                  width: BOX_W,
                  transform: `translateY(-50%) scale(${pop})`,
                  background: color.panel,
                  borderRadius: 12,
                  padding: "12px 14px",
                  boxShadow: `inset 0 0 0 2px ${heat(Math.max(0.2, temp))}, ${glow(temp * 0.5)}`,
                }}
              >
                <div style={{ fontFamily: fonts.mono, fontSize: 22, color: color.slate, marginBottom: 6 }}>{b.name}</div>
                {shown.map((l) => (
                  <div
                    key={l}
                    style={{
                      fontFamily: fonts.mono,
                      fontSize: 18,
                      color: phis.length && frame >= phi ? heat(0.55 + 0.45 * phiK) : color.steel,
                      textShadow: phis.length ? codeGlow(phiK) : undefined,
                      whiteSpace: "nowrap",
                      overflow: "hidden",
                      textOverflow: "ellipsis",
                    }}
                  >
                    {l}
                  </div>
                ))}
              </div>
            );
          })}
          {content.cfg
            .filter((b) => b.insts.some((l) => l.includes("phi")))
            .map((b) => (
              <Sparks key={b.name} at={phi} x={pos[b.name].x + BOX_W / 2} y={pos[b.name].y} count={60} speed={22} life={45} seed={b.name} />
            ))}
        </AbsoluteFill>
      </Enter>
    </Shake>
  );
};

// -------------------------------------------------------------- 5. forge

const PASS_NOTES = [
  "2.0 × 1.5 folds to 3.0; 32 × 8 < 0 folds to false",
  "condbr false becomes a plain br",
  "the dead mul and add are swept away",
  "the unreachable block is deleted, the CFG merged",
];
const IR_X = 110;
const IR_Y = 150;
const IR_LH = 27;

export const Forge: React.FC = () => {
  const frame = useCurrentFrame();
  const [s0] = T.scenes.forge;
  const hammers = T.hammers;
  // Which transition is playing (-1: showing -O0 before the first strike).
  let k = -1;
  hammers.forEach((h, i) => {
    if (frame >= h) k = i;
  });
  const done = frame >= T.collapse;
  const rows =
    k < 0
      ? content.snapshots[0].map((t) => ({ op: "keep" as const, text: t, before: t }))
      : content.transitions[k].map((r) => ({ op: r.op, text: r.op === "del" ? r.before! : r.after!, before: r.op === "keep" ? r.before : undefined }));
  const t = k < 0 ? 0 : frame - hammers[k];
  const firstDel = rows.findIndex((r) => r.op === "del");
  // y of the first burning line, for the spark source
  let acc = 0;
  for (let i = 0; i < firstDel && i >= 0; i++) acc += IR_LH;
  const sparkY = IR_Y + 34 + acc;
  const panelIn = tween(frame, s0, s0 + 14, [0, 1]);
  const counter = Math.round(tween(frame, T.collapse, T.collapse + 40, [content.ptxCountO0, content.ptxCountO1]));
  return (
    <Shake hits={[s0, ...hammers]} amp={38} decay={10}>
      <BeatGlow kicks={KICKS} x="35%" strength={1} />
      <div
        style={{
          position: "absolute",
          left: IR_X,
          top: IR_Y,
          width: 1060,
          height: 860,
          background: color.panel,
          borderRadius: 14,
          boxShadow: `inset 0 0 0 1px ${color.rule}, 0 30px 80px rgba(0,0,0,0.5)`,
          overflow: "hidden",
          transform: `scale(${1.25 - 0.25 * panelIn})`,
          opacity: panelIn,
        }}
      >
        <div style={{ position: "absolute", left: 40, top: 30 }}>
          {rows.map((r, i) => {
            let h = IR_LH;
            let temp = 0;
            let opacity = 1;
            if (r.op === "del") {
              temp = t < 22 ? 1 : tween(t, 22, 50, [1, 0.55]);
              h = IR_LH * tween(t, 46, 70, [1, 0]);
              opacity = tween(t, 50, 70, [1, 0]);
            } else if (r.op === "add") {
              h = IR_LH * tween(t, 14, 28, [0, 1]);
              temp = tween(t, 14, 60, [1, 0]);
              opacity = tween(t, 14, 24, [0, 1]);
            }
            const text = r.op === "keep" && t < 2 && r.before ? r.before : r.text;
            return (
              <div key={`${k}-${i}`} style={{ height: h, overflow: "hidden", opacity }}>
                <CodeLine text={text} size={20} tint={temp > 0.02 ? heat(temp) : undefined} shadow={temp > 0.02 ? codeGlow(temp * 0.7) : undefined} />
              </div>
            );
          })}
        </div>
      </div>
      {/* the pass checklist and the current strike */}
      <div style={{ position: "absolute", left: 1240, top: 170 }}>
        {content.passes.map((p, i) => {
          const active = i === k && !done;
          const hit = i <= k;
          return (
            <div key={p} style={{ fontFamily: fonts.mono, fontSize: 30, height: 52, color: active ? color.heatAmber : hit ? color.steel : color.slate, opacity: hit ? 1 : 0.45 }}>
              {hit ? "▸ " : "  "}
              {p}
            </div>
          );
        })}
      </div>
      {k >= 0 && !done && (
        <>
          <Slam key={`slam${k}`} at={hammers[k]} size={92} font={fonts.mono} weight={700} x={1240} y={430}>
            {content.passes[k]}
          </Slam>
          <div
            style={{
              position: "absolute",
              left: 1240,
              top: 570,
              width: 620,
              fontFamily: fonts.display,
              fontSize: 34,
              lineHeight: 1.35,
              color: color.steel,
              opacity: tween(frame, hammers[k] + 8, hammers[k] + 22, [0, 1]),
            }}
          >
            {PASS_NOTES[k]}
          </div>
        </>
      )}
      {done && (
        <div style={{ position: "absolute", left: 1240, top: 430 }}>
          <div style={{ fontFamily: fonts.display, fontSize: 36, color: color.slate }}>PTX instructions</div>
          <div style={{ fontFamily: fonts.display, fontWeight: 800, fontSize: 170, color: color.steel, textShadow: chroma(hitLevel(frame, [T.collapse], 10)) }}>
            {content.ptxCountO0}
            <span style={{ color: color.slate, fontWeight: 400 }}> → </span>
            <span style={{ color: heat(0.3 + 0.7 * hitLevel(frame, [T.collapse], 30)) }}>{counter}</span>
          </div>
          <div style={{ fontFamily: fonts.display, fontSize: 34, color: color.slate }}>fixpoint: a second round changes nothing</div>
        </div>
      )}
      {hammers.map((h, i) => (
        <Sparks key={h} at={h} x={520} y={sparkY > IR_Y ? sparkY : 400} count={170} speed={48} life={70} spread={Math.PI * 1.4} seed={`h${i}`} width={4} />
      ))}
      <Sparks at={T.collapse} x={1500} y={600} count={120} speed={40} life={60} seed="collapse" />
      <Flash hits={hammers} decay={5} tint={color.heatAmber} max={0.45} />
      <Flash hits={[s0]} decay={5} max={0.6} />
    </Shake>
  );
};

// ---------------------------------------------------------------- 6. PTX

const PTX_NOTES: [RegExp, string][] = [
  [/mad\.lo\.s32/, "global_id()"],
  [/setp\.lt\.s32/, "i < n"],
  [/add\.rn\.f32/, ".rn: no FMA contraction"],
  [/st\.global\.f32/, "c[i] = a[i] + b[i]"],
];

export const Ptx: React.FC = () => {
  const frame = useCurrentFrame();
  const [s0] = T.scenes.ptx;
  const freeze = T.ptxFreeze;
  const g = hitLevel(frame, T.glitches, 7);
  const lines = content.ptx.map((l) => l.replace(/\t/g, "  "));
  const rainOut = tween(frame, freeze, freeze + 10, [1, 0]);
  const key = lines.slice(lines.findIndex((l) => l.includes("mad.lo")), lines.findIndex((l) => l.trim() === "ret;") + 1);
  return (
    <Shake hits={[freeze, ...T.glitches]} amp={20} decay={6}>
      <Glitch k={g} seed="ptx">
        <AbsoluteFill style={{ background: color.ink }}>
          {/* data rain: the whole module streaming past in three columns */}
          {rainOut > 0 &&
            [0, 1, 2].map((c) => {
              const speed = [9, 13, 7][c];
              const y = -((frame - s0) * speed) % (lines.length * 30);
              return (
                <div key={c} style={{ position: "absolute", left: 80 + c * 620, top: 0, opacity: (0.25 + 0.2 * c) * rainOut }}>
                  {[...lines, ...lines, ...lines].map((l, i) => (
                    <div key={i} style={{ height: 30, transform: `translateY(${y}px)` }}>
                      <CodeLine text={l} size={19} />
                    </div>
                  ))}
                </div>
              );
            })}
          <Slam at={s0} size={260} font={fonts.mono} weight={700} align="center" y={380}>
            {frame < freeze ? "PTX" : ""}
          </Slam>
          {frame >= freeze && (
            <div style={{ position: "absolute", left: 120, top: 190 }}>
              <div style={{ fontFamily: fonts.display, fontWeight: 800, fontSize: 64, color: color.steel, marginBottom: 30 }}>vector_add, emitted for sm_86</div>
              {key.map((l, i) => {
                const note = PTX_NOTES.findIndex(([re]) => re.test(l));
                const noteAt = freeze + note * 30;
                const lit = note >= 0 && frame >= noteAt;
                const temp = lit ? tween(frame, noteAt, noteAt + 30, [1, 0.55]) : 0;
                const enter = tween(frame, freeze + i * 2, freeze + i * 2 + 10, [0, 1]);
                return (
                  <div key={i} style={{ height: 42, display: "flex", alignItems: "center", opacity: enter, transform: `translateX(${(1 - enter) * -60}px)` }}>
                    <div style={{ width: 900 }}>
                      <CodeLine text={l} size={28} tint={lit ? heat(temp) : undefined} shadow={lit ? codeGlow(temp * 0.6) : undefined} />
                    </div>
                    {lit && (
                      <div
                        style={{
                          fontFamily: fonts.display,
                          fontWeight: 600,
                          fontSize: 34,
                          color: color.heatAmber,
                          transform: `translateX(${tween(frame, noteAt, noteAt + 12, [60, 0])}px)`,
                          opacity: tween(frame, noteAt, noteAt + 8, [0, 1]),
                        }}
                      >
                        ← {PTX_NOTES[note][1]}
                      </div>
                    )}
                  </div>
                );
              })}
            </div>
          )}
        </AbsoluteFill>
      </Glitch>
    </Shake>
  );
};

// ---------------------------------------------------------------- 7. GPU

const COLS = 128;
const ROWS = 32;
const CELL = 12;
const GAP = 2;
const TERMINAL = [
  "device: NVIDIA GeForce RTX 3060 Laptop GPU (sm_86), CUDA driver API 13.3",
  "12 PTX files assembled with ptxas -arch=sm_86, 0 failures",
  "100% tests passed, 0 tests failed out of 177",
  "300 GPU fuzz runs (150 programs x -O0/-O1), 0 failures",
];

export const Gpu: React.FC = () => {
  const frame = useCurrentFrame();
  const [s0] = T.scenes.gpu;
  const [w0, w1] = T.gridWave;
  const stamp = T.passStamp;
  const gridW = COLS * (CELL + GAP);
  const gx = (W - gridW) / 2;
  const gy = 300;
  const progress = tween(frame, w0, w1, [0, 1], (x) => x);
  const threads = Math.round(progress * 1048576);
  const dim = tween(frame, stamp, stamp + 20, [1, 0.35]);
  const cells = [];
  for (let r = 0; r < ROWS; r++)
    for (let c = 0; c < COLS; c++) {
      // Blocks launch in a diagonal wave, with a little scheduling jitter.
      const order = (c + r * 1.6) / (COLS + ROWS * 1.6);
      const launch = w0 + (w1 - w0) * (order * 0.9 + random(`b${r}-${c}`) * 0.1);
      const tt = frame - launch;
      const fill = tt < 0 ? color.panel : tt < 26 ? heat(1 - tt / 26) : "#3A4E7A";
      cells.push(<rect key={`${r}-${c}`} x={gx + c * (CELL + GAP)} y={gy + r * (CELL + GAP)} width={CELL} height={CELL} rx={2} fill={fill} />);
    }
  return (
    <Shake hits={[s0, stamp]} amp={44} decay={11}>
      <BeatGlow kicks={KICKS} strength={0.8} y="50%" />
      <Slam at={s0} size={120} weight={800} x={gx} y={110}>
        1,048,576 threads
      </Slam>
      <svg width={W} height={H} style={{ position: "absolute", inset: 0, opacity: dim }}>
        {cells}
      </svg>
      <div style={{ position: "absolute", left: gx, top: gy + ROWS * (CELL + GAP) + 24, fontFamily: fonts.mono, fontSize: 30, color: color.slate, opacity: dim }}>
        4,096 blocks of 256 · {threads.toLocaleString("en-US")} threads run vector_add
      </div>
      {frame >= stamp && (
        <>
          <AbsoluteFill style={{ justifyContent: "center", alignItems: "center" }}>
            <div
              style={{
                fontFamily: fonts.mono,
                fontWeight: 700,
                fontSize: 300,
                color: heat(0.3 + 0.7 * hitLevel(frame, [stamp], 25)),
                textShadow: chroma(hitLevel(frame, [stamp], 10)),
                transform: `rotate(-6deg) scale(${tween(frame, stamp, stamp + 10, [3, 1])})`,
                border: `10px solid ${heat(0.4 + 0.6 * hitLevel(frame, [stamp], 25))}`,
                borderRadius: 30,
                padding: "0 60px",
                marginTop: -120,
              }}
            >
              PASS
            </div>
          </AbsoluteFill>
          <Sub at={stamp + 10} y={700} align="center" size={44}>
            bit-exact against the CPU reference
          </Sub>
        </>
      )}
      <div style={{ position: "absolute", left: gx, top: 850 }}>
        {TERMINAL.map((l, i) => {
          const at = T.terminal[i];
          if (frame < at) return null;
          const temp = tween(frame, at, at + 24, [1, 0]);
          return (
            <div key={l} style={{ opacity: tween(frame, at, at + 8, [0, 1]) }}>
              <CodeLine text={`✓ ${l}`} size={30} tint={heat(temp * 0.9)} shadow={codeGlow(temp)} />
            </div>
          );
        })}
      </div>
      <Sparks at={stamp} x={W / 2} y={420} count={260} speed={64} life={90} seed="stamp" width={4} />
      <Sparks at={s0} x={W / 2} y={170} count={140} speed={44} life={70} seed="gpu" />
      <Flash hits={[s0, stamp]} decay={6} />
    </Shake>
  );
};

// -------------------------------------------------------------- 8. stats

const STATS: [string, string][] = [
  ["SSA", "built on the fly, Braun et al."],
  ["SCCP", "Wegman–Zadeck conditional constants"],
  ["0x7FFFFFFF", "NaNs canonicalized exactly like the GPU"],
  ["177", "tests, from unit to GPU fuzz"],
  ["303 GB/s", "the same speed as nvcc"],
  ["bit-exact", "on a real RTX 3060"],
];

export const Stats: React.FC = () => {
  const frame = useCurrentFrame();
  const hits = T.statHits;
  let i = 0;
  hits.forEach((h, j) => {
    if (frame >= h) i = j;
  });
  const [big, small] = STATS[i];
  const [i0, i1] = T.implode;
  const shrink = tween(frame, i0, i1, [1, 0], (x) => x * x);
  const inverted = i === 2;
  const left = i % 2 === 0;
  return (
    <Shake hits={hits} amp={30} decay={7}>
      <AbsoluteFill style={{ background: inverted ? color.heatWhite : color.ink }} />
      {!inverted && <BeatGlow kicks={KICKS} strength={1} />}
      <AbsoluteFill style={{ transform: `scale(${shrink}) rotate(${(1 - shrink) * 40}deg)` }}>
        <Slam
          key={i}
          at={hits[i]}
          size={big.length > 6 ? 210 : 300}
          font={i === 2 ? fonts.mono : fonts.display}
          weight={800}
          align={left ? "left" : "left"}
          x={left ? 120 : 520}
          y={330}
          tint={inverted ? color.ink : undefined}
        >
          {big}
        </Slam>
        <div
          style={{
            position: "absolute",
            left: left ? 130 : 530,
            top: 660,
            fontFamily: fonts.display,
            fontSize: 50,
            color: inverted ? color.heatDeep : color.slate,
            opacity: tween(frame, hits[i] + 4, hits[i] + 14, [0, 1]),
          }}
        >
          {small}
        </div>
      </AbsoluteFill>
      {/* implosion: sparks rush into the centre */}
      {frame >= i0 &&
        Array.from({ length: 120 }).map((_, n) => {
          const a = random(`im${n}`) * Math.PI * 2;
          const r0 = 500 + random(`imr${n}`) * 700;
          const p = tween(frame, i0 + random(`imd${n}`) * 20, i1, [0, 1], (x) => x * x * x);
          const r = r0 * (1 - p);
          const x = W / 2 + Math.cos(a) * r;
          const y = H / 2 + Math.sin(a) * r;
          return (
            <div
              key={n}
              style={{
                position: "absolute",
                left: x,
                top: y,
                width: 4 + 18 * p,
                height: 3,
                background: heat(0.4 + 0.6 * p),
                boxShadow: glow(p * 0.6),
                transform: `rotate(${a}rad)`,
                opacity: 1 - p * 0.2,
              }}
            />
          );
        })}
      <Flash hits={hits} decay={4} max={0.25} />
    </Shake>
  );
};

// -------------------------------------------------------------- 9. outro

export const Outro: React.FC = () => {
  const frame = useCurrentFrame();
  const [s0, s1] = T.scenes.outro;
  const url = "github.com/JothishKamal/warp-forge";
  const typed = Math.floor(tween(frame, s0 + 40, s0 + 100, [0, url.length], (x) => x));
  const fadeOut = tween(frame, s1 - 50, s1, [1, 0], easeInOut);
  return (
    <AbsoluteFill style={{ opacity: fadeOut }}>
      <Shake hits={[s0]} amp={40} decay={12}>
        <BeatGlow kicks={[s0, s0 + 30, s0 + 60]} strength={1.2} />
        <AbsoluteFill style={{ justifyContent: "center" }}>
          <div style={{ marginTop: -100 }}>
            <Wordmark at={s0} size={230} scatter />
          </div>
        </AbsoluteFill>
        <div style={{ position: "absolute", top: 650, left: 0, right: 0, textAlign: "center", fontFamily: fonts.mono, fontSize: 46, color: color.steel }}>
          {url.slice(0, typed)}
          {typed < url.length && <span style={{ color: color.heatAmber }}>▍</span>}
        </div>
        <Sub at={s0 + 110} y={740} align="center" size={36}>
          an optimizing compiler from C-like GPU kernels to NVIDIA PTX
        </Sub>
        <Sparks at={s0} x={W / 2} y={480} count={300} speed={70} life={100} seed="outro" width={4} />
        <Sparks at={T.finalSparkle} x={W / 2} y={480} count={80} speed={18} life={70} gravity={0.2} seed="sparkle" />
        <Flash hits={[s0]} decay={7} />
      </Shake>
    </AbsoluteFill>
  );
};
