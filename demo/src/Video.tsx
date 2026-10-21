import React from "react";
import { AbsoluteFill, Sequence } from "remotion";
import { HeatSweep } from "./parts";
import { OptimizeScene, OutroScene, PipelineScene, PtxScene, SourceScene, TitleScene, ValidateScene } from "./scenes";
import { color } from "./theme";

// Scene lengths in frames at 30 fps. The film reads as one pass through the
// compiler: write a kernel, compile it, optimize it, emit PTX, prove it.
export const SCENES: { name: string; frames: number; Scene: React.FC }[] = [
  { name: "title", frames: 96, Scene: TitleScene },
  { name: "source", frames: 150, Scene: SourceScene },
  { name: "pipeline", frames: 186, Scene: PipelineScene },
  { name: "optimize", frames: 270, Scene: OptimizeScene },
  { name: "ptx", frames: 180, Scene: PtxScene },
  { name: "validate", frames: 186, Scene: ValidateScene },
  { name: "outro", frames: 96, Scene: OutroScene },
];

export const TOTAL_FRAMES = SCENES.reduce((n, s) => n + s.frames, 0);

// Each scene stays on screen a little past its slot, so the next scene's
// heat sweep reveals it on top of the previous one instead of over black.
const OVERLAP = 18;

export const WarpForgeDemo: React.FC = () => {
  let from = 0;
  return (
    <AbsoluteFill style={{ background: color.ink }}>
      {SCENES.map(({ name, frames, Scene }, i) => {
        const start = from;
        from += frames;
        return (
          <Sequence
            key={name}
            name={name}
            from={start}
            durationInFrames={i === SCENES.length - 1 ? frames : frames + OVERLAP}
          >
            {i === 0 ? (
              <Scene />
            ) : (
              <HeatSweep>
                <AbsoluteFill style={{ background: color.ink }}>
                  <Scene />
                </AbsoluteFill>
              </HeatSweep>
            )}
          </Sequence>
        );
      })}
    </AbsoluteFill>
  );
};
