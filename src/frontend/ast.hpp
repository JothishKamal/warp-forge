#pragma once

#include "frontend/source.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace wf {

// `Bool` is the type of a comparison. It never appears in source and is only
// legal as an `if` condition (Sema enforces this). `Error` means "not yet
// type-checked" or "already diagnosed"; it suppresses cascading errors.
enum class TypeKind { Int, Float, FloatPtr, Bool, Error };

const char* typeName(TypeKind type);

struct VarDecl {
  std::string name;
  TypeKind type = TypeKind::Error;
  SourceLoc loc;
  bool isParam = false;
  unsigned paramIndex = 0;
};

// ---------------------------------------------------------------- expressions

enum class ExprKind { IntLit, FloatLit, VarRef, Index, GlobalId, Neg, Binary };
enum class BinaryOp { Add, Sub, Mul, Less };

const char* binaryOpSpelling(BinaryOp op); // "+", "-", "*", "<"

struct Expr {
  ExprKind kind;
  SourceLoc loc;
  TypeKind type = TypeKind::Error; // filled in by Sema
  Expr(ExprKind k, SourceLoc l) : kind(k), loc(l) {}
  virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLitExpr : Expr {
  int32_t value;
  IntLitExpr(SourceLoc l, int32_t v) : Expr(ExprKind::IntLit, l), value(v) {}
};

struct FloatLitExpr : Expr {
  float value;
  std::string spelling; // kept for dumps, so `0.1` prints as written
  FloatLitExpr(SourceLoc l, float v, std::string s)
      : Expr(ExprKind::FloatLit, l), value(v), spelling(std::move(s)) {}
};

struct VarRefExpr : Expr {
  std::string name;
  VarDecl* decl = nullptr; // resolved by Sema
  VarRefExpr(SourceLoc l, std::string n) : Expr(ExprKind::VarRef, l), name(std::move(n)) {}
};

// base[index] used as a value (a load).
struct IndexExpr : Expr {
  std::unique_ptr<VarRefExpr> base;
  ExprPtr index;
  IndexExpr(SourceLoc l, std::unique_ptr<VarRefExpr> b, ExprPtr i)
      : Expr(ExprKind::Index, l), base(std::move(b)), index(std::move(i)) {}
};

struct GlobalIdExpr : Expr {
  explicit GlobalIdExpr(SourceLoc l) : Expr(ExprKind::GlobalId, l) {}
};

struct NegExpr : Expr {
  ExprPtr operand;
  NegExpr(SourceLoc l, ExprPtr o) : Expr(ExprKind::Neg, l), operand(std::move(o)) {}
};

struct BinaryExpr : Expr {
  BinaryOp op;
  ExprPtr lhs, rhs;
  BinaryExpr(SourceLoc l, BinaryOp o, ExprPtr a, ExprPtr b)
      : Expr(ExprKind::Binary, l), op(o), lhs(std::move(a)), rhs(std::move(b)) {}
};

// ----------------------------------------------------------------- statements

enum class StmtKind { Decl, Assign, Store, If, Return, Block };

struct Stmt {
  StmtKind kind;
  SourceLoc loc;
  Stmt(StmtKind k, SourceLoc l) : kind(k), loc(l) {}
  virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct DeclStmt : Stmt {
  VarDecl* decl; // owned by the enclosing Kernel
  ExprPtr init;
  DeclStmt(SourceLoc l, VarDecl* d, ExprPtr i)
      : Stmt(StmtKind::Decl, l), decl(d), init(std::move(i)) {}
};

struct AssignStmt : Stmt {
  std::unique_ptr<VarRefExpr> target;
  ExprPtr value;
  AssignStmt(SourceLoc l, std::unique_ptr<VarRefExpr> t, ExprPtr v)
      : Stmt(StmtKind::Assign, l), target(std::move(t)), value(std::move(v)) {}
};

struct StoreStmt : Stmt {
  std::unique_ptr<VarRefExpr> base;
  ExprPtr index, value;
  StoreStmt(SourceLoc l, std::unique_ptr<VarRefExpr> b, ExprPtr i, ExprPtr v)
      : Stmt(StmtKind::Store, l), base(std::move(b)), index(std::move(i)), value(std::move(v)) {}
};

struct BlockStmt : Stmt {
  std::vector<StmtPtr> stmts;
  explicit BlockStmt(SourceLoc l) : Stmt(StmtKind::Block, l) {}
};

struct IfStmt : Stmt {
  ExprPtr cond;
  std::unique_ptr<BlockStmt> thenBlock;
  StmtPtr elseStmt; // a BlockStmt, an IfStmt (else-if), or null
  IfStmt(SourceLoc l, ExprPtr c, std::unique_ptr<BlockStmt> t, StmtPtr e)
      : Stmt(StmtKind::If, l), cond(std::move(c)), thenBlock(std::move(t)),
        elseStmt(std::move(e)) {}
};

struct ReturnStmt : Stmt {
  explicit ReturnStmt(SourceLoc l) : Stmt(StmtKind::Return, l) {}
};

// ---------------------------------------------------------------- top level

struct Kernel {
  std::string name;
  SourceLoc loc;
  std::vector<VarDecl*> params;
  std::unique_ptr<BlockStmt> body;
  std::vector<std::unique_ptr<VarDecl>> decls; // owns params and locals

  VarDecl* newDecl(std::string declName, TypeKind type, SourceLoc declLoc) {
    decls.push_back(std::make_unique<VarDecl>(VarDecl{std::move(declName), type, declLoc}));
    return decls.back().get();
  }
};

struct Program {
  std::vector<std::unique_ptr<Kernel>> kernels;
};

// Indented tree, two spaces per level. After Sema, expressions are printed
// with their type (`Var i : int`).
void dumpAst(const Program& program, std::ostream& out);

} // namespace wf
