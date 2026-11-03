#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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

enum class VarSize : uint8_t {
    BYTE = 1,
    WORD = 2,
    DWORD = 4,
    QWORD = 8,
};

struct VarInfo {
    int32_t  rbp_offset; // negative offset from RBP  (e.g. -4 for first DWORD)
    VarSize  size;
};

class StackFrameEmitter {
public:
    // -----------------------------------------------------------------------
    // Prologue: push rbp / mov rbp,rsp / sub rsp,<placeholder>
    // The SUB immediate is patched when emit_epilogue() (or patch_prologue())
    // is called, once the total frame size is known.
    // -----------------------------------------------------------------------
    void emit_prologue() {
        if (prologue_emitted_)
            throw std::logic_error("emit_prologue() already called");

        // push rbp                      ; 55
        emit8(0x55);

        // mov rbp, rsp                  ; 48 89 E5
        emit8(0x48); emit8(0x89); emit8(0xE5);

        // sub rsp, imm32  (placeholder) ; 48 81 EC [00 00 00 00]
        sub_rsp_offset_ = static_cast<int32_t>(code_.size());
        emit8(0x48); emit8(0x81); emit8(0xEC);
        emit32(0x00000000);   // patched later

        prologue_emitted_ = true;
    }

    // -----------------------------------------------------------------------
    // Declare a named local variable.
    // Returns the VarInfo (rbp offset + size) for use in subsequent emits.
    // -----------------------------------------------------------------------
    VarInfo declare_var(const std::string& name, VarSize size) {
        if (!prologue_emitted_)
            throw std::logic_error("call emit_prologue() first");
        if (epilogue_emitted_)
            throw std::logic_error("cannot declare vars after emit_epilogue()");
        if (vars_.count(name))
            throw std::invalid_argument("duplicate variable name: " + name);

        uint8_t sz = static_cast<uint8_t>(size);

        // Align the next offset to the variable's natural alignment
        // (BYTE=1, WORD=2, DWORD=4, QWORD=8).
        current_offset_ = align_down(current_offset_ - sz, sz);

        VarInfo info{ current_offset_, size };
        vars_[name] = info;

        // Emit a comment-style NOP sequence so the byte stream stays readable
        // while also encoding a MOV [rbp+disp32], 0  (zero-initialise).
        emit_zero_init(info);

        return info;
    }

    // -----------------------------------------------------------------------
    // Look up a previously declared variable.
    // -----------------------------------------------------------------------
    VarInfo var(const std::string& name) const {
        auto it = vars_.find(name);
        if (it == vars_.end())
            throw std::invalid_argument("unknown variable: " + name);
        return it->second;
    }

    // -----------------------------------------------------------------------
    // Emit  MOV [rbp + offset], imm   for a variable (32-bit immediate max).
    // -----------------------------------------------------------------------
    void emit_store_imm(const std::string& name, int32_t value) {
        VarInfo v = var(name);
        switch (v.size) {
        case VarSize::BYTE:
            // C6 85 [disp32] [imm8]
            emit8(0xC6); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit8(static_cast<uint8_t>(value));
            break;
        case VarSize::WORD:
            // 66 C7 85 [disp32] [imm16]
            emit8(0x66); emit8(0xC7); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit16(static_cast<uint16_t>(value));
            break;
        case VarSize::DWORD:
            // C7 85 [disp32] [imm32]
            emit8(0xC7); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit32(static_cast<uint32_t>(value));
            break;
        case VarSize::QWORD:
            // Move imm32 sign-extended into qword:
            // 48 C7 85 [disp32] [imm32]
            emit8(0x48); emit8(0xC7); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit32(static_cast<uint32_t>(value));
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

        // leave   ; C9
        emit8(0xC9);
        // ret     ; C3
        emit8(0xC3);

        epilogue_emitted_ = true;
    }

    // -----------------------------------------------------------------------
    // Return the raw machine-code bytes.
    // -----------------------------------------------------------------------
    const std::vector<uint8_t>& code() const { return code_; }

    // -----------------------------------------------------------------------
    // Hex dump — handy for debugging.
    // -----------------------------------------------------------------------
    std::string hex_dump() const {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(code_.size() * 3);
        for (uint8_t b : code_) {
            out += hex[b >> 4];
            out += hex[b & 0xF];
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

    // Internal store — same as emit_store_imm but takes VarInfo directly.
    void emit_store_imm_raw(const VarInfo& v, int32_t value) {
        switch (v.size) {
        case VarSize::BYTE:
            emit8(0xC6); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit8(static_cast<uint8_t>(value));
            break;
        case VarSize::WORD:
            emit8(0x66); emit8(0xC7); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit16(static_cast<uint16_t>(value));
            break;
        case VarSize::DWORD:
            emit8(0xC7); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit32(static_cast<uint32_t>(value));
            break;
        case VarSize::QWORD:
            emit8(0x48); emit8(0xC7); emit8(0x85);
            emit32(static_cast<uint32_t>(v.rbp_offset));
            emit32(static_cast<uint32_t>(value));
            break;
        }
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

        uint8_t* p = code_.data() + sub_rsp_offset_ + 3; // skip 48 81 EC
        p[0] = (frame_size_) & 0xFF;
        p[1] = (frame_size_ >> 8) & 0xFF;
        p[2] = (frame_size_ >> 16) & 0xFF;
        p[3] = (frame_size_ >> 24) & 0xFF;
    }

    void emit8(uint8_t  v) { code_.push_back(v); }
    void emit16(uint16_t v) { emit8(v & 0xFF); emit8(v >> 8); }
    void emit32(uint32_t v) {
        emit8(v & 0xFF);
        emit8((v >> 8) & 0xFF);
        emit8((v >> 16) & 0xFF);
        emit8((v >> 24) & 0xFF);
    }

    std::vector<uint8_t>                    code_;
    std::unordered_map<std::string, VarInfo> vars_;
    int32_t  current_offset_ = 0;   // grows negative as vars are declared
    int32_t  sub_rsp_offset_ = 0;   // byte index of the imm32 in SUB RSP
    uint32_t frame_size_ = 0;
    bool     prologue_emitted_ = false;
    bool     epilogue_emitted_ = false;
};