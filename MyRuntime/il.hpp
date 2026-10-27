#pragma once
#include "pch.hpp"

// ---- types ----

enum class ILTypeKind : u8 {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    Bool, USize, Void,
    Ptr, Array, Fn
};

struct ILType {
    ILTypeKind kind;
    u32        inner_id = 0;  // for Ptr/Array: pointee type id
    // for Fn: signature id
    bool operator==(const ILType&) const = default;

    bool is_integer() const {
        return kind >= ILTypeKind::I8 && kind <= ILTypeKind::USize;
    }
    bool is_float() const {
        return kind == ILTypeKind::F32 || kind == ILTypeKind::F64;
    }
    bool is_void() const { return kind == ILTypeKind::Void; }
    bool is_ptr()  const { return kind == ILTypeKind::Ptr; }

    u32 size_bytes() const {
        switch (kind) {
        case ILTypeKind::I8:  case ILTypeKind::U8:   case ILTypeKind::Bool: return 1;
        case ILTypeKind::I16: case ILTypeKind::U16:                         return 2;
        case ILTypeKind::I32: case ILTypeKind::U32:  case ILTypeKind::F32:  return 4;
        case ILTypeKind::I64: case ILTypeKind::U64:  case ILTypeKind::F64:  return 8;
        case ILTypeKind::USize: case ILTypeKind::Ptr:                        return 8;
        default: return 0; // Void, Array, Fn — caller handles
        }
    }
};

// common type constants
namespace ILTypes {
    inline constexpr ILType I8{ ILTypeKind::I8 };
    inline constexpr ILType I16{ ILTypeKind::I16 };
    inline constexpr ILType I32{ ILTypeKind::I32 };
    inline constexpr ILType I64{ ILTypeKind::I64 };
    inline constexpr ILType U8{ ILTypeKind::U8 };
    inline constexpr ILType U32{ ILTypeKind::U32 };
    inline constexpr ILType U64{ ILTypeKind::U64 };
    inline constexpr ILType F32{ ILTypeKind::F32 };
    inline constexpr ILType F64{ ILTypeKind::F64 };
    inline constexpr ILType Bool{ ILTypeKind::Bool };
    inline constexpr ILType USize{ ILTypeKind::USize };
    inline constexpr ILType Void{ ILTypeKind::Void };
    inline ILType Ptr(u32 pointee_id = 0) { return { ILTypeKind::Ptr, pointee_id }; }
}

// ---- registers ----

// virtual register — just an index
// the type is stored alongside where needed (operands, defs)
using V_reg = u32;
inline constexpr V_reg NO_REG = UINT32_MAX;

struct TypedReg {
    V_reg    id = NO_REG;
    ILType type = ILTypes::Void;
    bool valid() const { return id != NO_REG; }
};

// an operand is either a virtual register or an immediate
struct Operand {
    enum class Kind : u8 { V_reg, ImmInt, ImmFloat, Empty } kind;
    union {
        V_reg    reg;
        u64    imm_int;
        f64    imm_float;
    };

    static Operand from_reg(V_reg r) { Operand o; o.kind = Kind::V_reg;      o.reg = r;   return o; }
    static Operand from_int(u64 v) { Operand o; o.kind = Kind::ImmInt;   o.imm_int = v;   return o; }
    static Operand from_float(f64 v) { Operand o; o.kind = Kind::ImmFloat; o.imm_float = v;   return o; }
    
    bool is_reg()   const { return kind == Kind::V_reg; }
    bool is_imm()   const { return kind != Kind::V_reg; }
};

// ---- opcodes ----

enum class Op : u8 {
    // arithmetic
    Add, Sub, Mul, Div, Rem, Neg,
    And, Or, Xor, Shl, Shr, Not,
    // comparison
    Eq, Ne, Lt, Le, Gt, Ge,
    // memory
    Alloc, StackAlloc, Free,
    Load, Store,
    Offset,
    // arrays
    AllocArray, ArrayLoad, ArrayStore, ArrayLen,
    // structs
    FieldPtr,
    // casts
    Bitcast, PtrToInt, IntToPtr,
    // control flow
    Jmp, Jz, Jnz, Js,
    Call, Ret,
    // misc
    Copy,   // $dst = $src  (used after phi resolution etc.)
};

// ---- instructions ----

struct Instr {
    Op       op;
    TypedReg dst;       // NO_REG if op produces no value (store, free, jmp...)
    Operand  src[2];    // most ops use 1-2 operands
    u32      param_info[2];    // at [0] : offset, at [1] : len
    u8       src_count;
    // extra fields reused by different ops:
    union {
        u32 block_target;   // jmp/jz/jnz: target basic block id
        u32 struct_id;      // field_ptr: which struct
        u32 fn_id;          // call: which function
    };
    u32      field_idx = 0; // field_ptr: which field

    // helpers so call sites read cleanly
    static Instr make_binop(Op op, TypedReg dst, Operand a, Operand b) {
        Instr i{}; i.op = op; i.dst = dst; i.src[0] = a; i.src[1] = b; i.src_count = 2; return i;
    }
    static Instr make_unop(Op op, TypedReg dst, Operand a) {
        Instr i{}; i.op = op; i.dst = dst; i.src[0] = a; i.src_count = 1; return i;
    }
    static Instr make_jmp(u32 target) {
        Instr i{}; i.op = Op::Jmp; i.dst = {}; i.block_target = target; i.src_count = 0; return i;
    }
    static Instr make_jz(Operand cond, u32 target) {
        Instr i{}; i.op = Op::Jz; i.src[0] = cond; i.block_target = target; i.src_count = 1; return i;
    }
    static Instr make_call(Op op, u32 fn_id, u32 param_offset, u32 param_ammount, TypedReg dst) {
        Instr i{}; i.op = op; i.fn_id = fn_id; i.dst = dst; i.param_info[0] = param_offset; i.param_info[1] = param_ammount; i.src_count = 0; return i;
    }
    static Instr make_ret(Operand val) {
        Instr i{}; i.op = Op::Ret; i.src[0] = val; i.src_count = 1; return i;
    }
    static Instr make_ret_void() {
        Instr i{}; i.op = Op::Ret; i.src_count = 0; return i;
    }
};

// ---- basic block ----

struct BasicBlock {
    u32                id;
    std::vector<Instr> instrs;  // last instr must be a terminator

    bool empty() const { return instrs.empty(); }
    const Instr& terminator() const { return instrs.back(); }
    bool is_terminated() const {
        if (instrs.empty()) return false;
        auto op = instrs.back().op;
        return op == Op::Jmp || op == Op::Jz || op == Op::Jnz || op == Op::Ret;
    }
};

// ---- function ----
struct Param {
    std::string name;
    u32 reg;
    ILType type;

};
struct JitCode {
    void* ptr = nullptr;
    size_t size = 0;
    std::vector<u64> instr_starts;
};
struct ILFunction {
    u32                      id;
    std::string              name;
    ILType                   return_type;
    std::vector<Param>       param_types;
    std::vector<V_reg>       arg_pool;
    std::vector<BasicBlock>  blocks;    // blocks[0] = entry
    u32                      reg_count = 0; // bump this to allocate a new vreg

    // profiling
    alignas(8) volatile u64 call_count = 0;


    //code
    void* jit_code = nullptr;
    std::vector<u64> instr_starts;
    std::size_t code_size = 0;

    void assign_jit_code(JitCode jc) {
        jit_code = jc.ptr;
        code_size = jc.size;
        instr_starts = std::move(jc.instr_starts);
    }

    // allocate a new virtual register
    V_reg alloc_reg() { return reg_count++; }

    TypedReg alloc_typed_reg(ILType t) {
        return { alloc_reg(), t };
    }


    BasicBlock& get_block(std::size_t id) {
        return blocks[id];
    }

    std::size_t add_block() {
        u32 id = (u32)blocks.size();
        blocks.push_back({ id, {} });
        return id;
    }

    std::size_t entry() { return 0; }

    //for function calls
    u32 push_arg(const V_reg r) {
        u32 offset = arg_pool.size();
        arg_pool.push_back(r);
        return offset;
    }

    std::span<const V_reg> get_args(u32 offset, u32 count) const {
        return { arg_pool.data() + offset, count };
    }
};

// ---- program ----

struct ILProgram {
    std::vector<ILFunction> functions;
    // struct table comes later

    std::size_t add_function(std::string name, ILType ret, std::vector<Param> params) {
        u32 id = (u32)functions.size();
        functions.push_back({ id, std::move(name), ret, std::move(params) });
        functions.back().add_block(); // entry block
        return id;
    }

    ILFunction* find(std::string_view name) {
        for (auto& f : functions)
            if (f.name == name) return &f;
        return nullptr;
    }

    ILFunction* find_by_id(u32 id) {
        for (auto& f : functions)
            if (f.id == id) return &f;
        return nullptr;
    }
};