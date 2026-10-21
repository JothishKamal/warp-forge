// The shared timeline (../timeline.json), plus derived beat lists computed
// exactly as scripts/make-audio.py computes them, so visuals hit with the drums.
import raw from "../timeline.json";

export const T = raw;

const inBreak = (f: number) => T.kickBreaks.some(([a, b]) => f >= a && f < b);

export const KICKS: number[] = [];
for (let f = T.drops[0]; f < T.scenes.outro[0]; f += T.beat) if (!inBreak(f)) KICKS.push(f);

export type SceneName = keyof typeof T.scenes;
export const span = (name: SceneName) => T.scenes[name] as [number, number];
