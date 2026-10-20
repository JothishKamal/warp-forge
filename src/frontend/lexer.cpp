// Lexer: a hand-written, single-pass scanner.
//
// Design notes:
//  * Every token carries a 1-based line:col so diagnostics can point at it.
//  * Integer literals are range-checked here (they must fit in a signed
//    32-bit int) so later stages can store them as int32_t without UB.
//  * Common C operators that WarpForge does not support (`/`, `>`, `==`, ...)
//    become `Unsupported` tokens rather than lexical errors; the parser then
//    emits a targeted "operator '/' is not supported" message.

#include "frontend/lexer.hpp"

#include <cctype>
#include <ostream>
#include <string_view>

namespace wf {

const char* tokenKindName(TokenKind kind) {
  switch (kind) {
  case TokenKind::Ident: return "Ident";
  case TokenKind::IntLit: return "IntLit";
  case TokenKind::FloatLit: return "FloatLit";
  case TokenKind::KwKernel: return "KwKernel";
  case TokenKind::KwInt: return "KwInt";
  case TokenKind::KwFloat: return "KwFloat";
  case TokenKind::KwIf: return "KwIf";
  case TokenKind::KwElse: return "KwElse";
  case TokenKind::KwReturn: return "KwReturn";
  case TokenKind::KwGlobalId: return "KwGlobalId";
  case TokenKind::LParen: return "LParen";
  case TokenKind::RParen: return "RParen";
  case TokenKind::LBrace: return "LBrace";
  case TokenKind::RBrace: return "RBrace";
  case TokenKind::LBracket: return "LBracket";
  case TokenKind::RBracket: return "RBracket";
  case TokenKind::Comma: return "Comma";
  case TokenKind::Semi: return "Semi";
  case TokenKind::Star: return "Star";
  case TokenKind::Plus: return "Plus";
  case TokenKind::Minus: return "Minus";
  case TokenKind::Less: return "Less";
  case TokenKind::Assign: return "Assign";
  case TokenKind::Unsupported: return "Unsupported";
  case TokenKind::Eof: return "Eof";
  }
  return "?";
}

void dumpTokens(const std::vector<Token>& tokens, std::ostream& out) {
  for (const Token& t : tokens)
    out << t.loc.line << ':' << t.loc.col << ' ' << tokenKindName(t.kind) << " '" << t.text
        << "'\n";
}

namespace {

class Lexer {
public:
  Lexer(const SourceFile& file, DiagnosticEngine& diags)
      : src_(file.text()), diags_(diags) {}

  std::vector<Token> run() {
    std::vector<Token> tokens;
    for (;;) {
      skipTrivia();
      SourceLoc loc = here();
      if (atEnd()) {
        tokens.push_back({TokenKind::Eof, "", loc});
        return tokens;
      }
      size_t start = pos_;
      TokenKind kind = scanToken();
      if (kind == TokenKind::Eof) continue; // a bad character was reported; skip it
      tokens.push_back({kind, std::string(src_.substr(start, pos_ - start)), loc});
    }
  }

private:
  bool atEnd() const { return pos_ >= src_.size(); }
  char peek(size_t ahead = 0) const {
    return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
  }
  SourceLoc here() const { return {line_, col_}; }

  void advance() {
    if (src_[pos_] == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    ++pos_;
  }

  void skipTrivia() {
    while (!atEnd()) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        advance();
      } else if (c == '/' && peek(1) == '/') {
        while (!atEnd() && peek() != '\n') advance();
      } else if (c == '/' && peek(1) == '*') {
        SourceLoc start = here();
        advance();
        advance();
        while (!atEnd() && !(peek() == '*' && peek(1) == '/')) advance();
        if (atEnd()) {
          diags_.error(start, "unterminated block comment");
          return;
        }
        advance();
        advance();
      } else {
        return;
      }
    }
  }

  static bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
  }
  static bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  }
  static bool isDigit(char c) { return c >= '0' && c <= '9'; }

  // Returns Eof (as a sentinel) after reporting an unexpected character.
  TokenKind scanToken() {
    char c = peek();
    if (isIdentStart(c)) return scanIdentOrKeyword();
    if (isDigit(c)) return scanNumber();

    // Two-character unsupported operators first, so ">=" isn't read as ">".
    static constexpr std::string_view kUnsupported2[] = {">=", "<=", "==", "!=", "&&",
                                                         "||", "++", "--", "+=", "-=",
                                                         "*=", "/=", "<<", ">>"};
    for (std::string_view op : kUnsupported2) {
      if (src_.substr(pos_, 2) == op) {
        advance();
        advance();
        return TokenKind::Unsupported;
      }
    }

    advance();
    switch (c) {
    case '(': return TokenKind::LParen;
    case ')': return TokenKind::RParen;
    case '{': return TokenKind::LBrace;
    case '}': return TokenKind::RBrace;
    case '[': return TokenKind::LBracket;
    case ']': return TokenKind::RBracket;
    case ',': return TokenKind::Comma;
    case ';': return TokenKind::Semi;
    case '*': return TokenKind::Star;
    case '+': return TokenKind::Plus;
    case '-': return TokenKind::Minus;
    case '<': return TokenKind::Less;
    case '=': return TokenKind::Assign;
    case '/': case '>': case '%': case '!': case '&': case '|': case '^': case '~':
    case '?': case ':': case '.':
      return TokenKind::Unsupported;
    default: {
      SourceLoc loc{line_, col_ - 1};
      diags_.error(loc, std::string("unexpected character '") + c + "'");
      return TokenKind::Eof;
    }
    }
  }

  TokenKind scanIdentOrKeyword() {
    size_t start = pos_;
    while (!atEnd() && isIdentChar(peek())) advance();
    std::string_view word = src_.substr(start, pos_ - start);
    if (word == "kernel") return TokenKind::KwKernel;
    if (word == "int") return TokenKind::KwInt;
    if (word == "float") return TokenKind::KwFloat;
    if (word == "if") return TokenKind::KwIf;
    if (word == "else") return TokenKind::KwElse;
    if (word == "return") return TokenKind::KwReturn;
    if (word == "global_id") return TokenKind::KwGlobalId;
    return TokenKind::Ident;
  }

  // INT_LIT   = digits
  // FLOAT_LIT = digits "." digits [exponent] ["f"] | digits exponent ["f"]
  TokenKind scanNumber() {
    SourceLoc loc = here();
    size_t start = pos_;
    while (isDigit(peek())) advance();
    bool isFloat = false;
    if (peek() == '.' && isDigit(peek(1))) {
      isFloat = true;
      advance();
      while (isDigit(peek())) advance();
    }
    if ((peek() == 'e' || peek() == 'E') &&
        (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
      isFloat = true;
      advance();
      if (peek() == '+' || peek() == '-') advance();
      while (isDigit(peek())) advance();
    }
    if (isFloat) {
      if (peek() == 'f' || peek() == 'F') advance();
      return TokenKind::FloatLit;
    }
    std::string_view digits = src_.substr(start, pos_ - start);
    // Compare as strings to avoid overflow: strip leading zeros, then length/lexical.
    size_t nz = digits.find_first_not_of('0');
    std::string_view sig = nz == std::string_view::npos ? std::string_view("0") : digits.substr(nz);
    if (sig.size() > 10 || (sig.size() == 10 && sig > "2147483647"))
      diags_.error(loc, "integer literal '" + std::string(digits) + "' is out of range for int");
    return TokenKind::IntLit;
  }

  std::string_view src_;
  DiagnosticEngine& diags_;
  size_t pos_ = 0;
  uint32_t line_ = 1;
  uint32_t col_ = 1;
};

} // namespace

std::vector<Token> lex(const SourceFile& file, DiagnosticEngine& diags) {
  return Lexer(file, diags).run();
}

} // namespace wf
