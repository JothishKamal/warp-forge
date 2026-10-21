// Turns the teaser render into an optimized GIF for the README: one palette
// for the whole clip, rectangle diffing, ordered dithering. Uses the ffmpeg
// that ships with Remotion (it has no `fps` filter, so -r drops frames).
import { execFileSync } from "node:child_process";
import { readdirSync, statSync } from "node:fs";
import { join } from "node:path";

const compositor = readdirSync("node_modules/@remotion").find((d) => d.startsWith("compositor-"));
const ffmpeg = join("node_modules", "@remotion", compositor, process.platform === "win32" ? "ffmpeg.exe" : "ffmpeg");
execFileSync(ffmpeg, [
  "-v", "error", "-y", "-i", "out/teaser.mp4",
  "-vf", "scale=720:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
  "-r", "15", "out/warpforge-teaser.gif",
], { stdio: "inherit" });
console.log(`out/warpforge-teaser.gif: ${(statSync("out/warpforge-teaser.gif").size / 1e6).toFixed(1)} MB`);
