#pragma once
#include "registers.hpp"
#include "pch.hpp"
#include <stdexcept>
#include <iostream>
#include "il.hpp"

// ---------------------------------------------------------------------------
// x86-64 Windows (MSVC ABI) machine-code emitter
//
// Generates the prologue / epilogue bytes and tracks named local variables
// at negative offsets from RBP, following the Windows x64 calling convention:
//
//   - Shadow space  : 32 bytes above return address (for rcx/rdx/r8/r9)
//   - Stack aligned : RSP must be 16-byte aligned before any CALL
//   - Frame pointer : RBP used as stable base (optional but emitted here)
//   - Locals        : [rbp - N] where N grows downward
//
// Usage:
//   StackFrameEmitter e;
//   e.emit_prologue();
//   e.declare_var("x", VarSize::DWORD);   // int   → [rbp-4]
//   e.declare_var("y", VarSize::QWORD);   // long  → [rbp-8] (after x at -4, aligned to -8 boundary)
//   e.declare_var("b", VarSize::BYTE);    // char  → [rbp-9]
//   e.emit_epilogue();
//   auto bytes = e.code();                // raw machine code bytes
// ---------------------------------------------------------------------------


struct VarInfo {
    int32_t  rbp_offset; // negative offset from RBP  (e.g. -4 for first DWORD)
    u32  size;
};
struct RealRegInfo {
    u32 v_reg;
    u8 r_reg;
};

class Emitter {
public:
    void rex_w(bool w, bool r, bool x, bool b);
    void emit_mov_imm_reg(Reg dst, uint64_t value);
    void emit_f7(u8 ext, Reg rm);
    void emit_rr(u8 opcode, Reg dst, Reg src);
    void push_r(Reg src);
    void push_i(i64);
    void pop_r(Reg dst);
    void mov_rr(Reg dst, Reg src);
    void mov_ri(Reg dst, i64 imm);
    void mov_rm(Reg dst, i32 rbp_offset);
    void mov_mr(i32 rbp_offset, Reg src);
    void add_rr(Reg dst, Reg src);
    void add_ri(Reg dst, i64 imm);
    void sub_rr(Reg dst, Reg src);
    void sub_ri(Reg dst, i64 imm);
    void mul_rr(Reg dst, Reg src);
    void div_rr(Reg dst, Reg src);
    void imul_rr(Reg dst, Reg src);
    void cmp_rr(Reg a, Reg b);
    void jmp_rel32(i32 offset);
    void je_rel32(i32 offset);   // patch these after layout
    void call_r(Reg r);
    void ret();

    void mark_instr() { instr_starts.push_back(current_offset()); }

    // for patching forward branches:
    u32  emit_jcc_placeholder();
    void patch_rel32(u32 offset, u32 target);
    JitCode commit();  // mmap(PROT_READ|PROT_WRITE|PROT_EXEC), memcpy, return

    void free_code(void* mem, size_t size);
    // -----------------------------------------------------------------------
    // Prologue: push rbp / mov rbp,rsp / sub rsp,<placeholder>
    // The SUB immediate is patched when emit_epilogue() (or patch_prologue())
    // is called, once the total frame size is known.
    // -----------------------------------------------------------------------
    void emit_prologue(const volatile void* counter) {
        mark_instr();
        if (prologue_emitted_)
            throw std::logic_error("emit_prologue() already called");

        // push rbp                      ; 55
        emit(0x55);

        mark_instr();
        // mov rbp, rsp                  ; 48 89 E5
        emit(0x48); emit(0x89); emit(0xE5);

        mark_instr();

        // sub rsp, imm32  (placeholder) ; 48 81 EC [00 00 00 00]
        sub_rsp_offset_ = static_cast<int32_t>(buf_.size());
        emit(0x48); emit(0x81); emit(0xEC);
        emitImm32(0x00000000);   // patched later

        emit_count_call(counter);

        prologue_emitted_ = true;
    }

    void emit_label(const u32 id) {
        labels[id] = current_offset();
        // patch any forward jumps that were waiting for this label
        for (auto it = patches.begin(); it != patches.end(); ) {
            if (it->second == id) {
                u32 rel = current_offset() - (it->first + 4);
                //std::cout << "emitted label to instr: " << rel << "\n";
                patch32(it->first, rel);
                it = patches.erase(it);
            }
            else ++it;
        }
    }

    void emit_jmp(const u32 id) {
        emit(0xE9);
        auto it = labels.find(id);
        if (it != labels.end()) {
            // backward jump — target already known
            i32 rel = it->second - ((i32)current_offset() + 4);
            //std::cout << "emitted jmp to instr: " << rel << "\n";
            emitImm32(rel);
        }
        else {
            // forward jump — patch later
            patches.push_back({ (i32)current_offset(), id });
            emitImm32(0x00000000);
        }
    }
    void Emitter::emit_test_reg(Reg reg) {
        uint8_t rex = 0x48;
        //if (needsREX(reg)) rex |= 0x01 | 0x04; // REX.B and REX.R
        if (isExt(reg)) {
            emit(rex);
        }
        emit(0x85);
        emit(0xC0 | (regId(reg) << 3) | regId(reg)); // ModRM: mod=11, reg=reg, r/m=reg
    }

    enum class Cond : u8 {
        O = 0x0, NO = 0x1,
        B = 0x2, AE = 0x3,   // unsigned <, >=   (carry)
        E = 0x4, NE = 0x5,   // ==, !=           (zero)
        BE = 0x6, A = 0x7,   // unsigned <=, >
        S = 0x8, NS = 0x9,   // sign
        P = 0xA, NP = 0xB,   // parity
        L = 0xC, GE = 0xD,   // signed <, >=
        LE = 0xE, G = 0xF,   // signed <=, >
    };

    void Emitter::emit_jcc(Cond cc, const u32 id) {
        emit(0x0F);
        emit(0x80 | static_cast<u8>(cc));
        auto it = labels.find(id);
        if (it != labels.end()) {
            // backward jump, target known
            i32 rel = it->second - ((i32)current_offset() + 4);
            //std::cout << "emitted jmp to instr: " << rel << "\n";
            emitImm32(rel);
        }
        else {
            // forward jump, patched in emit_label
            patches.push_back({ current_offset(), id });
            emitImm32(0xCCCCCCCC);   // jumps into the weeds, fails fast
        }
    }
    void setcc(Cond cc) { emit({ 0x0F, (u8)(0x90 | (u8)cc), 0xC0 }); } // setcc al
    void movzx_eax_al() { emit({ 0x0F, 0xB6, 0xC0 }); }                // movzx eax, al

    void movzx_eax_mem(i32 rbp_offset, u32 size) {      // size 1 or 2
        emit(0x0F); emit(size == 1 ? 0xB6 : 0xB7);      // movzx eax, byte/word ptr [rbp+disp32]
        emit(0x85);
        emitImm32(rbp_offset);
    }

    void Emitter::emit_jz(u32 id) { emit_jcc(Cond::E, id); }
    void Emitter::emit_jnz(u32 id) { emit_jcc(Cond::NE, id); }
    void Emitter::emit_js(u32 id) { emit_jcc(Cond::S, id); }
    void Emitter::emit_jns(u32 id) { emit_jcc(Cond::NS, id); }

    // signed
    void Emitter::emit_jl(u32 id) { emit_jcc(Cond::L, id); }
    void Emitter::emit_jle(u32 id) { emit_jcc(Cond::LE, id); }
    void Emitter::emit_jg(u32 id) { emit_jcc(Cond::G, id); }
    void Emitter::emit_jge(u32 id) { emit_jcc(Cond::GE, id); }

    // unsigned
    void Emitter::emit_jb(u32 id) { emit_jcc(Cond::B, id); }
    void Emitter::emit_jbe(u32 id) { emit_jcc(Cond::BE, id); }
    void Emitter::emit_ja(u32 id) { emit_jcc(Cond::A, id); }
    void Emitter::emit_jae(u32 id) { emit_jcc(Cond::AE, id); }

    // -----------------------------------------------------------------------
    // Declare a named local variable.
    // Returns the VarInfo (rbp offset + size) for use in subsequent emits.
    // -----------------------------------------------------------------------
    VarInfo declare_var(const u32 v_reg, u32 size) {
        if (!prologue_emitted_)
            throw std::logic_error("call emit_prologue() first");
        if (epilogue_emitted_)
            throw std::logic_error("cannot declare vars after emit_epilogue()");
        if (vars_.count(v_reg))
        {
            throw std::invalid_argument("duplicate variable name: " + v_reg);
        }


        // Align the next offset to the variable's natural alignment
        // (BYTE=1, WORD=2, DWORD=4, QWORD=8).
        current_offset_ = align_down(static_cast<i32>(current_offset_ - static_cast<i32>(size)), static_cast<i32>(size));

        VarInfo info{ current_offset_, size };
        vars_[v_reg] = info;

        return info;
    }

    // -----------------------------------------------------------------------
    // Look up a previously declared variable.
    // -----------------------------------------------------------------------
    VarInfo var(const u32 v_reg) const {
        auto it = vars_.find(v_reg);
        if (it == vars_.end())
            throw std::invalid_argument("unknown variable: " + v_reg);
        return it->second;
    }
    // -----------------------------------------------------------------------
    // Emit  MOV [rbp + offset], imm   for a variable (32-bit immediate max).
    // -----------------------------------------------------------------------
    void emit_store_imm_stack(const u32 v_reg, int64_t value) {
        VarInfo v = var(v_reg);
        const int32_t disp = v.rbp_offset;

        auto store_dword = [&](int32_t d, uint32_t imm) {
            emit(0xC7); emit(0x85);
            emitImm32(static_cast<uint32_t>(d));
            emitImm32(imm);
            };

        switch (v.size) {
        case 1:
            emit(0xC6); emit(0x85);
            emitImm32(static_cast<uint32_t>(disp));
            emit(static_cast<uint8_t>(value));
            break;
        case 2:
            emit(0x66); emit(0xC7); emit(0x85);
            emitImm32(static_cast<uint32_t>(disp));
            emitImm16(static_cast<uint16_t>(value));
            break;
        case 4:
            store_dword(disp, static_cast<uint32_t>(value));
            break;
        case 8:
            if (value >= INT32_MIN && value <= INT32_MAX) {
                // 48 C7 85 [disp32] [imm32], sign-extended by the CPU
                emit(0x48); emit(0xC7); emit(0x85);
                emitImm32(static_cast<uint32_t>(disp));
                emitImm32(static_cast<uint32_t>(value));
            }
            else {
                // no store-imm64 encoding exists; split into two dword stores
                store_dword(disp, static_cast<uint32_t>(value));
                store_dword(disp + 4, static_cast<uint32_t>(static_cast<uint64_t>(value) >> 32));
            }
            break;
        default:
            assert(!"unsupported var size");
        }
    }

    void emit_store_reg(const u32 v_reg, const u8 src_reg) {
        VarInfo v = var(v_reg);
        uint8_t reg = src_reg; // e.g. 0=RAX, 1=RCX, 2=RDX, etc.

        switch (v.size) {
        case 1:
            // 88 85 [disp32]  =>  MOV [rbp+disp32], reg8
            if (reg >= 8) emit(0x41); // REX.R for r8b–r15b
            emit(0x88);
            emit(0x85 | ((reg & 7) << 3));
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            break;
        case 2:
            // 66 89 85 [disp32]  =>  MOV [rbp+disp32], reg16
            emit(0x66);
            if (reg >= 8) emit(0x41);
            emit(0x89);
            emit(0x85 | ((reg & 7) << 3));
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            break;
        case 4:
            // 89 85 [disp32]  =>  MOV [rbp+disp32], reg32
            if (reg >= 8) emit(0x41);
            emit(0x89);
            emit(0x85 | ((reg & 7) << 3));
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            break;
        case 8:
            // 48 89 85 [disp32]  =>  MOV [rbp+disp32], reg64
            emit(reg >= 8 ? 0x4C : 0x48); // REX.W (+ REX.R if extended reg)
            emit(0x89);
            emit(0x85 | ((reg & 7) << 3));
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            break;
        }
    }

    // -----------------------------------------------------------------------
    // Epilogue: patch the SUB RSP immediate, then:
    //   leave   (mov rsp,rbp  +  pop rbp)
    //   ret
    // -----------------------------------------------------------------------
    void emit_epilogue() {
        if (!prologue_emitted_)
            throw std::logic_error("call emit_prologue() first");
        if (epilogue_emitted_)
            throw std::logic_error("emit_epilogue() already called");

        patch_prologue();
        
        mark_instr();
        // leave   ; C9
        emit(0xC9);
        mark_instr();
        // ret     ; C3
        emit(0xC3);
        //epilogue_emitted_ = true;
        //flush_();
    }


    bool contains_var(u32 v_reg) {
        return vars_.contains(v_reg);
    }

    void emit(uint8_t b) { buf_.push_back(b); }
    void emit(std::initializer_list<uint8_t> bs) { for (auto b : bs) emit(b); }

    inline void emitImm16(i16 imm) {
        emit(imm & 0xFF);
        emit((imm >> 8) & 0xFF);
    }

    inline void emitImm32(i32 imm) {
        emit(imm & 0xFF);
        emit((imm >> 8) & 0xFF);
        emit((imm >> 16) & 0xFF);
        emit((imm >> 24) & 0xFF);
    }

    inline void emitImm64(i64 imm) {
        for (int i = 0; i < 8; i++)
            emit((imm >> (i * 8)) & 0xFF);
    }

    // PROFILING

    void Emitter::emit_count_call(const volatile void* counter) {
        emit(0x49); emit(0xBB);
        emitImm64((u64)counter);
        emit(0x49); emit(0xFF); emit(0x03);      // inc qword [r11]
    }

    // -----------------------------------------------------------------------
    // Hex dump — handy for debugging.
    // -----------------------------------------------------------------------
    std::string hex_dump() const {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(buf_.size() * 3);
        for (uint8_t b : buf_) {
            out += hex[b >> 4];
            out += hex[b & 0xF];
            out += ' ';
        }
        if (!out.empty()) out.pop_back();
        return out;
    }

    std::string bin_dump() const {
        std::string out;
        out.reserve(buf_.size() * 9);
        for (uint8_t b : buf_) {
            for (int i = 7; i >= 0; --i)
                out += (b >> i) & 1 ? '1' : '0';
            out += ' ';
        }
        if (!out.empty()) out.pop_back();
        return out;
    }

    // -----------------------------------------------------------------------
    // Frame size (bytes allocated on the stack for locals, aligned to 16).
    // -----------------------------------------------------------------------
    uint32_t frame_size() const { return frame_size_; }

    
private:
    // Align x DOWN to the nearest multiple of align (align must be power-of-2)
    static int32_t align_down(int32_t x, int32_t align) {
        // For negative x and positive align, integer division truncates toward
        // zero, which is WRONG for alignment — we need to floor divide.
        return (x / align - (x % align != 0 ? 1 : 0)) * align;
    }

    // Round UP to next multiple of 16 (for RSP alignment).
    static uint32_t align16(uint32_t x) {
        return (x + 15u) & ~15u;
    }

    // Zero-initialise a variable at [rbp + offset].
    void emit_zero_init(const VarInfo& v) {
        emit_store_imm_raw(v, 0);
    }

    u32 current_offset() {
        return static_cast<u32>(buf_.size());
    }


    // Internal store — same as emit_store_imm but takes VarInfo directly.
    void emit_store_imm_raw(const VarInfo& v, int32_t value) {
        switch (v.size) {
        case 1:
            emit(0xC6); emit(0x85);
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            emit(static_cast<uint8_t>(value));
            break;
        case 2:
            emit(0x66); emit(0xC7); emit(0x85);
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            emitImm16(static_cast<uint16_t>(value));
            break;
        case 4:
            emit(0xC7); emit(0x85);
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            emitImm32(static_cast<uint32_t>(value));
            break;
        case 8:
            emit(0x48); emit(0xC7); emit(0x85);
            emitImm32(static_cast<uint32_t>(v.rbp_offset));
            emitImm32(static_cast<uint32_t>(value));
            break;
        }
    }
    void patch32(u32 offset, u32 value) {
        buf_[offset + 0] = (value >> 0) & 0xFF;
        buf_[offset + 1] = (value >> 8) & 0xFF;
        buf_[offset + 2] = (value >> 16) & 0xFF;
        buf_[offset + 3] = (value >> 24) & 0xFF;
    }

    // Patch the SUB RSP imm32 in the prologue with the actual frame size.
    void patch_prologue() {
        // Total bytes needed: magnitude of the most-negative offset, rounded
        // up to 16 so RSP stays aligned before any nested CALL.
        frame_size_ = align16(static_cast<uint32_t>(-current_offset_));

        // Windows ABI: include 32-byte shadow space in the allocation if the
        // function makes any calls.  We always reserve it for safety.
        frame_size_ += 32;
        frame_size_ = align16(frame_size_);  // re-align after adding shadow

        uint8_t* p = buf_.data() + sub_rsp_offset_ + 3; // skip 48 81 EC
        p[0] = (frame_size_) & 0xFF;
        p[1] = (frame_size_ >> 8) & 0xFF;
        p[2] = (frame_size_ >> 16) & 0xFF;
        p[3] = (frame_size_ >> 24) & 0xFF;
    }

    void flush_() {
        buf_.clear();
        vars_.clear();
        current_offset_ = 0;
        frame_size_ = 0;
        sub_rsp_offset_ = 0;
        prologue_emitted_ = false;
        epilogue_emitted_ = false;
        labels.clear();
        patches.clear();
        instr_starts.clear();
    }

    // META DATA 
    std::vector<u8> buf_;
    std::unordered_map<u32, VarInfo> vars_;
    int32_t  current_offset_ = 0;   // grows negative as vars are declared
    int32_t  sub_rsp_offset_ = 0;   // byte index of the imm32 in SUB RSP
    uint32_t frame_size_ = 0;
    bool     prologue_emitted_ = false;
    bool     epilogue_emitted_ = false;
    std::unordered_map<u32, u32> labels;        // defined labels
    std::vector<std::pair<u32, u32>> patches;   // (patch_offset, label_name)

    std::vector<u64> instr_starts;
};