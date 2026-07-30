#pragma once
#include <iostream>
#include "pch.hpp"
#include "emitter.hpp"

class Compiler {
        const std::shared_ptr<ILProgram> program;
        const std::shared_ptr<Emitter> emitter;
    public:
        Compiler(const std::shared_ptr<ILProgram> prog, const std::shared_ptr<Emitter> e) : program(prog), emitter(e) {}

        void compile() {
            // first compile main
            compile_fn(program->functions[0]);
        }

    private:
        void compile_fn(ILFunction& fn) {
            u8 reg_index{};
            const std::vector<u8> param_registers{ 1, 2, 8, 9 };
            emitter->emit_prologue();
            for (u32 p = 0; p < fn.param_types.size() && p < 4; p++) {
                emitter->declare_var(fn.param_types[p].reg, fn.param_types[p].type.size_bytes());
                emitter->emit_store_reg(fn.param_types[p].reg, param_registers[p]);
            }
            bool ex = false;
            for (auto block : fn.blocks) {
                if (ex) { break; }
                emitter->emit_label(block.id);
                for (auto const &instr : block.instrs) {
                    reg_index = 0;
                    // declare dst var for any instruction that produces a value
                    if (instr.dst.valid() && !emitter->contains_var(instr.dst.id)) {
                        emitter->declare_var(instr.dst.id, instr.dst.type.size_bytes());
                    }
                    switch (instr.op) {
                    case Op::Copy:
                        if (instr.src[0].is_imm()) {
                            emitter->emit_store_imm_stack(instr.dst.id, instr.src[0].imm_int);
                        }
                        else {
                            emitter->mov_rm(reg_for_size(instr.dst.type.size_bytes(), 0), emitter->var(instr.src[0].reg).rbp_offset);
                            VarInfo dst = emitter->var(instr.dst.id);
                            emitter->mov_mr(dst.rbp_offset, reg_for_size(instr.dst.type.size_bytes(), 0));
                        }
                        break;
                    case Op::Sub:
                    case Op::Add:

                        //load into registers
                        if (instr.src[0].is_imm()) {
                            emitter->emit_mov_imm_reg(reg_for_size(instr.dst.type.size_bytes(), 0), instr.src[0].imm_int);
                        }
                        else {
                            emitter->mov_rm(reg_for_size(instr.dst.type.size_bytes(), 0), emitter->var(instr.src[0].reg).rbp_offset);
                        }

                        if (instr.src[1].is_imm()) {
                            emitter->emit_mov_imm_reg(reg_for_size(instr.dst.type.size_bytes(), 1), instr.src[1].imm_int);
                        }
                        else {
                            emitter->mov_rm(reg_for_size(instr.dst.type.size_bytes(), 1), emitter->var(instr.src[1].reg).rbp_offset);
                        }
                        VarInfo dst = emitter->var(instr.dst.id);
                        
                        //opp
                        if (instr.op == Op::Add) {
                            emitter->add_rr(reg_for_size(instr.dst.type.size_bytes(), 0), reg_for_size(instr.dst.type.size_bytes(), 1));
                        }
                        else if (instr.op == Op::Sub) {
                            emitter->sub_rr(reg_for_size(instr.dst.type.size_bytes(), 0), reg_for_size(instr.dst.type.size_bytes(), 1));
                        }

                        // save back to stack
                        emitter->mov_mr(dst.rbp_offset, reg_for_size(instr.dst.type.size_bytes(), 0));
                        break;
                    
                    case Op::Jmp:
                        emitter->emit_jmp(instr.block_target);
                        break;
                    case Op::Jz:
                        emitter->mov_rm(reg_for_size(8, 0), emitter->var(instr.src[0].reg).rbp_offset);
                        emitter->emit_test_reg(reg_for_size(8, 0));
                        emitter->emit_jz(instr.block_target);
                        break;
                    case Op::Ret:
                        if (!(instr.src->kind == Operand::Kind::Empty)) {
                            // TODO fix so it doesnt just put in rax might need to rewrite how operand works

                            emitter->mov_rm(reg_for_size(instr.dst.type.size_bytes(), 0), emitter->var(instr.src[0].reg).rbp_offset);
                        }
                        emitter->emit_epilogue();
                        ex = true;
                        break;
                    }
                    
                }
            }
            
            fn.assign_jit_code(emitter->commit());
            std::cout << emitter->hex_dump();
        }

        static Reg reg_for_size(u8 size, u8 index)
        {
            switch (size * 8) {
            case 32:
                return get_32_reg(index);
            case 64:
                return get_64_reg(index);
            default:
                return Reg::RAX;
            }
        }
};