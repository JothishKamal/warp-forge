import React from "react";
import { Composition, registerRoot } from "remotion";
import { WarpForgeDemo, TOTAL_FRAMES } from "./Video";
import { FPS, HEIGHT, WIDTH } from "./theme";

const Root: React.FC = () =>
  React.createElement(Composition, {
    id: "WarpForgeDemo",
    component: WarpForgeDemo,
    durationInFrames: TOTAL_FRAMES,
    fps: FPS,
    width: WIDTH,
    height: HEIGHT,
  });

registerRoot(Root);
