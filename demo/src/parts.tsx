import React from "react";
import { color, fonts } from "./theme";

// Very small highlighter for WarpForge source, IR and PTX. Deliberately
// quiet: keywords cool blue, comments slate, everything else steel.
const KEYWORDS = /^(kernel|int|float|if|else|return|global_id|condbr|br|ret|phi|store|load|addr|lt|neg|ld|st|mov|setp|mad|mul|add|sub|xor|cvta|bra|\.visible|\.entry|\.param|\.reg|\.version|\.target|\.address_size)\b/;

export const CodeLine: React.FC<{ text: string; size?: number; tint?: string; shadow?: string }> = ({
  text,
  size = 30,
  tint,
  shadow,
}) => {
  const comment = text.indexOf("//");
  const body = comment >= 0 ? text.slice(0, comment) : text;
  const tail = comment >= 0 ? text.slice(comment) : "";
  const parts = body.split(/(\s+|[(),\[\]{};=<+*])/).filter((p) => p !== "");
  return (
    <div
      style={{
        fontFamily: fonts.mono,
        fontSize: size,
        lineHeight: 1.45,
        whiteSpace: "pre",
        color: tint ?? color.steel,
        textShadow: shadow,
        fontVariantLigatures: "none",
      }}
    >
      {parts.map((p, i) => {
        if (tint) return <span key={i}>{p}</span>;
        const word = p.replace(/^\t+/, "");
        const isKeyword = KEYWORDS.test(word); // \b also matches dotted opcodes: add.rn.f32
        return (
          <span key={i} style={{ color: isKeyword ? color.keyword : undefined }}>
            {p}
          </span>
        );
      })}
      {tail && <span style={{ color: color.slate }}>{tail}</span>}
    </div>
  );
};
