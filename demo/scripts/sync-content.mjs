// Pulls every piece of code shown in the video straight from the repo, so the
// demo can never drift from what the compiler actually produces. Re-run after
// regenerating golden files:  npm run sync
//
// If a built `warpforge` is found (WARPFORGE env var, or ../build/*/), the
// IR after each pass comes from a real --print-after-all run; otherwise the
// snapshots already in content.generated.ts are kept.
import { spawnSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const read = (p) => readFileSync(join(root, p), "utf8").replace(/\r\n/g, "\n");
const lines = (text) => text.replace(/\n+$/, "").split("\n");
const normalize = (l) => l.replace(/%\d+/g, "%_").trim();

// ------------------------------------------------------------------ source
const sourceText = lines(read("examples/vector_add.wf"))
  .filter((l) => !l.startsWith("//"))
  .join("\n");
const source = sourceText.split("\n");

// Tokens, mirroring src/frontend/lexer.cpp's categories, with 0-based line/col.
const KW = new Set(["kernel", "int", "float", "if", "else", "return", "global_id"]);
const tokens = [];
source.forEach((text, line) => {
  const re = /[A-Za-z_][A-Za-z0-9_]*|\d+(?:\.\d+)?|[(){}\[\],;*+\-<=]/g;
  for (const m of text.matchAll(re)) {
    const t = m[0];
    const kind = KW.has(t) ? "keyword" : /^[A-Za-z_]/.test(t) ? "ident" : /^\d/.test(t) ? "number" : "punct";
    tokens.push({ text: t, kind, line, col: m.index });
  }
});

// ------------------------------------------------------- forge (saxpy_const)
const o0 = lines(read("tests/golden/saxpy_const.ir")).filter((l) => l.trim() !== "");
const o1 = lines(read("tests/golden/saxpy_const.opt.ir")).filter((l) => l.trim() !== "");

// The IR after each of the four passes, from a real --print-after-all run.
const PASSES = ["const-fold", "sccp", "dce", "simplify-cfg"];
function findCompiler() {
  const candidates = [process.env.WARPFORGE, ...["mingw-gcc", "ci", "release", "mingw-clang"].flatMap((p) => [
    join(root, "build", p, "warpforge.exe"),
    join(root, "build", p, "warpforge"),
  ]), join(root, "build", "msvc", "Debug", "warpforge.exe")].filter(Boolean);
  return candidates.find((c) => existsSync(c));
}

let snapshots; // [O0, after const-fold, after sccp, after dce, after simplify-cfg]
const compiler = findCompiler();
if (compiler) {
  // One round of each pass, logging the IR after every pass (on stderr).
  const run = spawnSync(
    compiler,
    [join(root, "examples", "saxpy_const.wf"), "--passes=" + PASSES.join(","), "--print-after-all", "--dump-ir"],
    { encoding: "utf8" },
  );
  if (run.status !== 0) throw new Error(`warpforge failed:\n${run.stderr}`);
  const after = run.stderr
    .replace(/\r\n/g, "\n") // stderr is in text mode on Windows
    .split(/^; \*\*\* IR after .*$/m)
    .slice(1)
    .map((s) => lines(s).filter((l) => l.trim() !== ""));
  snapshots = [o0, ...after];
} else {
  const prev = readFileSync(join(root, "demo", "src", "content.generated.ts"), "utf8");
  snapshots = JSON.parse(prev.slice(prev.indexOf("{"), prev.lastIndexOf("}") + 1)).snapshots;
}
if (snapshots.at(-1).join("\n") !== o1.join("\n"))
  throw new Error("one round of the passes should already reach the -O1 golden output");

// Line diff (LCS, comparing lines with the printer's %N numbering ignored).
// Each transition lists the rows of the *before* and *after* IR, interleaved:
// "keep" rows survive (shown with their new spelling), "del" rows burn away,
// "add" rows are forged in.
function diff(a, b) {
  const na = a.map(normalize), nb = b.map(normalize);
  const L = Array.from({ length: a.length + 1 }, () => new Array(b.length + 1).fill(0));
  for (let i = a.length - 1; i >= 0; i--)
    for (let j = b.length - 1; j >= 0; j--)
      L[i][j] = na[i] === nb[j] ? L[i + 1][j + 1] + 1 : Math.max(L[i + 1][j], L[i][j + 1]);
  const rows = [];
  let i = 0, j = 0;
  while (i < a.length || j < b.length) {
    if (i < a.length && j < b.length && na[i] === nb[j]) rows.push({ op: "keep", before: a[i++], after: b[j++] });
    else if (j < b.length && (i === a.length || L[i][j + 1] >= L[i + 1][j])) rows.push({ op: "add", after: b[j++] });
    else rows.push({ op: "del", before: a[i++] });
  }
  return rows;
}
const transitions = PASSES.map((_, k) => diff(snapshots[k], snapshots[k + 1]));

const countPtx = (text) =>
  lines(text).filter((l) => /^\s+[a-z@].*;$/.test(l) && !l.trim().startsWith(".")).length;

// ------------------------------------------------------------ CFG (branchy)
const branchy = lines(read("tests/golden/branchy.opt.ir"));
const blocks = [];
for (const l of branchy) {
  const label = l.match(/^([A-Za-z0-9_.]+):$/);
  if (label) blocks.push({ name: label[1], insts: [], succs: [] });
  else if (blocks.length && l.startsWith("  ")) {
    const inst = l.trim();
    const b = blocks[blocks.length - 1];
    b.insts.push(inst);
    const br = inst.match(/^br (\S+)$/);
    const cbr = inst.match(/^condbr [^,]+, (\S+), (\S+)$/);
    if (br) b.succs.push(br[1]);
    if (cbr) b.succs.push(cbr[1], cbr[2]);
  }
}
// Level = longest path from the entry (the CFG is acyclic).
const level = { [blocks[0].name]: 0 };
for (let changed = true; changed; ) {
  changed = false;
  for (const b of blocks)
    for (const s of b.succs)
      if (level[b.name] !== undefined && (level[s] ?? -1) < level[b.name] + 1) {
        level[s] = level[b.name] + 1;
        changed = true;
      }
}
const cfg = blocks.map((b) => ({ ...b, level: level[b.name] }));

// --------------------------------------------------------------------- PTX
const ptxAll = lines(read("tests/golden/vector_add.ptx"));
const entry = ptxAll.findIndex((l) => l.startsWith(".visible .entry"));

const content = {
  source,
  tokens,
  passes: PASSES,
  snapshots,
  transitions,
  cfg,
  ptx: ptxAll.slice(entry).filter((l) => l.trim() !== ""),
  ptxCountO0: countPtx(read("tests/golden/saxpy_const.O0.ptx")),
  ptxCountO1: countPtx(read("tests/golden/saxpy_const.ptx")),
};

writeFileSync(
  join(root, "demo", "src", "content.generated.ts"),
  "// Generated by scripts/sync-content.mjs from the repo's examples and golden files. Do not edit.\n" +
    `export const content = ${JSON.stringify(content, null, 2)} as const;\n`,
);
const perPass = transitions
  .map((t, i) => `${PASSES[i]} -${t.filter((r) => r.op === "del").length}/+${t.filter((r) => r.op === "add").length}`)
  .join(", ");
console.log(
  `synced: ${tokens.length} tokens, ${cfg.length} CFG blocks, IR ${o0.length} -> ${o1.length} lines (${perPass})` +
    `${compiler ? "" : " [pass snapshots reused: no compiler found]"}, PTX ${content.ptxCountO0} -> ${content.ptxCountO1}`,
);
