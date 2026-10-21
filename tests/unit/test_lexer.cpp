#include "frontend/lexer.hpp"

#include <gtest/gtest.h>
#include <sstream>

using namespace wf;

namespace {

struct Lexed {
  SourceFile file;
  DiagnosticEngine diags;
  std::vector<Token> tokens;
  explicit Lexed(std::string text) : file("t.wf", std::move(text)), diags(file) {
    tokens = lex(file, diags);
  }
};

} // namespace

TEST(Lexer, VectorAdd) {
  Lexed l("kernel vector_add(float* a, int n) {\n  int i = global_id();\n}\n");
  ASSERT_EQ(l.diags.errorCount(), 0u);
  std::vector<TokenKind> expected = {
      TokenKind::KwKernel, TokenKind::Ident,  TokenKind::LParen, TokenKind::KwFloat,
      TokenKind::Star,     TokenKind::Ident,  TokenKind::Comma,  TokenKind::KwInt,
      TokenKind::Ident,    TokenKind::RParen, TokenKind::LBrace, TokenKind::KwInt,
      TokenKind::Ident,    TokenKind::Assign, TokenKind::KwGlobalId, TokenKind::LParen,
      TokenKind::RParen,   TokenKind::Semi,   TokenKind::RBrace, TokenKind::Eof};
  ASSERT_EQ(l.tokens.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(l.tokens[i].kind, expected[i]) << "token " << i;
  EXPECT_EQ(l.tokens[1].text, "vector_add");
  EXPECT_EQ(l.tokens[1].loc.line, 1u);
  EXPECT_EQ(l.tokens[1].loc.col, 8u);
  EXPECT_EQ(l.tokens[11].loc.line, 2u);
  EXPECT_EQ(l.tokens[11].loc.col, 3u);
}

TEST(Lexer, FloatForms) {
  Lexed l("1.5 1.5f 2e3 1.0e-2f 7");
  ASSERT_EQ(l.diags.errorCount(), 0u);
  EXPECT_EQ(l.tokens[0].kind, TokenKind::FloatLit);
  EXPECT_EQ(l.tokens[1].kind, TokenKind::FloatLit);
  EXPECT_EQ(l.tokens[1].text, "1.5f");
  EXPECT_EQ(l.tokens[2].kind, TokenKind::FloatLit);
  EXPECT_EQ(l.tokens[3].kind, TokenKind::FloatLit);
  EXPECT_EQ(l.tokens[4].kind, TokenKind::IntLit);
}

TEST(Lexer, IntOverflow) {
  Lexed l("x = 2147483648;");
  ASSERT_EQ(l.diags.errorCount(), 1u);
  EXPECT_EQ(l.diags.all()[0].message, "integer literal '2147483648' is out of range for int");
  EXPECT_EQ(l.diags.all()[0].loc.col, 5u);
}

TEST(Lexer, IntMaxIsFine) {
  Lexed l("2147483647");
  EXPECT_EQ(l.diags.errorCount(), 0u);
}

TEST(Lexer, UnterminatedComment) {
  Lexed l("a /* never\nends");
  ASSERT_EQ(l.diags.errorCount(), 1u);
  EXPECT_EQ(l.diags.all()[0].message, "unterminated block comment");
  EXPECT_EQ(l.diags.all()[0].loc.col, 3u);
  EXPECT_EQ(l.tokens.back().kind, TokenKind::Eof);
}

TEST(Lexer, CommentsSkipped) {
  Lexed l("a // x\n/* y */ b");
  ASSERT_EQ(l.tokens.size(), 3u);
  EXPECT_EQ(l.tokens[1].text, "b");
  EXPECT_EQ(l.tokens[1].loc.line, 2u);
}

TEST(Lexer, BadChar) {
  Lexed l("a @ b");
  ASSERT_EQ(l.diags.errorCount(), 1u);
  EXPECT_EQ(l.diags.all()[0].message, "unexpected character '@'");
}

TEST(Lexer, UnsupportedOperatorsAreTokens) {
  Lexed l("a / b >= c == d");
  EXPECT_EQ(l.diags.errorCount(), 0u);
  EXPECT_EQ(l.tokens[1].kind, TokenKind::Unsupported);
  EXPECT_EQ(l.tokens[1].text, "/");
  EXPECT_EQ(l.tokens[3].text, ">=");
  EXPECT_EQ(l.tokens[5].text, "==");
}

TEST(Lexer, DumpFormat) {
  Lexed l("int x;");
  std::ostringstream out;
  dumpTokens(l.tokens, out);
  EXPECT_EQ(out.str(), "1:1 KwInt 'int'\n1:5 Ident 'x'\n1:6 Semi ';'\n1:7 Eof ''\n");
}
