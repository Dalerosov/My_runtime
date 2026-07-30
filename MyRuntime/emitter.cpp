#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "pch.hpp"
#include "emitter.hpp"

JitCode Emitter::commit() {
    size_t size = buf_.size();

    // allocate RW first — never RWX, some AV software kills RWX pages
    void* mem = VirtualAlloc(nullptr, size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!mem) return { nullptr, 0 };

    memcpy(mem, buf_.data(), size);

    // flip to RX
    DWORD old;
    VirtualProtect(mem, size, PAGE_EXECUTE_READ, &old);

    return { mem, buf_.size() };
}

void Emitter::free_code(void* mem, size_t size) {
    VirtualFree(mem, 0, MEM_RELEASE);
}



void Emitter::emit_mov_imm_reg(Reg dst, uint64_t value)
{
    uint8_t rex = 0x48; // REX.W

    //if (needsREX(dst))
    //    rex |= 0x01;    // REX.B

    if (needsREX(dst)) { emit(rex); };
    emit(0xB8 + (regId(dst) & 7));
    auto size = regInfo(dst).size;
    switch (size) {
    case RegSize::Byte:
        emit(value);
        return;
    case RegSize::Word:
        emitImm16(value);
        return;
    case RegSize::DWord:
        emitImm32(value);
        return;
    }
    emitImm64(value);
}
void Emitter::push_r(Reg src) {
    if (needsREX(src)) emit(0x41);       // REX.B for Byte–R15
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
    if (needsREX(dst)) emit(0x41);  // REX.B for Byte–R15
    emit(0x58 | regId(dst));        // 0x58+rd
}

void Emitter::mov_rr(Reg dst, Reg src) {
    u8 rex = 0x48;                    // REX.W
    if (needsREX(src)) rex |= 0x01;        // REX.B — src in r/m field
    if (needsREX(dst)) rex |= 0x04;        // REX.R — dst in reg field
    emit(rex);
    emit(0x89);                            // MOV r/m64, QWord
    emit(0xC0 | (regId(src) << 3) | regId(dst));  // ModRM
}

void Emitter::add_ri(Reg dst, i64 imm) {
    u8 rex = 0x48;
    if (needsREX(dst)) rex |= 0x01;

    if (imm >= -128 && imm <= 127) {
        emit(rex);
        emit(0x83);
        emit(0xC0 | regId(dst));
        emit(static_cast<u8>(imm));
    }
    else if (imm >= INT32_MIN && imm <= INT32_MAX) {
        emit(rex);
        emit(0x81);
        emit(0xC0 | regId(dst));
        emitImm32(static_cast<i32>(imm));
    }
    else {
        // no add QWord, imm64 encoding — use scratch register
        // save rax if dst is not rax
        if (dst != Reg::RAX) {
            push_r(Reg::RAX);
        }
        // mov rax, imm64
        emit(0x48);
        emit(0xB8);
        emitImm64(imm);
        // add dst, rax
        add_rr(dst, Reg::RAX);
        if (dst != Reg::RAX) {
            pop_r(Reg::RAX);
        }
    }
}

void Emitter::mov_rm(Reg dst, int32_t rbp_offset) {
    uint8_t rex = 0x48;
    //if (needsREX(dst)) rex |= 0x04; // REX.R extends reg (dst)
    if (needsREX(dst)) { emit(rex); }
    emit(0x8B);                                          // MOV r64, r/m64
    emit(0x80 | (regId(dst) << 3) | 0x05);              // ModRM: mod=10, reg=dst, r/m=RBP
    emitImm32(rbp_offset);
}

void Emitter::mov_mr(int32_t rbp_offset, Reg src) {
    uint8_t rex = 0x48;
    //if (needsREX(src)) rex |= 0x04; // REX.R extends reg (src)
    if (needsREX(src)) { emit(rex); }
    emit(0x89);                          // MOV r/m64, r64
    emit(0x85 | (regId(src) << 3));      // ModRM: mod=10 reg=src r/m=101 (RBP+disp32)
    emitImm32(rbp_offset);
}

void Emitter::add_rr(Reg dst, Reg src) {
    u8 rex = 0x48;                // REX.W
    //if (needsREX(src)) rex |= 0x01;   // REX.B
    //if (needsREX(dst)) rex |= 0x04;   // REX.R
    if (needsREX(src)) { emit(rex); }
    emit(0x01);                        // ADD r/m64, QWord
    emit(0xC0 | (regId(src) << 3) | regId(dst));  // ModRM
}

void Emitter::sub_rr(Reg dst, Reg src) {
    uint8_t rex = 0x48;
    //if (needsREX(src)) rex |= 0x01;   // REX.B
   //if (needsREX(dst)) rex |= 0x04;   // REX.R
    if (needsREX(src)) { emit(rex); }
    emit(0x29);                                        // SUB r/m64, QWord
    emit(0xC0 | (regId(src) << 3) | regId(dst));
}

void Emitter::sub_ri(Reg dst, i64 imm) {
    u8 rex = 0x48;
    if (needsREX(dst)) rex |= 0x01;

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
    if (needsREX(src)) rex |= 0x01;
    if (needsREX(dst)) rex |= 0x04;
    emit(rex);
    emit(0x0F);
    emit(0xAF);
    emit(0xC0 | (regId(dst) << 3) | regId(src));
}

void Emitter::cmp_rr(Reg a, Reg b) {
    u8 rex = 0x48;
    if (needsREX(b)) rex |= 0x01;
    if (needsREX(a)) rex |= 0x04;
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
    if (needsREX(r)) emit(0x41);
    emit(0xFF);
    emit(0xD0 | regId(r));
}

// RET
void Emitter::ret() { emit(0xC3); }