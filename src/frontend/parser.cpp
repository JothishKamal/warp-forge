// Recursive-descent parser.
//
// One function per grammar rule (see docs/language.md). Precedence, lowest
// to highest:
//     compare  :=  additive [ "<" additive ]      (non-associative)
//     additive :=  term { ("+" | "-") term }
//     term     :=  unary { "*" unary }
//     unary    :=  "-" unary | primary
//
// `<` is accepted in any expression position and Sema rejects it outside an
// `if` condition. That gives a precise message ("comparison result can only
// be used as an if condition") instead of a vague syntax error.
//
// Error handling: parse functions return nullptr after reporting. The
// statement loop then calls skipStatement() to resynchronize at the next `;`
// or closing `}`, so one run reports several independent mistakes.

#include "frontend/parser.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace wf {

namespace {

// Identifiers that are C keywords WarpForge deliberately rejects. Seeing one
// in statement position gives "'while' is not supported in WarpForge".
constexpr std::array<std::string_view, 14> kUnsupportedKeywords = {
    "while", "for",   "do",     "switch", "break",  "continue", "goto",
    "void",  "double", "char",  "long",   "unsigned", "struct", "bool"};

bool isUnsupportedKeyword(std::string_view word) {
  for (std::string_view kw : kUnsupportedKeywords)
    if (word == kw) return true;
  return false;
}

class Parser {
public:
  Parser(const std::vector<Token>& tokens, DiagnosticEngine& diags)
      : toks_(tokens), diags_(diags) {}

  std::unique_ptr<Program> run() {
    auto program = std::make_unique<Program>();
    while (!at(TokenKind::Eof) && !diags_.tooMany()) {
      if (at(TokenKind::KwKernel)) {
        if (auto k = parseKernel()) program->kernels.push_back(std::move(k));
      } else {
        error(peek(), "expected 'kernel' at top level, found " + describe(peek()));
        while (!at(TokenKind::Eof) && !at(TokenKind::KwKernel)) next();
      }
    }
    return program;
  }

private:
  // ------------------------------------------------------------ token stream

  const Token& peek(size_t ahead = 0) const {
    size_t i = pos_ + ahead;
    return i < toks_.size() ? toks_[i] : toks_.back();
  }
  bool at(TokenKind k) const { return peek().kind == k; }
  const Token& next() {
    const Token& t = peek();
    if (pos_ < toks_.size() - 1) ++pos_;
    return t;
  }
  bool accept(TokenKind k) {
    if (!at(k)) return false;
    next();
    return true;
  }

  static std::string describe(const Token& t) {
    if (t.kind == TokenKind::Eof) return "end of file";
    return "'" + t.text + "'";
  }

  void error(const Token& at, std::string message) { diags_.error(at.loc, std::move(message)); }

  // Reports "expected X <context>, found Y" and returns false on mismatch.
  bool expect(TokenKind k, const char* spelling, const char* context) {
    if (accept(k)) return true;
    if (at(TokenKind::Unsupported)) {
      unsupportedOperator(peek());
      return false;
    }
    error(peek(), std::string("expected '") + spelling + "' " + context + ", found " +
                      describe(peek()));
    return false;
  }

  void unsupportedOperator(const Token& t) {
    error(t, "operator '" + t.text + "' is not supported in WarpForge");
  }

  // Skip to just after the next `;` at nesting depth 0, or to (not past) a `}`
  // that would close the enclosing block. Balanced `{ }` and `( )` are skipped
  // whole, so `while (...) { ... }` is consumed as one unit.
  void skipStatement() {
    int braces = 0;
    int parens = 0;
    while (!at(TokenKind::Eof)) {
      TokenKind k = peek().kind;
      if (k == TokenKind::LParen) ++parens;
      if (k == TokenKind::RParen && parens > 0) --parens;
      if (k == TokenKind::LBrace) ++braces;
      if (k == TokenKind::RBrace) {
        if (braces == 0) return;
        if (--braces == 0 && parens == 0) {
          next();
          return;
        }
      }
      if (k == TokenKind::Semi && braces == 0 && parens == 0) {
        next();
        return;
      }
      next();
    }
  }

  // ------------------------------------------------------------------ types

  // type := "int" | "float" | "float" "*"
  // Returns Error (after reporting) for anything else.
  TypeKind parseType() {
    const Token& t = peek();
    if (t.kind == TokenKind::KwInt || t.kind == TokenKind::KwFloat) {
      next();
      int stars = 0;
      while (accept(TokenKind::Star)) ++stars;
      if (t.kind == TokenKind::KwFloat && stars == 0) return TypeKind::Float;
      if (t.kind == TokenKind::KwFloat && stars == 1) return TypeKind::FloatPtr;
      if (t.kind == TokenKind::KwInt && stars == 0) return TypeKind::Int;
      error(t, "unsupported type '" + t.text + std::string(static_cast<size_t>(stars), '*') +
                   "' (supported: int, float, float*)");
      return TypeKind::Error;
    }
    if (t.kind == TokenKind::Ident && isUnsupportedKeyword(t.text)) {
      next();
      while (accept(TokenKind::Star)) {
      }
      error(t, "unsupported type '" + t.text + "' (supported: int, float, float*)");
      return TypeKind::Error;
    }
    error(t, "expected a type, found " + describe(t));
    return TypeKind::Error;
  }

  // ---------------------------------------------------------------- kernels

  std::unique_ptr<Kernel> parseKernel() {
    next(); // 'kernel'
    auto kernel = std::make_unique<Kernel>();
    const Token& name = peek();
    if (!expect(TokenKind::Ident, "<name>", "after 'kernel'")) {
      while (!at(TokenKind::Eof) && !at(TokenKind::KwKernel)) next();
      return nullptr;
    }
    kernel->name = name.text;
    kernel->loc = name.loc;
    kernel_ = kernel.get();

    bool ok = expect(TokenKind::LParen, "(", "after kernel name");
    if (ok && !at(TokenKind::RParen)) {
      do {
        TypeKind type = parseType();
        const Token& paramName = peek();
        if (type == TypeKind::Error || !expect(TokenKind::Ident, "<name>", "for parameter")) {
          ok = false;
          break;
        }
        VarDecl* d = kernel->newDecl(paramName.text, type, paramName.loc);
        d->isParam = true;
        d->paramIndex = static_cast<unsigned>(kernel->params.size());
        kernel->params.push_back(d);
      } while (accept(TokenKind::Comma));
    }
    if (ok) ok = expect(TokenKind::RParen, ")", "after parameters");
    if (!ok) {
      // Resynchronize at the body so its statements still get checked.
      while (!at(TokenKind::Eof) && !at(TokenKind::LBrace) && !at(TokenKind::KwKernel)) next();
      if (!at(TokenKind::LBrace)) return nullptr;
    }
    if (!at(TokenKind::LBrace)) {
      expect(TokenKind::LBrace, "{", "to start kernel body");
      return nullptr;
    }
    kernel->body = parseBlock();
    return kernel;
  }

  // ------------------------------------------------------------- statements

  std::unique_ptr<BlockStmt> parseBlock() {
    auto block = std::make_unique<BlockStmt>(peek().loc);
    next(); // '{'
    while (!at(TokenKind::RBrace) && !at(TokenKind::Eof) && !diags_.tooMany()) {
      if (StmtPtr s = parseStatement())
        block->stmts.push_back(std::move(s));
      else
        skipStatement();
    }
    expect(TokenKind::RBrace, "}", "to close block");
    return block;
  }

  StmtPtr parseStatement() {
    const Token& t = peek();
    switch (t.kind) {
    case TokenKind::LBrace: return parseBlock();
    case TokenKind::KwIf: return parseIf();
    case TokenKind::KwReturn: {
      next();
      if (!expect(TokenKind::Semi, ";", "after 'return'")) return nullptr;
      return std::make_unique<ReturnStmt>(t.loc);
    }
    case TokenKind::KwInt:
    case TokenKind::KwFloat: return parseDecl();
    case TokenKind::Ident: {
      if (isUnsupportedKeyword(t.text)) {
        error(t, "'" + t.text + "' is not supported in WarpForge");
        return nullptr;
      }
      if (peek(1).kind == TokenKind::Assign) return parseAssign();
      if (peek(1).kind == TokenKind::LBracket) return parseStore();
      if (peek(1).kind == TokenKind::LParen) {
        error(t, "function calls are not supported in WarpForge (only global_id())");
        return nullptr;
      }
      error(peek(1), "expected '=' or '[' after '" + t.text + "', found " + describe(peek(1)));
      return nullptr;
    }
    case TokenKind::Unsupported: unsupportedOperator(t); return nullptr;
    default: error(t, "expected a statement, found " + describe(t)); return nullptr;
    }
  }

  // decl := ("int" | "float") IDENT "=" expr ";"
  StmtPtr parseDecl() {
    const Token& typeTok = peek();
    if ((typeTok.kind == TokenKind::KwFloat) && peek(1).kind == TokenKind::Star &&
        peek(2).kind != TokenKind::Star) {
      error(typeTok, "pointer locals are not supported in WarpForge; pointers may only be "
                     "kernel parameters");
      return nullptr;
    }
    TypeKind type = parseType();
    if (type == TypeKind::Error) return nullptr;
    const Token& name = peek();
    if (!expect(TokenKind::Ident, "<name>", "in declaration")) return nullptr;
    if (!at(TokenKind::Assign)) {
      error(name, "declaration of '" + name.text + "' must have an initializer");
      return nullptr;
    }
    next();
    ExprPtr init = parseExpr();
    if (!init) return nullptr;
    if (!expect(TokenKind::Semi, ";", "after declaration")) return nullptr;
    VarDecl* d = kernel_->newDecl(name.text, type, name.loc);
    return std::make_unique<DeclStmt>(typeTok.loc, d, std::move(init));
  }

  StmtPtr parseAssign() {
    const Token& name = next();
    next(); // '='
    ExprPtr value = parseExpr();
    if (!value) return nullptr;
    if (!expect(TokenKind::Semi, ";", "after assignment")) return nullptr;
    return std::make_unique<AssignStmt>(name.loc, std::make_unique<VarRefExpr>(name.loc, name.text),
                                        std::move(value));
  }

  StmtPtr parseStore() {
    const Token& name = next();
    next(); // '['
    ExprPtr index = parseExpr();
    if (!index) return nullptr;
    if (!expect(TokenKind::RBracket, "]", "after index")) return nullptr;
    if (!expect(TokenKind::Assign, "=", "in indexed store")) return nullptr;
    ExprPtr value = parseExpr();
    if (!value) return nullptr;
    if (!expect(TokenKind::Semi, ";", "after store")) return nullptr;
    return std::make_unique<StoreStmt>(name.loc,
                                       std::make_unique<VarRefExpr>(name.loc, name.text),
                                       std::move(index), std::move(value));
  }

  // if := "if" "(" expr ")" block [ "else" ( block | if ) ]
  StmtPtr parseIf() {
    const Token& ifTok = next();
    if (!expect(TokenKind::LParen, "(", "after 'if'")) return nullptr;
    ExprPtr cond = parseExpr();
    if (!cond) return nullptr;
    if (!expect(TokenKind::RParen, ")", "after if condition")) return nullptr;
    if (!at(TokenKind::LBrace)) {
      error(peek(), "expected '{' after if condition, found " + describe(peek()) +
                        " (braces are required)");
      return nullptr;
    }
    auto thenBlock = parseBlock();
    StmtPtr elseStmt;
    if (accept(TokenKind::KwElse)) {
      if (at(TokenKind::KwIf)) {
        elseStmt = parseIf();
        if (!elseStmt) return nullptr;
      } else if (at(TokenKind::LBrace)) {
        elseStmt = parseBlock();
      } else {
        error(peek(), "expected '{' or 'if' after 'else', found " + describe(peek()));
        return nullptr;
      }
    }
    return std::make_unique<IfStmt>(ifTok.loc, std::move(cond), std::move(thenBlock),
                                    std::move(elseStmt));
  }

  // ------------------------------------------------------------ expressions

  ExprPtr parseExpr() {
    ExprPtr lhs = parseAdditive();
    if (!lhs) return nullptr;
    if (at(TokenKind::Less)) {
      const Token& op = next();
      ExprPtr rhs = parseAdditive();
      if (!rhs) return nullptr;
      if (at(TokenKind::Less)) {
        error(peek(), "chained comparisons are not supported in WarpForge");
        return nullptr;
      }
      lhs = std::make_unique<BinaryExpr>(op.loc, BinaryOp::Less, std::move(lhs), std::move(rhs));
    }
    if (at(TokenKind::Unsupported)) {
      unsupportedOperator(peek());
      return nullptr;
    }
    return lhs;
  }

  ExprPtr parseAdditive() {
    ExprPtr lhs = parseTerm();
    while (lhs && (at(TokenKind::Plus) || at(TokenKind::Minus))) {
      const Token& op = next();
      ExprPtr rhs = parseTerm();
      if (!rhs) return nullptr;
      lhs = std::make_unique<BinaryExpr>(op.loc,
                                         op.kind == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Sub,
                                         std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr parseTerm() {
    ExprPtr lhs = parseUnary();
    while (lhs && at(TokenKind::Star)) {
      const Token& op = next();
      ExprPtr rhs = parseUnary();
      if (!rhs) return nullptr;
      lhs = std::make_unique<BinaryExpr>(op.loc, BinaryOp::Mul, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr parseUnary() {
    if (at(TokenKind::Minus)) {
      const Token& op = next();
      ExprPtr operand = parseUnary();
      if (!operand) return nullptr;
      return std::make_unique<NegExpr>(op.loc, std::move(operand));
    }
    return parsePrimary();
  }

  ExprPtr parsePrimary() {
    const Token& t = peek();
    switch (t.kind) {
    case TokenKind::IntLit: {
      next();
      // The lexer already diagnosed out-of-range literals; use 0 to continue.
      long long v = std::strtoll(t.text.c_str(), nullptr, 10);
      int32_t value = v > 2147483647LL ? 0 : static_cast<int32_t>(v);
      return std::make_unique<IntLitExpr>(t.loc, value);
    }
    case TokenKind::FloatLit: {
      next();
      // strtof rounds the decimal text to the nearest binary32 value. A
      // literal too large for float would silently become inf: reject it, as
      // the lexer does for out-of-range ints. (Tiny literals rounding to a
      // subnormal or to zero are exact IEEE behaviour and allowed.)
      float v = std::strtof(t.text.c_str(), nullptr);
      if (std::isinf(v)) {
        error(t, "float literal '" + t.text + "' is out of range for float (max ~3.4028235e38)");
        return nullptr;
      }
      return std::make_unique<FloatLitExpr>(t.loc, v, t.text);
    }
    case TokenKind::KwGlobalId: {
      next();
      if (!expect(TokenKind::LParen, "(", "after 'global_id'")) return nullptr;
      if (!expect(TokenKind::RParen, ")", "after 'global_id(' (it takes no arguments)"))
        return nullptr;
      return std::make_unique<GlobalIdExpr>(t.loc);
    }
    case TokenKind::Ident: {
      next();
      if (at(TokenKind::LParen)) {
        error(t, "function calls are not supported in WarpForge (only global_id())");
        return nullptr;
      }
      auto ref = std::make_unique<VarRefExpr>(t.loc, t.text);
      if (accept(TokenKind::LBracket)) {
        ExprPtr index = parseExpr();
        if (!index) return nullptr;
        if (!expect(TokenKind::RBracket, "]", "after index")) return nullptr;
        return std::make_unique<IndexExpr>(t.loc, std::move(ref), std::move(index));
      }
      return ref;
    }
    case TokenKind::LParen: {
      next();
      ExprPtr inner = parseExpr();
      if (!inner) return nullptr;
      if (!expect(TokenKind::RParen, ")", "to close parenthesis")) return nullptr;
      return inner;
    }
    case TokenKind::Unsupported: unsupportedOperator(t); return nullptr;
    default: error(t, "expected an expression, found " + describe(t)); return nullptr;
    }
  }

  const std::vector<Token>& toks_;
  DiagnosticEngine& diags_;
  size_t pos_ = 0;
  Kernel* kernel_ = nullptr;
};

} // namespace

std::unique_ptr<Program> parse(const std::vector<Token>& tokens, DiagnosticEngine& diags) {
  return Parser(tokens, diags).run();
}

} // namespace wf
