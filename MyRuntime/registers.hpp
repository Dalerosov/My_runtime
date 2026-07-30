#pragma once
#include "pch.hpp"

// ── Register constants ───────────────────────────────────────────
enum class Reg {
    // 64-bit
    RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15,

    // 32-bit
    EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI,

    // 16-bit
    AX, CX, DX, BX, SP, BP, SI, DI,

    // 8-bit
    AL, CL, DL, BL, AH, CH, DH, BH,

    NO_REG
};
inline Reg gpr32[] = { Reg::EAX, Reg::ECX, Reg::EBX, Reg::EDX };
inline Reg gpr64[] = { Reg::RAX, Reg::RCX, Reg::RBX, Reg::RDX, Reg::RSI, Reg::RDI, Reg::R8, Reg::R9 };
inline Reg get_32_reg(u8 index) {
    return gpr32[index];
}
inline Reg get_64_reg(u8 index) {
    return gpr64[index];
}


enum class RegSize { Byte, Word, DWord, QWord };

// ── Register descriptor ──────────────────────────────────────────
struct RegInfo {
    u8 id;
    bool    ext;
    RegSize size;
};

static const RegInfo reg_table[] = {
    // 64-bit (index matches Reg enum)
    { 0, false, RegSize::QWord }, // RAX
    { 1, false, RegSize::QWord }, // RCX
    { 2, false, RegSize::QWord }, // RDX
    { 3, false, RegSize::QWord }, // RBX
    { 4, false, RegSize::QWord }, // RSP
    { 5, false, RegSize::QWord }, // RBP
    { 6, false, RegSize::QWord }, // RSI
    { 7, false, RegSize::QWord }, // RDI
    { 8, true,  RegSize::QWord }, // R8
    { 9, true,  RegSize::QWord }, // R9
    { 10, true, RegSize::QWord }, // R10
    { 11, true, RegSize::QWord }, // R11
    { 12, true, RegSize::QWord }, // R12
    { 13, true, RegSize::QWord }, // R13
    { 14, true, RegSize::QWord }, // R14
    { 15, true, RegSize::QWord }, // R15

    // 32-bit
    { 0, false, RegSize::DWord }, // EAX
    { 1, false, RegSize::DWord }, // ECX
    { 2, false, RegSize::DWord }, // EDX
    { 3, false, RegSize::DWord }, // EBX
    { 4, false, RegSize::DWord }, // ESP
    { 5, false, RegSize::DWord }, // EBP
    { 6, false, RegSize::DWord }, // ESI
    { 7, false, RegSize::DWord }, // EDI
    { 8, true,  RegSize::DWord }, // R8D
    { 9, true,  RegSize::DWord }, // R9D
    { 10, true, RegSize::DWord }, // R10D
    { 11, true, RegSize::DWord }, // R11D
    { 12, true, RegSize::DWord }, // R12D
    { 13, true, RegSize::DWord }, // R13D
    { 14, true, RegSize::DWord }, // R14D
    { 15, true, RegSize::DWord }, // R15D

    // 16-bit
    { 0, false, RegSize::Word }, // AX
    { 1, false, RegSize::Word }, // CX
    { 2, false, RegSize::Word }, // DX
    { 3, false, RegSize::Word }, // BX
    { 4, false, RegSize::Word }, // SP
    { 5, false, RegSize::Word }, // BP
    { 6, false, RegSize::Word }, // SI
    { 7, false, RegSize::Word }, // DI
    { 8, true,  RegSize::Word }, // R8W
    { 9, true,  RegSize::Word }, // R9W
    { 10, true, RegSize::Word }, // R10W
    { 11, true, RegSize::Word }, // R11W
    { 12, true, RegSize::Word }, // R12W
    { 13, true, RegSize::Word }, // R13W
    { 14, true, RegSize::Word }, // R14W
    { 15, true, RegSize::Word }, // R15W

    // 8-bit (low byte) — no REX prefix needed for AL/CL/DL/BL
    { 0, false, RegSize::Byte }, // AL
    { 1, false, RegSize::Byte }, // CL
    { 2, false, RegSize::Byte }, // DL
    { 3, false, RegSize::Byte }, // BL
    { 4, false, RegSize::Byte }, // SPL  (requires REX prefix to access as low byte)
    { 5, false, RegSize::Byte }, // BPL  (requires REX prefix)
    { 6, false, RegSize::Byte }, // SIL  (requires REX prefix)
    { 7, false, RegSize::Byte }, // DIL  (requires REX prefix)
    { 8, true,  RegSize::Byte }, // R8B
    { 9, true,  RegSize::Byte }, // R9B
    { 10, true, RegSize::Byte }, // R10B
    { 11, true, RegSize::Byte }, // R11B
    { 12, true, RegSize::Byte }, // R12B
    { 13, true, RegSize::Byte }, // R13B
    { 14, true, RegSize::Byte }, // R14B
    { 15, true, RegSize::Byte }, // R15B

    // 8-bit high byte (AH/CH/DH/BH) — encoding 4–7 without a REX prefix
    // These conflict with SPL/BPL/SIL/DIL; which one applies depends on REX presence
    //{ 4, false, RegSize::ByteH }, // AH
    //{ 5, false, RegSize::ByteH }, // CH
    //{ 6, false, RegSize::ByteH }, // DH
    //{ 7, false, RegSize::ByteH }, // BH
};
inline const RegInfo& regInfo(Reg r) {
    return reg_table[static_cast<int>(r)];
}

inline uint8_t regId(Reg r) { return regInfo(r).id & 0b111; }
inline bool needsREX(Reg r) { return regInfo(r).ext; }
inline RegSize regSize(Reg r) { return regInfo(r).size; }