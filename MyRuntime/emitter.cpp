#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "pch.hpp"
#include "emitter.hpp"

JitCode Emitter::commit() {
    size_t size = buf_.size();
    std::vector<u64> is = instr_starts;

    // allocate RW first — never RWX, some AV software kills RWX pages
    void* mem = VirtualAlloc(nullptr, size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!mem) return { nullptr, 0 };

    memcpy(mem, buf_.data(), size);

    // flip to RX
    DWORD old;
    VirtualProtect(mem, size, PAGE_EXECUTE_READ, &old);
    //std::cout << hex_dump() << "\n\n";
    flush_();
    return { mem, size, is };
}

void Emitter::free_code(void* mem, size_t size) {
    VirtualFree(mem, 0, MEM_RELEASE);
}



void Emitter::emit_mov_imm_reg(Reg dst, uint64_t value)
{
    const int     id = regId(dst);
    const bool    ext = id >= 8;      // r8..r15 -> REX.B
    const uint8_t low = id & 7;

    switch (regInfo(dst).size) {
    case RegSize::Byte:
        if (ext || (id >= 4 && id < 8))   // r8b+ or spl/bpl/sil/dil
            emit(0x40 | ext);
        emit(0xB0 + low);
        emit(uint8_t(value));
        return;
    case RegSize::Word:
        emit(0x66);
        if (ext) emit(0x41);
        emit(0xB8 + low);
        emitImm16(value);
        return;
    case RegSize::DWord:
        if (ext) emit(0x41);
        emit(0xB8 + low);
        emitImm32(value);
        return;
    case RegSize::QWord:
        break;
    }

    if (int64_t(value) == int32_t(value)) { // mov r/m64, simm32
        emit(0x48 | ext);
        emit(0xC7);
        emit(0xC0 | low);
        emitImm32(value);
    }
    else {                                       // movabs
        emit(0x48 | ext);
        emit(0xB8 + low);
        emitImm64(value);
    }
}
void Emitter::push_r(Reg src) {
    if (isExt(src)) emit(0x41);       // REX.B for Byte–R15
    emit(0x50 | regId(src));             // 0x50+rd
}

// Care with 64 bit numbers, will clobber RAX
void Emitter::push_i(i64 imm) {
    if (imm <= 0x7F) {
        emit(0x6A);
        emit(static_cast<u8>(imm));
    }
    else if (imm <= 0x7FFFFFFF) {
        emit(0x68);
        emitImm32(static_cast<u32>(imm));
    }
    else {
        // MOV RAX, imm64 — 48 B8 + 8 bytes
        // 
        emit(0x48);
        emit(0xB8);
        emitImm64(imm);
        // PUSH RAX — 50
        emit(0x50);
    }
}

void Emitter::pop_r(Reg dst) {
    if (isExt(dst)) emit(0x41);  // REX.B for Byte–R15
    emit(0x58 | regId(dst));        // 0x58+rd
}

void Emitter::mov_rr(Reg dst, Reg src) {
    u8 rex = 0x48;                    // REX.W
    if (isExt(src)) rex |= 0x01;        // REX.B — src in r/m field
    if (isExt(dst)) rex |= 0x04;        // REX.R — dst in reg field
    emit(rex);
    emit(0x89);                            // MOV r/m64, QWord
    emit(0xC0 | (regId(src) << 3) | regId(dst));  // ModRM
}

void Emitter::add_ri(Reg dst, i64 imm) {
    const u8 rex = 0x48 | (isExt(dst) ? 0x01 : 0x00);
    const u8 rm = regId(dst) & 7;

    if (imm >= -128 && imm <= 127) {
        emit(rex); emit(0x83); emit(0xC0 | rm);          // /0 = ADD
        emit(static_cast<u8>(imm));
    }
    else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        emit(rex); emit(0x81); emit(0xC0 | rm);
        emitImm32(static_cast<i32>(imm));
    }
    else {
        assert(dst != Reg::RSP);
        const Reg s = (dst == Reg::RAX) ? Reg::RCX : Reg::RAX;
        push_r(s);
        emit(0x48 | (isExt(s) ? 0x01 : 0x00));            // movabs s, imm64
        emit(0xB8 | (regId(s) & 7));
        emitImm64(imm);
        add_rr(dst, s);
        pop_r(s);
    }
}

void Emitter::mov_rm(Reg dst, int32_t rbp_offset) {      // mov dst, [rbp+disp32]
    emit(0x48 | (isExt(dst) ? 0x04 : 0x00));             // REX.W + R
    emit(0x8B);
    emit(0x80 | ((regId(dst) & 7) << 3) | 0x05);         // mod=10, reg=dst, rm=rbp
    emitImm32(static_cast<uint32_t>(rbp_offset));
}

void Emitter::mov_mr(int32_t rbp_offset, Reg src) {      // mov [rbp+disp32], src
    emit(0x48 | (isExt(src) ? 0x04 : 0x00));
    emit(0x89);
    emit(0x80 | ((regId(src) & 7) << 3) | 0x05);
    emitImm32(static_cast<uint32_t>(rbp_offset));
}


void Emitter::emit_f7(u8 ext, Reg rm) {
    emit(0x48 | (isExt(rm) ? 0x01 : 0x00));   // only B, reg field isn't a register
    emit(0xF7);
    emit(0xC0 | (ext << 3) | (regId(rm) & 7));
}

void Emitter::mul_rr(Reg dst, Reg src) {
    uint8_t rex = 0x48; // REX.W

    if (isExt(dst)) rex |= 0x04; // REX.R: ModRM reg field
    if (isExt(src)) rex |= 0x01; // REX.B: ModRM r/m field

    emit(rex);
    emit(0x0F);
    emit(0xAF);
    emit(0xC0 | ((regId(dst) & 7) << 3) | (regId(src) & 7));
}
void Emitter::div_rr(Reg dst, Reg src) {    // unsigned, dst /= src
    emit_rr(0x89, Reg::RAX, dst);           // mov rax, dst
    emit(0x31); emit(0xD2);                 // xor edx, edx
    emit_f7(6, src);                        // div src
    emit_rr(0x89, dst, Reg::RAX);           // mov dst, rax
}

void Emitter::emit_rr(u8 opcode, Reg dst, Reg src) {
    emit(0x48 | (isExt(src) ? 0x04 : 0x00) | (isExt(dst) ? 0x01 : 0x00));
    emit(opcode);
    emit(0xC0 | ((regId(src) & 7) << 3) | (regId(dst) & 7));
}

void Emitter::add_rr(Reg dst, Reg src) { emit_rr(0x01, dst, src); }
void Emitter::sub_rr(Reg dst, Reg src) { emit_rr(0x29, dst, src); }

void Emitter::sub_ri(Reg dst, i64 imm) {
    u8 rex = 0x48;
    if (isExt(dst)) rex |= 0x01;

    if (imm >= -128 && imm <= 127) {
        emit(rex);
        emit(0x83);
        emit(0xE8 | regId(dst));                       // opcode ext 5 = SUB
        emit(static_cast<u8>(imm));
    }
    else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        emit(rex);
        emit(0x81);
        emit(0xE8 | regId(dst));                       // opcode ext 5 = SUB
        emitImm32(static_cast<i32>(imm));
    }
    else {
        if (dst != Reg::RAX) push_r(Reg::RAX);
        emit(0x48);
        emit(0xB8);
        emitImm64(imm);
        sub_rr(dst, Reg::RAX);
        if (dst != Reg::RAX) pop_r(Reg::RAX);
    }
}

void Emitter::imul_rr(Reg dst, Reg src) {
    u8 rex = 0x48;
    if (isExt(src)) rex |= 0x01;
    if (isExt(dst)) rex |= 0x04;
    emit(rex);
    emit(0x0F);
    emit(0xAF);
    emit(0xC0 | (regId(dst) << 3) | regId(src));
}

void Emitter::cmp_rr(Reg a, Reg b) {
    u8 rex = 0x48;
    if (isExt(b)) rex |= 0x01;
    if (isExt(a)) rex |= 0x04;
    emit(rex);
    emit(0x3B);
    emit(0xC0 | (regId(a) << 3) | regId(b));
}

void Emitter::jmp_rel32(i32 target) {
    emit(0xE9);
    auto it = labels.find(target);
    if (it != labels.end()) {
        // backward jump — target already known
        u32 rel = it->second - (current_offset() + 4);
        emitImm32(rel);
    }
    else {
        // forward jump — patch later
        patches.push_back({ current_offset(), target });
        emitImm32(0x00000000);
    }
}

void Emitter::je_rel32(i32 offset) {
    emit(0x0F);
    emit(0x84);
    emitImm32(offset);
}

void Emitter::call_r(Reg r) {
    if (isExt(r)) emit(0x41);
    emit(0xFF);
    emit(0xD0 | regId(r));
}

// RET
void Emitter::ret() { emit(0xC3); }