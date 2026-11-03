#pragma once
#include "pch.hpp"
#include "il.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <sstream>
#include <charconv>
class Parser {
public:
	ILProgram parse(const std::shared_ptr<std::vector<std::string>> lines) {
		std::size_t cur_fn = -1;
		std::size_t cur_block = -1;

        // vreg name -> TypedReg within current function
        std::unordered_map<std::string, TypedReg> reg_map;

        auto flush_label_map = [&]() {
            label_map.clear();
            reg_map.clear();
            };

        for (size_t ln = 0; ln < lines->size(); ln++) {
            std::string line = trim(lines->operator[](ln));
            if (line.empty() || line[0] == ';') continue;

            // ── function declaration ─────────────────────────────
            // fn name(T, T) -> T
            if (line.starts_with("fn ")) {
                // ── resolve jump targets within function before returning ──────────────────────
                for (auto& u : unresolved) {
                    // rebuild label map for this function
                    // (we already have label_map only for last function — redo per fn)
                    // simpler: store label_map per function
                    // for now, error if not found
                    auto it = label_map.find(u.label);
                    if (it == label_map.end())
                        throw std::runtime_error("undefined label: " + u.label);
                    (&program.find_by_id(cur_fn)->get_block(u.block))->instrs[u.instr_idx].block_target = it->second;
                }
                unresolved.clear();
                flush_label_map();
                auto [name, ret, params] = parse_fn_decl(line);
                cur_fn = program.add_function(name, ret, params);
                cur_block = program.find_by_id(cur_fn)->entry();
                for(auto& param : params) {
                    param.reg = get_or_alloc_reg(param.name, param.type, program.find_by_id(cur_fn), reg_map).id;
                }
                label_map["entry"] = 0;
                continue;
            }

            // ── label ────────────────────────────────────────────
            // label:
            if (line.back() == ':' && line.find(' ') == std::string::npos) {
                if (cur_fn == UINT_MAX) throw std::runtime_error("label outside function");
                std::string lbl = line.substr(0, line.size() - 1);
                std::size_t blk = program.find_by_id(cur_fn)->add_block();
                label_map[lbl] = program.find_by_id(cur_fn)->get_block(blk).id;
                cur_block = blk;
                continue;
            }

            if (cur_fn == -1 || cur_block == -1)
                throw std::runtime_error("instruction outside function at line " + std::to_string(ln));

            // ── instructions ─────────────────────────────────────
            Instr instr = parse_instr(line, program.find_by_id(cur_fn), reg_map);

            // check if it has an unresolved jump label
            if (instr.op == Op::Jmp || instr.op == Op::Jz || instr.op == Op::Jnz || instr.op == Op::Js) {
                // block_target was set to UINT32_MAX with label stored separately
                // we store label in field_idx abuse — instead use a side channel
                std::string lbl = pending_label_;
                pending_label_.clear();
                size_t idx = (&program.find_by_id(cur_fn)->get_block(cur_block))->instrs.size();
                (&program.find_by_id(cur_fn)->get_block(cur_block))->instrs.push_back(instr);
                unresolved.push_back({ cur_fn, cur_block, idx, lbl });
                continue;
            }

            (&program.find_by_id(cur_fn)->get_block(cur_block))->instrs.push_back(instr);
        }

        // ── resolve jump targets within function before returning ──────────────────────
        for (auto& u : unresolved) {
            // rebuild label map for this function
            // (we already have label_map only for last function — redo per fn)
            // simpler: store label_map per function
            // for now, error if not found
            auto it = label_map.find(u.label);
            if (it == label_map.end())
                throw std::runtime_error("undefined label: " + u.label);
            (&program.find_by_id(cur_fn)->get_block(u.block))->instrs[u.instr_idx].block_target = it->second;
        }
        unresolved.clear();
        return program;
	}

private:
    ILProgram program;
    static struct UnresolvedJmp {
        std::size_t fn;
        std::size_t block;
        size_t      instr_idx;
        std::string label;
    };
    std::vector<UnresolvedJmp> unresolved;
    std::string pending_label_; // side channel for jump label

    // label -> block id within current function
    std::unordered_map<std::string, u32> label_map;

    // ── string helpers ───────────────────────────────────────────

    static std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, delim)) {
            tok = trim(tok);
            if (!tok.empty()) out.push_back(tok);
        }
        return out;
    }

    // ── type parsing ─────────────────────────────────────────────

    static ILType parse_type(const std::string& s) {
        if (s == "i8")    return ILTypes::I8;
        if (s == "i16")   return { ILTypeKind::I16 };
        if (s == "i32")   return ILTypes::I32;
        if (s == "i64")   return ILTypes::I64;
        if (s == "u8")    return ILTypes::U8;
        if (s == "u16")   return { ILTypeKind::U16 };
        if (s == "u32")   return ILTypes::U32;
        if (s == "u64")   return ILTypes::U64;
        if (s == "f32")   return ILTypes::F32;
        if (s == "f64")   return ILTypes::F64;
        if (s == "bool")  return ILTypes::Bool;
        if (s == "usize") return ILTypes::USize;
        if (s == "void")  return ILTypes::Void;
        if (s.starts_with("ptr<"))
            return ILTypes::Ptr(0); // pointee resolution skipped for now
        throw std::runtime_error("unknown type: " + s);
    }

    // ── fn declaration parsing ───────────────────────────────────
    // fn name(i32, i64) -> i32



    struct FnDecl { std::string name; ILType ret; std::vector<Param> params; };

    static FnDecl parse_fn_decl(const std::string& line) {
        // strip "fn "
        std::string rest = trim(line.substr(3));

        // name
        size_t paren = rest.find('(');
        if (paren == std::string::npos)
            throw std::runtime_error("bad fn decl: " + line);
        std::string name = trim(rest.substr(0, paren));

        // params
        size_t close = rest.find(')');
        std::string param_str = rest.substr(paren + 1, close - paren - 1);
        std::vector<Param> params;
        if (!trim(param_str).empty()) {
            for (auto& p : split(param_str, ',')) {
                auto parts = split(p, ':');
                params.push_back({ parts[0].substr(1), 0,  parse_type(parts[1])});
            }
        }

        // return type
        ILType ret = ILTypes::Void;
        size_t arrow = rest.find("->", close);
        if (arrow != std::string::npos)
            ret = parse_type(trim(rest.substr(arrow + 2)));

        return { name, ret, params };
    }
    // ── operand parsing ──────────────────────────────────────────
    // $name  or  integer literal  or  float literal

    static Operand parse_operand(const std::string& s,
        ILFunction& fn,
        std::unordered_map<std::string, TypedReg>& reg_map)
    {
        if (s.empty()) throw std::runtime_error("empty operand");

        if (s[0] == '$') {
            auto it = reg_map.find(s.substr(1));
            if (it == reg_map.end())
                throw std::runtime_error("undefined register: " + s);
            return Operand::from_reg(it->second.id);
        }

        // binary literal e.g. 1010b
        if (s.back() == 'b' && s.find_first_not_of("01", 0) == s.size() - 1) {
            u64 val = std::stoll(s.substr(0, s.size() - 1), nullptr, 2);
            return Operand::from_int(val);
        }

        // hex literal e.g. Ax or 0xFF
        if (s.back() == 'x' && s.size() > 1 && s[0] != '0') {
            u64 val = std::stoll(s.substr(0, s.size() - 1), nullptr, 16);
            return Operand::from_int(val);
        }
        if (s.size() > 2 && s[0] == '0' && s[1] == 'x') {
            u64 val = std::stoll(s, nullptr, 16);
            return Operand::from_int(val);
        }

        // float
        if (s.find('.') != std::string::npos) {
            return Operand::from_float(std::stod(s));
        }

        // integer
        return Operand::from_int(std::stoull(s));
    }

    // get or create a vreg by name
    static TypedReg get_or_alloc_reg(const std::string& name, ILType type,
        ILFunction *fn,
        std::unordered_map<std::string, TypedReg>& reg_map)
    {
        auto it = reg_map.find(name);
        if (it != reg_map.end()) return it->second;
        TypedReg r = fn->alloc_typed_reg(type);
        reg_map[name] = r;
        return r;
    }

    // ── instruction parsing ──────────────────────────────────────

    Instr parse_instr(const std::string& line,
        ILFunction *fn,
        std::unordered_map<std::string, TypedReg>& reg_map)
    {
        // check for assignment: $dst : type = ...  or  $dst = ...
        if (line[0] == '$') {
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error("expected '=' in: " + line);

            // parse lhs
            std::string lhs = trim(line.substr(0, eq));
            std::string rhs = trim(line.substr(eq + 1));

            // lhs may be "$name : type" or just "$name"
            std::string reg_name;
            ILType dst_type = ILTypes::I64; // default, overridden below
            size_t colon = lhs.find(':');
            if (colon != std::string::npos) {
                reg_name = trim(lhs.substr(1, colon - 1)); // strip $
                dst_type = parse_type(trim(lhs.substr(colon + 1)));
            }
            else {
                reg_name = trim(lhs.substr(1)); // strip $
            }

            TypedReg dst = get_or_alloc_reg(reg_name, dst_type, fn, reg_map);
            dst.type = dst_type;
            reg_map[reg_name] = dst;

            return parse_rhs(rhs, dst, *fn, reg_map);
        }

        // no assignment — void instructions
        return parse_void_instr(line, *fn, reg_map);
    }

    Instr parse_rhs(const std::string& rhs, TypedReg dst,
        ILFunction& fn,
        std::unordered_map<std::string, TypedReg>& reg_map)
    {
        auto tokens = split(rhs, ' ');
        if (tokens.empty()) throw std::runtime_error("empty rhs");

        const std::string& op_str = tokens[0];

        // binary ops: add sub mul div mod and or xor shl shr eq ne lt le gt ge
        static const std::unordered_map<std::string, Op> binops = {
            {"add",Op::Add},{"sub",Op::Sub},{"mul",Op::Mul},{"div",Op::Div},
            {"rem",Op::Rem},{"and",Op::And},{"or",Op::Or},{"xor",Op::Xor},
            {"shl",Op::Shl},{"shr",Op::Shr},{"eq",Op::Eq},{"ne",Op::Ne},
            {"lt",Op::Lt},{"le",Op::Le},{"gt",Op::Gt},{"ge",Op::Ge},
        };
        static const std::unordered_map<std::string, Op> unops = {
            {"neg",Op::Neg},{"not",Op::Not},
        };

        if (auto it = binops.find(op_str); it != binops.end()) {
            // tokens: op $a, $b  — split remaining by comma
            std::string rest = rhs.substr(op_str.size());
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("binop needs 2 operands: " + rhs);
            return Instr::make_binop(it->second, dst,
                parse_operand(args[0], fn, reg_map),
                parse_operand(args[1], fn, reg_map));
        }

        if (auto it = unops.find(op_str); it != unops.end()) {
            std::string rest = trim(rhs.substr(op_str.size()));
            return Instr::make_unop(it->second, dst, parse_operand(rest, fn, reg_map));
        }

        if (op_str == "alloc") {
            // $p = alloc T  or  $p = alloc T, $n
            std::string rest = trim(rhs.substr(op_str.size()));
            auto args = split(rest, ',');
            Instr i{};
            i.op = Op::Alloc;
            i.dst = dst;
            if (args.size() == 2)
                i.src[0] = parse_operand(args[1], fn, reg_map);
            return i;
        }

        if (op_str == "stackalloc") {
            Instr i{}; i.op = Op::StackAlloc; i.dst = dst; return i;
        }

        if (op_str == "load") {
            std::string rest = trim(rhs.substr(op_str.size()));
            return Instr::make_unop(Op::Load, dst, parse_operand(rest, fn, reg_map));
        }

        if (op_str == "offset") {
            std::string rest = trim(rhs.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("offset needs 2 operands");
            return Instr::make_binop(Op::Offset, dst,
                parse_operand(args[0], fn, reg_map),
                parse_operand(args[1], fn, reg_map));
        }

        if (op_str == "alloc_array") {
            std::string rest = trim(rhs.substr(op_str.size()));
            auto args = split(rest, ',');
            Instr i{}; i.op = Op::AllocArray; i.dst = dst;
            if (args.size() >= 2)
                i.src[0] = parse_operand(args[1], fn, reg_map);
            return i;
        }

        if (op_str == "array_load") {
            std::string rest = trim(rhs.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("array_load needs 2 operands");
            return Instr::make_binop(Op::ArrayLoad, dst,
                parse_operand(args[0], fn, reg_map),
                parse_operand(args[1], fn, reg_map));
        }

        if (op_str == "array_len") {
            std::string rest = trim(rhs.substr(op_str.size()));
            return Instr::make_unop(Op::ArrayLen, dst, parse_operand(rest, fn, reg_map));
        }

        if (op_str == "field_ptr") {
            std::string rest = trim(rhs.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("field_ptr needs 2 operands");
            Instr i{};
            i.op = Op::FieldPtr; i.dst = dst;
            i.src[0] = parse_operand(args[0], fn, reg_map);
            i.field_idx = (u32)std::stoul(args[1]);
            return i;
        }

        if (op_str == "bitcast") {
            std::string rest = trim(rhs.substr(op_str.size()));
            return Instr::make_unop(Op::Bitcast, dst, parse_operand(rest, fn, reg_map));
        }

        if (op_str == "ptr_to_int") {
            std::string rest = trim(rhs.substr(op_str.size()));
            return Instr::make_unop(Op::PtrToInt, dst, parse_operand(rest, fn, reg_map));
        }

        if (op_str == "int_to_ptr") {
            std::string rest = trim(rhs.substr(op_str.size()));
            return Instr::make_unop(Op::IntToPtr, dst, parse_operand(rest, fn, reg_map));
        }

        if (op_str == "call") {
            std::string rest = trim(rhs.substr(op_str.size()));
            auto args = split(rest, ',');
            std::string fname = args[0]; // e.g. "my_func"
            ILFunction* target = program.find(fname);
            if (!target) throw std::runtime_error("undefined function: " + fname);
            u32 conunt{ 1 };
            std::size_t offset = -1;
            for (; conunt < args.size(); conunt++) {
                auto it = reg_map.find(args[conunt].substr(1));
                std::size_t o = fn.push_arg(Operand::from_reg(it->second.id).reg);
                if (offset == -1) {
                    offset = o;
                }
            }

            return Instr::make_call(Op::Call, target->id, offset, conunt - 1, dst);
        }

        // plain copy / immediate assignment: $dst = $src or $dst = 42
        return Instr::make_unop(Op::Copy, dst, parse_operand(rhs, fn, reg_map));
    }

    Instr parse_void_instr(const std::string& line,
        ILFunction& fn,
        std::unordered_map<std::string, TypedReg>& reg_map)
    {
        auto tokens = split(line, ' ');
        const std::string& op_str = tokens[0];

        if (op_str == "free") {
            Instr i{}; i.op = Op::Free;
            i.src[0] = parse_operand(tokens[1], fn, reg_map);
            return i;
        }

        if (op_str == "store") {
            // store $p, $v
            std::string rest = trim(line.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("store needs 2 operands");
            Instr i{}; i.op = Op::Store;
            i.src[0] = parse_operand(args[0], fn, reg_map);
            i.src[1] = parse_operand(args[1], fn, reg_map);
            return i;
        }

        if (op_str == "array_store") {
            std::string rest = trim(line.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 3) throw std::runtime_error("array_store needs 3 operands");
            Instr i{}; i.op = Op::ArrayStore;
            i.src[0] = parse_operand(args[0], fn, reg_map);
            i.src[1] = parse_operand(args[1], fn, reg_map);
            // third operand stored nowhere yet — extend Instr if needed
            return i;
        }

        if (op_str == "jmp") {
            pending_label_ = tokens[1];
            return Instr::make_jmp(UINT32_MAX); // patched in second pass
        }

        if (op_str == "jz") {
            // jz $cond, label
            std::string rest = trim(line.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("jz needs cond and label");
            pending_label_ = args[1];
            return Instr::make_jz(parse_operand(args[0], fn, reg_map), UINT32_MAX);
        }

        if (op_str == "jnz") {
            std::string rest = trim(line.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("jnz needs cond and label");
            pending_label_ = args[1];
            Instr i = Instr::make_jz(parse_operand(args[0], fn, reg_map), UINT32_MAX);
            i.op = Op::Jnz;
            return i;
        }

        if (op_str == "js") {
            std::string rest = trim(line.substr(op_str.size()));
            auto args = split(rest, ',');
            if (args.size() != 2) throw std::runtime_error("jnz needs cond and label");
            pending_label_ = args[1];
            Instr i = Instr::make_jz(parse_operand(args[0], fn, reg_map), UINT32_MAX);
            i.op = Op::Js;
            return i;
        }

        if (op_str == "ret") {
            if (tokens.size() == 1) return Instr::make_ret_void();
            std::string rest = trim(line.substr(op_str.size()));

            return Instr::make_ret(parse_operand(rest, fn, reg_map));
        }

        throw std::runtime_error("unknown instruction: " + line);
    }
};