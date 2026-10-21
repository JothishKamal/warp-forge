#include "frontend/source.hpp"

namespace wf {

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)) {
  lineStarts_.push_back(0);
  for (size_t i = 0; i < text_.size(); ++i)
    if (text_[i] == '\n') lineStarts_.push_back(i + 1);
}

std::string_view SourceFile::lineText(uint32_t line) const {
  if (line == 0 || line > lineStarts_.size()) return {};
  size_t begin = lineStarts_[line - 1];
  if (begin >= text_.size()) return {};
  size_t end = text_.find('\n', begin);
  if (end == std::string::npos) end = text_.size();
  if (end > begin && text_[end - 1] == '\r') --end; // tolerate CRLF sources
  return std::string_view(text_).substr(begin, end - begin);
}

} // namespace wf
