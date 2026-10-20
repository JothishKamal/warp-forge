// Parser for the textual IR (the format printed by ir/printer.cpp).
//
// Each function is parsed in two phases, which is what makes forward
// references (a phi naming a value defined in a later block) trivial:
//   1. Create every block (from labels) and every instruction, with no
//      operands yet. An instruction's result type is fully determined by its
//      spelling (`add.i32` -> i32, `lt.f32` -> i1, `addr` -> ptr, ...), so
//      every named value exists, correctly typed, before any operand is read.
//   2. Resolve operands and branch targets by name.
// Structural and SSA rules (dominance, phi entries, operand types) are not
// checked here; that is the verifier's job.

#include "ir/ir_parser.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <optional>
#include <unordered_map>
#include <vector>

namespace wf::ir {

namespace {

struct Token {
  std::string text;
  uint32_t col;
};

struct Line {
  uint32_t number;
  std::vector<Token> tokens;
};

class SyntaxError {
public:
  SyntaxError(uint32_t line, uint32_t col, std::string msg)
      : message(std::to_string(line) + ":" + std::to_string(col) + ": " + std::move(msg)) {}
  std::string message;
};

bool isWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '%' || c == '@' ||
         c == '-';
}

// Splits a line into words and single-character punctuation. Returns an
// empty token list for blank lines and ';' comments.
std::vector<Token> tokenize(std::string_view line) {
  std::vector<Token> out;
  size_t i = 0;
  while (i < line.size()) {
    char c = line[i];
    if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
    } else if (c == ';') {
      break;
    } else if (isWordChar(c)) {
      size_t start = i;
      while (i < line.size() && isWordChar(line[i])) ++i;
      out.push_back({std::string(line.substr(start, i - start)), static_cast<uint32_t>(start + 1)});
    } else {
      out.push_back({std::string(1, c), static_cast<uint32_t>(i + 1)});
      ++i;
    }
  }
  return out;
}

struct OpInfo {
  Op op;
  Type type; // result type
};

std::optional<OpInfo> lookupOpcode(const std::string& s) {
  auto suffix = [&](const char* base) -> std::optional<Type> {
    std::string b = base;
    if (s == b + ".i32") return Type::I32;
    if (s == b + ".f32") return Type::F32;
    return std::nullopt;
  };
  if (auto t = suffix("add")) return OpInfo{Op::Add, *t};
  if (auto t = suffix("sub")) return OpInfo{Op::Sub, *t};
  if (auto t = suffix("mul")) return OpInfo{Op::Mul, *t};
  if (auto t = suffix("neg")) return OpInfo{Op::Neg, *t};
  if (suffix("lt")) return OpInfo{Op::Lt, Type::I1};
  if (s == "global_id") return OpInfo{Op::GlobalId, Type::I32};
  if (s == "addr") return OpInfo{Op::Addr, Type::Ptr};
  if (s == "load.f32") return OpInfo{Op::Load, Type::F32};
  if (s == "store.f32") return OpInfo{Op::Store, Type::Void};
  if (s == "phi.i32") return OpInfo{Op::Phi, Type::I32};
  if (s == "phi.f32") return OpInfo{Op::Phi, Type::F32};
  if (s == "phi.i1") return OpInfo{Op::Phi, Type::I1};
  if (s == "phi.ptr") return OpInfo{Op::Phi, Type::Ptr};
  if (s == "br") return OpInfo{Op::Br, Type::Void};
  if (s == "condbr") return OpInfo{Op::CondBr, Type::Void};
  if (s == "ret") return OpInfo{Op::Ret, Type::Void};
  return std::nullopt;
}

std::optional<Type> lookupType(const std::string& s) {
  if (s == "i1") return Type::I1;
  if (s == "i32") return Type::I32;
  if (s == "f32") return Type::F32;
  if (s == "ptr") return Type::Ptr;
  return std::nullopt;
}

bool isNumbered(std::string_view name) {
  return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// One instruction line between the phases.
struct Pending {
  Instruction* inst;
  const Line* line;
  size_t firstOperand; // index into line->tokens
};

class Parser {
public:
  explicit Parser(std::string_view text) {
    uint32_t number = 0;
    size_t start = 0;
    while (start <= text.size()) {
      size_t end = text.find('\n', start);
      if (end == std::string_view::npos) end = text.size();
      ++number;
      auto tokens = tokenize(text.substr(start, end - start));
      if (!tokens.empty()) lines_.push_back({number, std::move(tokens)});
      if (end == text.size()) break;
      start = end + 1;
    }
    eofLine_ = number; // the last (possibly empty) line, where EOF sits
  }

  std::unique_ptr<Module> run() {
    auto module = std::make_unique<Module>();
    while (pos_ < lines_.size()) module->functions.push_back(function());
    return module;
  }

private:
  [[noreturn]] void fail(uint32_t line, uint32_t col, std::string msg) {
    throw SyntaxError(line, col, std::move(msg));
  }
  [[noreturn]] void fail(const Line& l, size_t tok, std::string msg) {
    uint32_t col = tok < l.tokens.size() ? l.tokens[tok].col
                                         : (l.tokens.empty() ? 1 : l.tokens.back().col +
                                                                       static_cast<uint32_t>(l.tokens.back().text.size()));
    fail(l.number, col, std::move(msg));
  }

  void expect(const Line& l, size_t& i, const char* text) {
    if (i >= l.tokens.size() || l.tokens[i].text != text)
      fail(l, i, std::string("expected '") + text + "'" +
                     (i < l.tokens.size() ? ", found '" + l.tokens[i].text + "'" : std::string()));
    ++i;
  }

  // kernel @name(type %a, ...) {
  std::unique_ptr<Function> function() {
    const Line& head = lines_[pos_++];
    size_t i = 0;
    expect(head, i, "kernel");
    if (i >= head.tokens.size() || head.tokens[i].text.size() < 2 || head.tokens[i].text[0] != '@')
      fail(head, i, "expected '@name' after 'kernel'");
    auto fn = std::make_unique<Function>(head.tokens[i++].text.substr(1));
    values_.clear();
    blocks_.clear();
    expect(head, i, "(");
    while (i < head.tokens.size() && head.tokens[i].text != ")") {
      auto type = lookupType(head.tokens[i].text);
      if (!type) fail(head, i, "unknown type '" + head.tokens[i].text + "'");
      ++i;
      if (i >= head.tokens.size() || head.tokens[i].text[0] != '%') fail(head, i, "expected '%name' for parameter");
      std::string name = head.tokens[i].text.substr(1);
      define(head, i, name, fn->addArg(*type, name));
      ++i;
      if (i < head.tokens.size() && head.tokens[i].text == ",") ++i;
    }
    expect(head, i, ")");
    expect(head, i, "{");

    // Collect the body up to the closing brace.
    std::vector<const Line*> body;
    bool closed = false;
    while (pos_ < lines_.size()) {
      const Line& l = lines_[pos_++];
      if (l.tokens.size() == 1 && l.tokens[0].text == "}") {
        closed = true;
        break;
      }
      body.push_back(&l);
    }
    if (!closed) fail(eofLine_, 1, "expected '}' to close kernel @" + fn->name);

    // Phase 1: blocks and instructions.
    for (const Line* l : body)
      if (isLabel(*l)) {
        const std::string& name = l->tokens[0].text;
        if (blocks_.count(name)) fail(*l, 0, "redefinition of block '" + name + "'");
        blocks_[name] = fn->createBlock(name);
      }
    std::vector<Pending> pending;
    BasicBlock* current = nullptr;
    for (const Line* l : body) {
      if (isLabel(*l)) {
        current = blocks_.at(l->tokens[0].text);
        continue;
      }
      if (!current) fail(*l, 0, "instruction outside a block (missing label?)");
      size_t j = 0;
      std::string result;
      if (l->tokens.size() >= 2 && l->tokens[0].text[0] == '%' && l->tokens[1].text == "=") {
        result = l->tokens[0].text.substr(1);
        j = 2;
      }
      if (j >= l->tokens.size()) fail(*l, j, "expected an instruction");
      auto info = lookupOpcode(l->tokens[j].text);
      if (!info) fail(*l, j, "unknown instruction '" + l->tokens[j].text + "'");
      if (info->type == Type::Void && !result.empty()) fail(*l, 0, "'" + l->tokens[j].text + "' has no result");
      if (info->type != Type::Void && result.empty()) fail(*l, j, "'" + l->tokens[j].text + "' needs a result name");
      auto inst = std::make_unique<Instruction>(info->op, info->type, std::vector<Value*>{});
      Instruction* raw = current->append(std::move(inst));
      if (!result.empty()) {
        if (!isNumbered(result)) raw->name = result;
        define(*l, 0, result, raw);
      }
      pending.push_back({raw, l, j + 1});
    }

    // Phase 2: operands and targets.
    for (const Pending& p : pending) operands(*fn, p);
    return fn;
  }

  static bool isLabel(const Line& l) { return l.tokens.size() == 2 && l.tokens[1].text == ":"; }

  void define(const Line& l, size_t tok, const std::string& name, Value* v) {
    if (!values_.emplace(name, v).second) fail(l, tok, "redefinition of '%" + name + "'");
  }

  Value* value(Function& fn, const Line& l, size_t tok) {
    if (tok >= l.tokens.size()) fail(l, tok, "expected an operand");
    const std::string& t = l.tokens[tok].text;
    if (t[0] == '%') {
      auto it = values_.find(t.substr(1));
      if (it == values_.end()) fail(l, tok, "undefined value '" + t + "'");
      return it->second;
    }
    if (t == "true" || t == "false") return fn.constI1(t == "true");
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
      uint32_t bits = 0;
      auto [ptr, ec] = std::from_chars(t.data() + 2, t.data() + t.size(), bits, 16);
      if (ec != std::errc() || ptr != t.data() + t.size()) fail(l, tok, "bad f32 constant '" + t + "'");
      return fn.constF32Bits(bits);
    }
    int32_t v = 0;
    auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (ec != std::errc() || ptr != t.data() + t.size()) fail(l, tok, "expected an operand, found '" + t + "'");
    return fn.constI32(v);
  }

  BasicBlock* block(const Line& l, size_t tok) {
    if (tok >= l.tokens.size()) fail(l, tok, "expected a block name");
    auto it = blocks_.find(l.tokens[tok].text);
    if (it == blocks_.end()) fail(l, tok, "undefined block '" + l.tokens[tok].text + "'");
    return it->second;
  }

  void operands(Function& fn, const Pending& p) {
    const Line& l = *p.line;
    size_t i = p.firstOperand;
    Instruction& inst = *p.inst;
    auto comma = [&] { expect(l, i, ","); };
    switch (inst.op()) {
    case Op::Phi:
      while (i < l.tokens.size()) {
        expect(l, i, "[");
        Value* v = value(fn, l, i++);
        comma();
        BasicBlock* from = block(l, i++);
        expect(l, i, "]");
        inst.addIncoming(v, from);
        if (i < l.tokens.size()) comma();
      }
      break;
    case Op::Br: inst.blocks().push_back(block(l, i++)); break;
    case Op::CondBr:
      inst.addOperand(value(fn, l, i++));
      comma();
      inst.blocks().push_back(block(l, i++));
      comma();
      inst.blocks().push_back(block(l, i++));
      break;
    case Op::Ret: case Op::GlobalId: break;
    default: {
      size_t arity = (inst.op() == Op::Neg || inst.op() == Op::Load) ? 1 : 2;
      for (size_t k = 0; k < arity; ++k) {
        if (k) comma();
        inst.addOperand(value(fn, l, i++));
      }
    }
    }
    if (i < l.tokens.size()) fail(l, i, "unexpected '" + l.tokens[i].text + "'");
  }

  std::vector<Line> lines_;
  size_t pos_ = 0;
  uint32_t eofLine_ = 1;
  std::unordered_map<std::string, Value*> values_;
  std::unordered_map<std::string, BasicBlock*> blocks_;
};

} // namespace

std::unique_ptr<Module> parseIR(std::string_view text, std::string& error) {
  try {
    return Parser(text).run();
  } catch (const SyntaxError& e) {
    error = e.message;
    return nullptr;
  }
}

} // namespace wf::ir
