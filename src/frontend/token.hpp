#pragma once

#include "frontend/source.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace wf {

enum class TokenKind {
  Ident,
  IntLit,
  FloatLit,
  KwKernel,
  KwInt,
  KwFloat,
  KwIf,
  KwElse,
  KwReturn,
  KwGlobalId,
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Comma,
  Semi,
  Star,
  Plus,
  Minus,
  Less,
  Assign,
  // A recognizable C operator WarpForge deliberately does not support
  // (`/`, `>`, `==`, ...). Lexed as a token so the parser can say
  // "operator '/' is not supported" instead of "unexpected character".
  Unsupported,
  Eof,
};

struct Token {
  TokenKind kind = TokenKind::Eof;
  std::string text;
  SourceLoc loc;
};

const char* tokenKindName(TokenKind kind);
void dumpTokens(const std::vector<Token>& tokens, std::ostream& out);

} // namespace wf
