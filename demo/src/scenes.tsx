import React from "react";
import { AbsoluteFill, useCurrentFrame } from "remotion";
import { content } from "./content.generated";
import { Caption, CodeLine, Panel, StageRule } from "./parts";
import { codeGlow, color, easeInOut, fonts, glow, heat, MARGIN, tween } from "./theme";

// ---------------------------------------------------------------- title

// Letters arrive white-hot and cool to steel one after another: the forge.
export const Wordmark: React.FC<{ size: number; start?: number; stagger?: number }> = ({
  size,
  start = 0,
  stagger = 3,
}) => {
  const frame = useCurrentFrame();
  return (
    <div style={{ display: "flex", fontFamily: fonts.display, fontWeight: 800, fontSize: size, letterSpacing: -size * 0.035 }}>
      {"WarpForge".split("").map((ch, i) => {
        const f = frame - start - i * stagger;
        const temp = tween(f, 0, 34, [1, 0]);
        const rise = tween(f, 0, 22, [1, 0]);
        const shown = f >= 0 ? 1 : 0;
        return (
          <span
            key={i}
            style={{
              color: heat(temp),
              textShadow: glow(temp * 0.9),
              opacity: shown,
              transform: `translateY(${rise * size * 0.18}px)`,
              display: "inline-block",
            }}
          >
            {ch}
          </span>
        );
      })}
    </div>
  );
};

export const TitleScene: React.FC = () => {
  const frame = useCurrentFrame();
  const sub = tween(frame, 46, 70, [0, 1]);
  return (
    <AbsoluteFill style={{ justifyContent: "center", paddingLeft: MARGIN }}>
      <Wordmark size={190} start={4} />
      <div
        style={{
          marginTop: 18,
          fontFamily: fonts.display,
          fontSize: 44,
          color: color.slate,
          opacity: sub,
          transform: `translateY(${(1 - sub) * 14}px)`,
        }}
      >
        An optimizing compiler from C-like GPU kernels to NVIDIA PTX.
      </div>
    </AbsoluteFill>
  );
};

// ---------------------------------------------------------------- source

export const SourceScene: React.FC = () => {
  const frame = useCurrentFrame();
  const text = content.source.join("\n");
  const typed = Math.floor(tween(frame, 20, 110, [0, text.length], (x) => x)); // linear typing
  const visible = text.slice(0, typed).split("\n");
  const caret = frame % 30 < 18;
  return (
    <AbsoluteFill>
      <StageRule label="source" />
      <Caption>Kernels are written in a small, frozen C-like language.</Caption>
      <Panel style={{ position: "absolute", left: MARGIN, top: 250, width: 1240 }}>
        {content.source.map((_, i) => (
          <div key={i} style={{ display: "flex" }}>
            <CodeLine text={visible[i] ?? ""} size={34} />
            {i === visible.length - 1 && typed < text.length && caret && (
              <span style={{ width: 18, background: color.heatAmber, marginLeft: 2, height: 42, alignSelf: "center" }} />
            )}
          </div>
        ))}
      </Panel>
      <div
        style={{
          position: "absolute",
          left: MARGIN + 1300,
          top: 270,
          width: 400,
          fontFamily: fonts.display,
          fontSize: 28,
          lineHeight: 1.5,
          color: color.slate,
          opacity: tween(frame, 100, 125, [0, 1]),
        }}
      >
        int, float and float*. Wrapping integer overflow, IEEE floats, no implicit conversions.
      </div>
    </AbsoluteFill>
  );
};

// -------------------------------------------------------------- pipeline

const STAGES: [string, string][] = [
  ["Lexer", "tokens"],
  ["Parser", "AST"],
  ["Sema", "types, scopes"],
  ["IRGen", "SSA, Braun et al."],
  ["Passes", "fold, SCCP, DCE, CFG"],
  ["Out of SSA", "parallel copies"],
  ["PTX", "sm_86"],
];

export const PipelineScene: React.FC = () => {
  const frame = useCurrentFrame();
  const x0 = MARGIN + 20;
  const span = 1640;
  const step = span / (STAGES.length - 1);
  // The heat pulse along the track. Ease-in-out so every stage gets a fair
  // share of screen time; stage heat below is derived from this same value.
  const travel = tween(frame, 24, 130, [0, 1], easeInOut);
  const pulseX = x0 + travel * span;
  return (
    <AbsoluteFill>
      <StageRule label="pipeline" />
      <Caption>Every kernel goes through a real compiler pipeline.</Caption>
      {/* track */}
      <div style={{ position: "absolute", left: x0, top: 560, width: span, height: 2, background: color.rule }} />
      <div
        style={{
          position: "absolute",
          left: x0,
          top: 559,
          width: travel * span,
          height: 4,
          background: `linear-gradient(90deg, ${color.rule}, ${color.heatDeep} 70%, ${color.heatAmber})`,
          borderRadius: 2,
        }}
      />
      <div
        style={{
          position: "absolute",
          left: pulseX - 9,
          top: 552,
          width: 18,
          height: 18,
          borderRadius: 9,
          background: color.heatWhite,
          boxShadow: glow(1),
          opacity: travel > 0 && travel < 1 ? 1 : tween(frame, 130, 150, [1, 0]),
        }}
      />
      {STAGES.map(([name, detail], i) => {
        const x = x0 + i * step;
        // A stage glows as the pulse reaches it and cools as the pulse moves on;
        // the last one cools after the pulse arrives.
        const pos = i / (STAGES.length - 1);
        const reached = travel >= pos - 1e-6;
        const behind = travel - pos;
        const cooling = i === STAGES.length - 1 ? tween(frame, 132, 162, [1, 0]) : 1;
        const temp = reached ? Math.max(0, 1 - behind * 4.5) * cooling : 0;
        const appear = tween(frame, 8 + i * 3, 30 + i * 3, [0, 1]);
        return (
          <React.Fragment key={name}>
            <div
              style={{
                position: "absolute",
                left: x - 7,
                top: 554,
                width: 14,
                height: 14,
                borderRadius: 7,
                background: reached ? heat(Math.max(temp, 0.12)) : color.panel,
                boxShadow: `inset 0 0 0 2px ${reached ? heat(Math.max(temp, 0.3)) : color.rule}`,
                opacity: appear,
              }}
            />
            <div
              style={{
                position: "absolute",
                left: x,
                top: 470,
                transform: "translateX(-50%)",
                whiteSpace: "nowrap",
                fontFamily: fonts.display,
                fontWeight: 600,
                fontSize: 38,
                color: reached ? heat(temp) : color.slate,
                textShadow: glow(temp * 0.7),
                opacity: appear,
              }}
            >
              {name}
            </div>
            <div
              style={{
                position: "absolute",
                left: x,
                top: 604,
                transform: "translateX(-50%)",
                whiteSpace: "nowrap",
                fontFamily: fonts.mono,
                fontSize: 22,
                color: color.slate,
                opacity: appear * (reached ? 1 : 0.5),
              }}
            >
              {detail}
            </div>
          </React.Fragment>
        );
      })}
      <div
        style={{
          position: "absolute",
          left: MARGIN,
          top: 790,
          fontFamily: fonts.display,
          fontSize: 30,
          color: color.slate,
          opacity: tween(frame, 135, 160, [0, 1]),
          maxWidth: 1500,
          lineHeight: 1.5,
        }}
      >
        A verifier checks the IR after every stage and every pass: types, phis, and that each definition dominates its uses.
      </div>
    </AbsoluteFill>
  );
};

// -------------------------------------------------------------- optimize

const PASSES = ["const-fold", "sccp", "dce", "simplify-cfg"];
const PASS_START = 40; // first pass begins
const PASS_LEN = 34; // frames per pass

export const OptimizeScene: React.FC = () => {
  const frame = useCurrentFrame();
  const dying = content.irO0.map((l, i) => ({ ...l, i })).filter((l) => !l.kept);
  const collapseAt = PASS_START + PASSES.length * PASS_LEN + 6;
  const collapse = tween(frame, collapseAt, collapseAt + 26, [0, 1]);
  const toO1 = tween(frame, collapseAt + 24, collapseAt + 40, [0, 1]);
  const lineH = 26;
  const passIndex = Math.min(PASSES.length - 1, Math.floor((frame - PASS_START) / PASS_LEN));
  const counter = Math.round(tween(frame, collapseAt, collapseAt + 30, [content.ptxCountO0, content.ptxCountO1]));

  return (
    <AbsoluteFill>
      <StageRule label="optimize" />
      <Caption>-O1 runs four passes until nothing changes.</Caption>
      <Panel style={{ position: "absolute", left: MARGIN, top: 200, width: 1120, height: 820, overflow: "hidden" }}>
        {/* -O0 IR, with the lines the passes rewrite or delete heating up and collapsing */}
        <div style={{ opacity: 1 - toO1, position: "absolute", top: 30, left: 44 }}>
          {content.irO0.map((l, i) => {
            const k = dying.findIndex((d) => d.i === i);
            const owner = k < 0 ? -1 : Math.floor((k * PASSES.length) / dying.length); // spread across the passes
            const hotStart = PASS_START + owner * PASS_LEN + (k % 3) * 5;
            // Flash white-hot when the pass reaches the line, then settle to amber.
            const temp =
              k < 0
                ? 0
                : frame < hotStart + 12
                  ? tween(frame, hotStart, hotStart + 12, [0, 1])
                  : tween(frame, hotStart + 12, hotStart + 32, [1, 0.62], easeInOut);
            const h = k < 0 ? lineH : lineH * (1 - collapse);
            return (
              <div key={i} style={{ height: h, overflow: "hidden", opacity: k < 0 ? 1 : 1 - collapse }}>
                <CodeLine
                  text={l.text}
                  size={19}
                  tint={k >= 0 && temp > 0 ? heat(temp) : undefined}
                  shadow={k >= 0 ? codeGlow(temp * 0.6) : undefined}
                />
              </div>
            );
          })}
        </div>
        {/* the real -O1 output */}
        <div style={{ opacity: toO1, position: "absolute", top: 30, left: 44 }}>
          {content.irO1.map((t, i) => (
            <div key={i} style={{ height: lineH }}>
              <CodeLine text={t} size={19} />
            </div>
          ))}
        </div>
      </Panel>

      {/* pass ticker */}
      <div style={{ position: "absolute", left: MARGIN + 1180, top: 230, fontFamily: fonts.mono }}>
        {PASSES.map((p, i) => {
          const start = PASS_START + i * PASS_LEN;
          const active = frame >= start && passIndex === i && frame < collapseAt;
          const done = frame >= start + PASS_LEN || frame >= collapseAt;
          const t = tween(frame, start, start + 10, [0, 1]);
          return (
            <div key={p} style={{ display: "flex", alignItems: "baseline", gap: 18, height: 64, opacity: 0.35 + 0.65 * t }}>
              <span
                style={{
                  fontSize: 34,
                  color: active ? color.heatAmber : color.steel,
                  textShadow: active ? glow(0.4) : "none",
                  width: 260,
                }}
              >
                {p}
              </span>
              <span style={{ fontSize: 22, color: color.slate, opacity: done ? 1 : 0 }}>changed</span>
            </div>
          );
        })}
        <div style={{ height: 64, fontSize: 22, color: color.slate, opacity: tween(frame, collapseAt - 8, collapseAt + 4, [0, 1]) }}>
          second round: unchanged, fixpoint
        </div>
        <div style={{ marginTop: 70, fontFamily: fonts.display, color: color.slate, fontSize: 28, opacity: tween(frame, collapseAt, collapseAt + 12, [0, 1]) }}>
          PTX instructions
        </div>
        <div
          style={{
            fontFamily: fonts.display,
            fontWeight: 800,
            fontSize: 96,
            color: color.steel,
            opacity: tween(frame, collapseAt, collapseAt + 12, [0, 1]),
          }}
        >
          {content.ptxCountO0} <span style={{ color: color.slate, fontWeight: 400 }}>to</span> {counter}
        </div>
      </div>
    </AbsoluteFill>
  );
};

// ------------------------------------------------------------------- PTX

const NOTES: [RegExp, string][] = [
  [/mad\.lo\.s32/, "global_id()"],
  [/setp\.lt\.s32/, "i < n"],
  [/@!%p1 bra/, "bounds check"],
  [/add\.rn\.f32/, ".rn: no FMA contraction"],
  [/st\.global\.f32/, "c[i] = ..."],
];

export const PtxScene: React.FC = () => {
  const frame = useCurrentFrame();
  const lines = content.ptx;
  const lineH = 31;
  const scroll = tween(frame, 30, 150, [0, Math.max(0, lines.length * lineH - 700)], (x) => x * x * (3 - 2 * x));
  return (
    <AbsoluteFill>
      <StageRule label="codegen" />
      <Caption>Out of SSA and into PTX, with the semantics pinned down.</Caption>
      <Panel style={{ position: "absolute", left: MARGIN, top: 200, width: 1240, height: 820, overflow: "hidden" }}>
        <div style={{ transform: `translateY(${-scroll}px)` }}>
          {content.ptxHeader.map((t, i) => (
            <div key={`h${i}`} style={{ height: lineH }}>
              <CodeLine text={t} size={22} />
            </div>
          ))}
          <div style={{ height: lineH }} />
          {lines.map((t, i) => {
            const note = NOTES.find(([re]) => re.test(t));
            // Annotated lines warm to a crisp amber once they've scrolled into view.
            const y = (content.ptxHeader.length + 1 + i) * lineH - scroll;
            const warm = note ? Math.max(0, Math.min(1, (780 - y) / 120)) : 0;
            return (
              <div key={i} style={{ height: lineH, display: "flex", alignItems: "center" }}>
                <CodeLine
                  text={t.replace(/\t/g, "    ")}
                  size={22}
                  tint={note ? heat(0.7 * warm) : undefined}
                  shadow={note ? codeGlow(0.3 * warm) : undefined}
                />
              </div>
            );
          })}
        </div>
      </Panel>
      <div style={{ position: "absolute", left: MARGIN + 1300, top: 230, width: 420 }}>
        {NOTES.map(([, label], i) => (
          <div
            key={label}
            style={{
              fontFamily: fonts.display,
              fontSize: 30,
              color: color.steel,
              marginBottom: 26,
              opacity: tween(frame, 40 + i * 18, 60 + i * 18, [0, 1]),
            }}
          >
            {label}
          </div>
        ))}
        <div style={{ fontFamily: fonts.display, fontSize: 24, color: color.slate, lineHeight: 1.5, marginTop: 40, opacity: tween(frame, 130, 150, [0, 1]) }}>
          Float negation is a sign-bit xor: PTX leaves neg.f32 of a NaN unspecified.
        </div>
      </div>
    </AbsoluteFill>
  );
};

// -------------------------------------------------------------- validate

const TERMINAL: { cmd?: string; out?: string; pass?: boolean }[] = [
  { cmd: "ctest --test-dir build" },
  { out: "100% tests passed, 0 tests failed out of 177", pass: true },
  { cmd: "python tests/run_ptxas.py tests/golden" },
  { out: "12 PTX files assembled with ptxas -arch=sm_86, 0 failures", pass: true },
  { cmd: "wf-run --source examples/vector_add.wf --ptx vector_add.ptx --kernel vector_add" },
  { out: "device: NVIDIA GeForce RTX 3060 Laptop GPU (sm_86), CUDA driver API 13.3" },
  { out: "PASS vector_add n=1048576 threads=1048576 (bit-exact vs IR interpreter and C++ loop)", pass: true },
  { cmd: "python tests/run_gpu_fuzz.py ..." },
  { out: "300 GPU fuzz runs (150 programs x -O0/-O1), 0 failures", pass: true },
];

export const ValidateScene: React.FC = () => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill>
      <StageRule label="validate" />
      <Caption>Checked by ptxas and bit-for-bit on a real GPU.</Caption>
      <Panel style={{ position: "absolute", left: MARGIN, top: 220, width: 1680, padding: "40px 52px" }}>
        {TERMINAL.map((l, i) => {
          const at = 14 + i * 14;
          const t = tween(frame, at, at + 10, [0, 1]);
          const temp = l.pass ? tween(frame, at, at + 30, [1, 0]) : 0;
          return (
            <div key={i} style={{ opacity: t, marginBottom: l.out && TERMINAL[i + 1]?.cmd ? 26 : 6 }}>
              {l.cmd ? (
                <CodeLine text={`$ ${l.cmd}`} size={27} tint={color.slate} />
              ) : (
                <CodeLine text={l.out ?? ""} size={27} tint={l.pass ? heat(temp) : color.steel} shadow={l.pass ? codeGlow(temp) : undefined} />
              )}
            </div>
          );
        })}
      </Panel>
    </AbsoluteFill>
  );
};

// ----------------------------------------------------------------- outro

export const OutroScene: React.FC = () => {
  const frame = useCurrentFrame();
  const t = tween(frame, 30, 50, [0, 1]);
  return (
    <AbsoluteFill style={{ justifyContent: "center", paddingLeft: MARGIN }}>
      <Wordmark size={150} start={2} stagger={2} />
      <div style={{ marginTop: 26, fontFamily: fonts.mono, fontSize: 34, color: color.slate, opacity: t }}>
        github.com/JothishKamal/warp-forge
      </div>
    </AbsoluteFill>
  );
};
