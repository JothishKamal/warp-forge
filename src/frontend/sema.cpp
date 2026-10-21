// Semantic analysis.
//
// Name resolution uses a stack of scopes (one per block). The kernel's
// parameters and the kernel body's top-level declarations share the
// outermost scope, as in C, so `kernel k(int n) { int n = 1; }` is a
// redeclaration while shadowing in a nested block is allowed.
//
// Typing has two entry points:
//   checkExpr   - any expression, may yield Bool (only `<` does)
//   checkValue  - an expression used as a value; a Bool here is an error
// TypeKind::Error marks "already diagnosed" and silences follow-on errors.

#include "frontend/sema.hpp"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace wf {

namespace {

class Sema {
public:
  explicit Sema(DiagnosticEngine& diags) : diags_(diags) {}

  void program(Program& p) {
    std::set<std::string> names;
    for (auto& k : p.kernels) {
      if (!names.insert(k->name).second) error(k->loc, "duplicate kernel name '" + k->name + "'");
      kernel(*k);
    }
  }

private:
  void error(SourceLoc loc, std::string msg) { diags_.error(loc, std::move(msg)); }

  // ------------------------------------------------------------ scopes

  void declare(VarDecl* d) {
    auto& scope = scopes_.back();
    if (!scope.emplace(d->name, d).second)
      error(d->loc, "redeclaration of '" + d->name + "' in the same scope");
  }

  VarDecl* lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
      if (auto found = it->find(name); found != it->end()) return found->second;
    return nullptr;
  }

  // -------------------------------------------------------------- kernel

  void kernel(Kernel& k) {
    scopes_.clear();
    scopes_.emplace_back();
    for (VarDecl* p : k.params) declare(p);
    if (k.body)
      for (auto& s : k.body->stmts) stmt(*s); // body shares the parameter scope
    scopes_.clear();
  }

  void block(BlockStmt& b) {
    scopes_.emplace_back();
    for (auto& s : b.stmts) stmt(*s);
    scopes_.pop_back();
  }

  void stmt(Stmt& s) {
    switch (s.kind) {
    case StmtKind::Decl: {
      auto& d = static_cast<DeclStmt&>(s);
      // Check the initializer before declaring, so `int x = x;` refers to
      // an outer x (or is undefined) rather than to itself.
      TypeKind t = checkValue(*d.init);
      if (t != TypeKind::Error && t != d.decl->type)
        error(d.init->loc, "cannot initialize '" + d.decl->name + "' of type " +
                               typeName(d.decl->type) + " with a value of type " + typeName(t));
      declare(d.decl);
      break;
    }
    case StmtKind::Assign: {
      auto& a = static_cast<AssignStmt&>(s);
      VarDecl* target = resolve(*a.target);
      if (target && target->type == TypeKind::FloatPtr) {
        // Report the real mistake, not "pointer used as a value" on the RHS.
        error(a.target->loc, "cannot assign to pointer parameter '" + target->name + "'");
        break;
      }
      TypeKind t = checkValue(*a.value);
      if (!target) break;
      if (t != TypeKind::Error && t != target->type)
        error(a.value->loc, std::string("cannot assign a value of type ") + typeName(t) +
                                " to '" + target->name + "' of type " + typeName(target->type));
      break;
    }
    case StmtKind::Store: {
      auto& st = static_cast<StoreStmt&>(s);
      checkPointerBase(*st.base);
      checkIndex(*st.index);
      TypeKind t = checkValue(*st.value);
      if (t != TypeKind::Error && t != TypeKind::Float)
        error(st.value->loc, std::string("stored value must be float, got ") + typeName(t));
      break;
    }
    case StmtKind::If: {
      auto& i = static_cast<IfStmt&>(s);
      TypeKind t = checkExpr(*i.cond);
      if (t != TypeKind::Error && t != TypeKind::Bool)
        error(i.cond->loc, "if condition must be a comparison (e.g. 'i < n')");
      block(*i.thenBlock);
      if (i.elseStmt) {
        if (i.elseStmt->kind == StmtKind::Block)
          block(static_cast<BlockStmt&>(*i.elseStmt));
        else
          stmt(*i.elseStmt); // else-if
      }
      break;
    }
    case StmtKind::Return: break;
    case StmtKind::Block: block(static_cast<BlockStmt&>(s)); break;
    }
  }

  // --------------------------------------------------------- expressions

  VarDecl* resolve(VarRefExpr& ref) {
    ref.decl = lookup(ref.name);
    if (!ref.decl) error(ref.loc, "undefined variable '" + ref.name + "'");
    return ref.decl;
  }

  // Base of `p[...]`: must be a float* parameter.
  void checkPointerBase(VarRefExpr& base) {
    VarDecl* d = resolve(base);
    if (!d) return;
    base.type = d->type;
    if (d->type != TypeKind::FloatPtr) error(base.loc, "cannot index non-pointer '" + d->name + "'");
  }

  void checkIndex(Expr& index) {
    TypeKind t = checkValue(index);
    if (t != TypeKind::Error && t != TypeKind::Int)
      error(index.loc, std::string("index must be int, got ") + typeName(t));
  }

  TypeKind checkValue(Expr& e) {
    TypeKind t = checkExpr(e);
    if (t == TypeKind::Bool) {
      error(e.loc, "comparison result can only be used as an if condition");
      return TypeKind::Error;
    }
    return t;
  }

  TypeKind checkExpr(Expr& e) {
    e.type = computeType(e);
    return e.type;
  }

  TypeKind computeType(Expr& e) {
    switch (e.kind) {
    case ExprKind::IntLit: return TypeKind::Int;
    case ExprKind::FloatLit: return TypeKind::Float;
    case ExprKind::GlobalId: return TypeKind::Int;
    case ExprKind::VarRef: {
      auto& ref = static_cast<VarRefExpr&>(e);
      VarDecl* d = resolve(ref);
      if (!d) return TypeKind::Error;
      if (d->type == TypeKind::FloatPtr) {
        error(ref.loc, "cannot use pointer '" + d->name + "' as a value (index it, e.g. '" +
                           d->name + "[i]')");
        return TypeKind::Error;
      }
      return d->type;
    }
    case ExprKind::Index: {
      auto& x = static_cast<IndexExpr&>(e);
      checkPointerBase(*x.base);
      checkIndex(*x.index);
      return TypeKind::Float;
    }
    case ExprKind::Neg: {
      TypeKind t = checkValue(*static_cast<NegExpr&>(e).operand);
      return t; // int or float (pointers/bools already diagnosed as Error)
    }
    case ExprKind::Binary: {
      auto& b = static_cast<BinaryExpr&>(e);
      TypeKind l = checkValue(*b.lhs);
      TypeKind r = checkValue(*b.rhs);
      if (l == TypeKind::Error || r == TypeKind::Error) return TypeKind::Error;
      if (l != r) {
        error(b.loc, std::string("invalid operands to '") + binaryOpSpelling(b.op) +
                         "': " + typeName(l) + " and " + typeName(r));
        return TypeKind::Error;
      }
      return b.op == BinaryOp::Less ? TypeKind::Bool : l;
    }
    }
    return TypeKind::Error;
  }

  DiagnosticEngine& diags_;
  std::vector<std::unordered_map<std::string, VarDecl*>> scopes_;
};

} // namespace

bool checkProgram(Program& program, DiagnosticEngine& diags) {
  size_t before = diags.errorCount();
  Sema(diags).program(program);
  return diags.errorCount() == before;
}

} // namespace wf
