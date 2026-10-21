import { Config } from "@remotion/cli/config";

// Lossless frames, so text stays crisp until the final encode. Codec-specific
// options (CRF, pixel format) live in the package.json render scripts, since
// the GIF encoder rejects them.
Config.setVideoImageFormat("png");
