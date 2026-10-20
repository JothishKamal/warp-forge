#include "frontend/ast.hpp"

#include <ostream>
#include <string>

namespace wf {

const char* typeName(TypeKind type) {
  switch (type) {
  case TypeKind::Int: return "int";
  case TypeKind::Float: return "float";
  case TypeKind::FloatPtr: return "float*";
  case TypeKind::Bool: return "bool";
  case TypeKind::Error: return "<error>";
  }
  return "?";
}

const char* binaryOpSpelling(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add: return "+";
  case BinaryOp::Sub: return "-";
  case BinaryOp::Mul: return "*";
  case BinaryOp::Less: return "<";
  }
  return "?";
}

namespace {

class AstDumper {
public:
  explicit AstDumper(std::ostream& out) : out_(out) {}

  void kernel(const Kernel& k) {
    line(0) << "Kernel " << k.name << '\n';
    for (const VarDecl* p : k.params) line(1) << "Param " << typeName(p->type) << ' ' << p->name << '\n';
    if (k.body) stmt(*k.body, 1);
  }

private:
  std::ostream& line(int depth) {
    out_ << std::string(static_cast<size_t>(depth) * 2, ' ');
    return out_;
  }

  void typed(const Expr& e) {
    if (e.type != TypeKind::Error) out_ << " : " << typeName(e.type);
    out_ << '\n';
  }

  void expr(const Expr& e, int d) {
    switch (e.kind) {
    case ExprKind::IntLit:
      line(d) << "Int " << static_cast<const IntLitExpr&>(e).value;
      typed(e);
      break;
    case ExprKind::FloatLit:
      line(d) << "Float " << static_cast<const FloatLitExpr&>(e).spelling;
      typed(e);
      break;
    case ExprKind::VarRef:
      line(d) << "Var " << static_cast<const VarRefExpr&>(e).name;
      typed(e);
      break;
    case ExprKind::Index: {
      const auto& x = static_cast<const IndexExpr&>(e);
      line(d) << "Index " << x.base->name;
      typed(e);
      expr(*x.index, d + 1);
      break;
    }
    case ExprKind::GlobalId:
      line(d) << "GlobalId";
      typed(e);
      break;
    case ExprKind::Neg:
      line(d) << "Neg";
      typed(e);
      expr(*static_cast<const NegExpr&>(e).operand, d + 1);
      break;
    case ExprKind::Binary: {
      const auto& b = static_cast<const BinaryExpr&>(e);
      line(d) << "Binary " << binaryOpSpelling(b.op);
      typed(e);
      expr(*b.lhs, d + 1);
      expr(*b.rhs, d + 1);
      break;
    }
    }
  }

  void stmt(const Stmt& s, int d) {
    switch (s.kind) {
    case StmtKind::Decl: {
      const auto& x = static_cast<const DeclStmt&>(s);
      line(d) << "Decl " << typeName(x.decl->type) << ' ' << x.decl->name << '\n';
      expr(*x.init, d + 1);
      break;
    }
    case StmtKind::Assign: {
      const auto& x = static_cast<const AssignStmt&>(s);
      line(d) << "Assign " << x.target->name << '\n';
      expr(*x.value, d + 1);
      break;
    }
    case StmtKind::Store: {
      const auto& x = static_cast<const StoreStmt&>(s);
      line(d) << "Store " << x.base->name << '\n';
      expr(*x.index, d + 1);
      expr(*x.value, d + 1);
      break;
    }
    case StmtKind::If: {
      const auto& x = static_cast<const IfStmt&>(s);
      line(d) << "If\n";
      expr(*x.cond, d + 1);
      stmt(*x.thenBlock, d + 1);
      if (x.elseStmt) {
        line(d + 1) << "Else\n";
        stmt(*x.elseStmt, d + 2);
      }
      break;
    }
    case StmtKind::Return: line(d) << "Return\n"; break;
    case StmtKind::Block:
      line(d) << "Block\n";
      for (const StmtPtr& c : static_cast<const BlockStmt&>(s).stmts) stmt(*c, d + 1);
      break;
    }
  }

  std::ostream& out_;
};

} // namespace

void dumpAst(const Program& program, std::ostream& out) {
  AstDumper dumper(out);
  for (const auto& k : program.kernels) dumper.kernel(*k);
}

} // namespace wf
