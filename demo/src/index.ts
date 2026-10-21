import React from "react";
import { AbsoluteFill, Composition, Sequence, registerRoot } from "remotion";
import { WarpForgeDemo, TOTAL_FRAMES } from "./Video";
import { FPS, HEIGHT, WIDTH } from "./theme";

// Silent highlight reel for the README preview: three slices of the full
// film (title drop, the forge's hammer strikes, the PASS stamp), played back
// to back. A Sequence with a negative `from` shifts the film's clock, so each
// slice is frame-for-frame identical to the master.
export const TEASER_SLICES: [number, number][] = [
  [228, 420],
  [1006, 1232],
  [1904, 2064],
];
const TEASER_FRAMES = TEASER_SLICES.reduce((n, [a, b]) => n + (b - a), 0);

const Teaser: React.FC = () => {
  let at = 0;
  return React.createElement(
    AbsoluteFill,
    null,
    TEASER_SLICES.map(([a, b]) => {
      const from = at;
      at += b - a;
      return React.createElement(
        Sequence,
        { key: a, from, durationInFrames: b - a },
        React.createElement(Sequence, { from: -a }, React.createElement(WarpForgeDemo, { muted: true })),
      );
    }),
  );
};

const Root: React.FC = () =>
  React.createElement(
    React.Fragment,
    null,
    React.createElement(Composition, {
      id: "WarpForgeDemo",
      component: WarpForgeDemo,
      durationInFrames: TOTAL_FRAMES,
      fps: FPS,
      width: WIDTH,
      height: HEIGHT,
      defaultProps: { muted: false },
    }),
    React.createElement(Composition, {
      id: "WarpForgeTeaser",
      component: Teaser,
      durationInFrames: TEASER_FRAMES,
      fps: FPS,
      width: WIDTH,
      height: HEIGHT,
    }),
  );

registerRoot(Root);
