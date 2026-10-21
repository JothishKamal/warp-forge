import React from "react";
import { AbsoluteFill, Html5Audio, staticFile, useCurrentFrame } from "remotion";
import { Grade } from "./fx";
import { Forge, Gpu, Intro, Lex, Outro, Ptx, Ssa, Stats, Title } from "./scenes";
import { color } from "./theme";
import { span, T, type SceneName } from "./timeline";

// Hard cuts on the beat; each scene owns its slice of timeline.json.
const SCENES: [SceneName, React.FC][] = [
  ["intro", Intro],
  ["title", Title],
  ["lex", Lex],
  ["ssa", Ssa],
  ["forge", Forge],
  ["ptx", Ptx],
  ["gpu", Gpu],
  ["stats", Stats],
  ["outro", Outro],
];

export const TOTAL_FRAMES = T.total;

export const WarpForgeDemo: React.FC<{ muted?: boolean }> = ({ muted = false }) => {
  const frame = useCurrentFrame();
  const current = SCENES.find(([name]) => {
    const [a, b] = span(name);
    return frame >= a && frame < b;
  });
  const Scene = current?.[1];
  return (
    <AbsoluteFill style={{ background: color.ink, overflow: "hidden" }}>
      {Scene && <Scene />}
      <Grade />
      {!muted && <Html5Audio src={staticFile("soundtrack.wav")} />}
    </AbsoluteFill>
  );
};
