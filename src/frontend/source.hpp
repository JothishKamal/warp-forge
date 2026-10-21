#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wf {

// 1-based line and column (columns count bytes, like GCC/Clang).
struct SourceLoc {
  uint32_t line = 0;
  uint32_t col = 0;
};

class SourceFile {
public:
  SourceFile(std::string path, std::string text);

  const std::string& path() const { return path_; }
  std::string_view text() const { return text_; }
  // Text of a 1-based line without its newline; empty if out of range.
  std::string_view lineText(uint32_t line) const;

private:
  std::string path_;
  std::string text_;
  std::vector<size_t> lineStarts_;
};

} // namespace wf
