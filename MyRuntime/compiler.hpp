#pragma once
#include <iostream>
#include "pch.hpp"
#include "emitter.hpp"
#include "emitter.hpp"

class Compiler {
        const std::shared_ptr<ILProgram> program;
        const std::shared_ptr<Emitter> emitter;
        const std::vector<u64> instr_start;
    public:
        Compiler(const std::shared_ptr<ILProgram> prog, const std::shared_ptr<Emitter> e) : program(prog), emitter(e) {}

        void compile_eager() {
            // first compile main
            
            for (ILFunction& f : program->functions) {
                compile_fn(f);
            }
        }

        void compile_lazy() {

        }

    private:
        std::unordered_map<u32, ILType> vreg_types;

        static bool is_signed(ILType t) { return t.kind <= ILTypeKind::I64; }

        void load_operand(const Operand& o, u32 sz, u8 slot) {
            emitter->mark_instr();
            if (o.is_imm())
                emitter->emit_mov_imm_reg(reg_for_size(sz, slot), o.imm_int);
            else
                emitter->mov_rm(reg_for_size(sz, slot), emitter->var(o.reg).rbp_offset);
        }
        void compile_fn(ILFunction& fn) {
            vreg_types.clear();
            u8 reg_index{};
            const std::vector<u8> param_registers{ 1, 2, 8, 9 };
            emitter->emit_prologue(reinterpret_cast<const volatile void*>(&(fn.call_count)));
            for (u32 p = 0; p < fn.param_types.size() && p < 4; p++) {
                emitter->declare_var(fn.param_types[p].reg, fn.param_types[p].type.size_bytes());
                emitter->mark_instr();
                emitter->emit_store_reg(fn.param_types[p].reg, param_registers[p]);
                vreg_types[fn.param_types[p].reg] = fn.param_types[p].type;
            }
            bool ex = false;
            for (auto& block : fn.blocks) {
                if (ex) { continue; }
                emitter->emit_label(block.id);
                for (u64 instr_index = 0; instr_index < block.instrs.size(); instr_index++) {
                    auto const& instr = block.instrs[instr_index];
                    reg_index = 0;
                    // declare dst var for any instruction that produces a value
                    if (instr.dst.valid() && !emitter->contains_var(instr.dst.id)) {
                        emitter->declare_var(instr.dst.id, instr.dst.type.size_bytes());
                        vreg_types[instr.dst.id] = instr.dst.type;
                    }
                    switch (instr.op) {
                    case Op::Copy:
                        if (instr.src[0].is_imm()) {
                            emitter->mark_instr();
                            emitter->emit_store_imm_stack(instr.dst.id, instr.src[0].imm_int);
                        }
                        else {
                            emitter->mark_instr();
                            emitter->mov_rm(reg_for_size(instr.dst.type.size_bytes(), 0), emitter->var(instr.src[0].reg).rbp_offset);
                            VarInfo dst = emitter->var(instr.dst.id);
                            emitter->mark_instr();
                            emitter->mov_mr(dst.rbp_offset, reg_for_size(instr.dst.type.size_bytes(), 0));
                        }
                        break;
                    case Op::Sub:
                    case Op::Add:
                    case Op::Mul:
                    case Op::Div:
                    case Op::Rem:

                        //load into registers
                        if (instr.src[0].is_imm()) {
                            emitter->mark_instr();
                            emitter->emit_mov_imm_reg(reg_for_size(instr.dst.type.size_bytes(), 0), instr.src[0].imm_int);
                        }
                        else {
                            emitter->mark_instr();
                            emitter->mov_rm(reg_for_size(instr.dst.type.size_bytes(), 0), emitter->var(instr.src[0].reg).rbp_offset);
                        }

                        if (instr.src[1].is_imm()) {
                            emitter->mark_instr();
                            emitter->emit_mov_imm_reg(reg_for_size(instr.dst.type.size_bytes(), 1), instr.src[1].imm_int);
                        }
                        else {
                            emitter->mark_instr();
                            emitter->mov_rm(reg_for_size(instr.dst.type.size_bytes(), 1), emitter->var(instr.src[1].reg).rbp_offset);
                        }
                        VarInfo dst = emitter->var(instr.dst.id);
                        emitter->mark_instr();
                        //opp
                        if (instr.op == Op::Add) {
                            emitter->add_rr(reg_for_size(instr.dst.type.size_bytes(), 0), reg_for_size(instr.dst.type.size_bytes(), 1));
                        }
                        else if (instr.op == Op::Sub) {
                            
                            emitter->sub_rr(reg_for_size(instr.dst.type.size_bytes(), 0), reg_for_size(instr.dst.type.size_bytes(), 1));
                        }
                        else if (instr.op == Op::Mul) {
                            emitter->mul_rr(reg_for_size(instr.dst.type.size_bytes(), 0), reg_for_size(instr.dst.type.size_bytes(), 1));
                        }
                        else if (instr.op == Op::Div || instr.op == Op::Rem) {
                            emitter->div_rr(reg_for_size(instr.dst.type.size_bytes(), 0), reg_for_size(instr.dst.type.size_bytes(), 1));
                        }

                        // save back to stack
                        emitter->mark_instr();
                        if (instr.op == Op::Rem) {
                            emitter->mov_mr(dst.rbp_offset, reg_for_size(instr.dst.type.size_bytes(), 3));
                            break;
                        }
                        emitter->mov_mr(dst.rbp_offset, reg_for_size(instr.dst.type.size_bytes(), 0));
                        break;
                    
                    case Op::Jmp:
                        emitter->mark_instr();
                        emitter->emit_jmp(instr.block_target);
                        break;
                    case Op::Jz:
                    case Op::Jnz:
                    case Op::Js:
                    {
                        u32 sz = instr.src[0].is_reg() ? emitter->var(instr.src[0].reg).size : 4;
                        if (instr.src[0].is_reg() && sz < 4) {
                            emitter->mark_instr();
                            emitter->movzx_eax_mem(emitter->var(instr.src[0].reg).rbp_offset, sz);
                            sz = 4;
                        }
                        else {
                            load_operand(instr.src[0], sz, 0);
                        }
                        emitter->mark_instr();
                        emitter->emit_test_reg(reg_for_size(sz, 0));
                        emitter->mark_instr();
                        Emitter::Cond cc = instr.op == Op::Jz ? Emitter::Cond::E
                            : instr.op == Op::Jnz ? Emitter::Cond::NE
                            : Emitter::Cond::S;
                        emitter->emit_jcc(cc, instr.block_target);
                        break;
                    }

                    case Op::Eq: case Op::Ne:
                    case Op::Lt: case Op::Le:
                    case Op::Gt: case Op::Ge:
                    {
                        // operand type comes from whichever source is a vreg
                        ILType t = instr.src[0].is_reg() ? vreg_types.at(instr.src[0].reg)
                            : instr.src[1].is_reg() ? vreg_types.at(instr.src[1].reg)
                            : ILTypes::I32;
                        u32  sz = t.size_bytes();
                        bool sgn = is_signed(t);

                        load_operand(instr.src[0], sz, 0);
                        load_operand(instr.src[1], sz, 1);
                        emitter->mark_instr();
                        emitter->cmp_rr(reg_for_size(sz, 0), reg_for_size(sz, 1));

                        Emitter::Cond cc;
                        switch (instr.op) {
                        case Op::Eq: cc = Emitter::Cond::E;  break;
                        case Op::Ne: cc = Emitter::Cond::NE; break;
                        case Op::Lt: cc = sgn ? Emitter::Cond::L : Emitter::Cond::B;  break;
                        case Op::Le: cc = sgn ? Emitter::Cond::LE : Emitter::Cond::BE; break;
                        case Op::Gt: cc = sgn ? Emitter::Cond::G : Emitter::Cond::A;  break;
                        default:     cc = sgn ? Emitter::Cond::GE : Emitter::Cond::AE; break;  // Ge
                        }

                        emitter->mark_instr(); emitter->setcc(cc);
                        emitter->mark_instr(); emitter->movzx_eax_al();
                        emitter->mark_instr(); emitter->emit_store_reg(instr.dst.id, 0);  // size-aware store of EAX
                        break;
                    }
                    case Op::Call:
                    {
                        ILFunction& callee = program->functions[instr.fn_id];
                        auto args = fn.get_args(instr.param_info[0], instr.param_info[1]); // by value, not auto&

                        for (u32 p = 0; p < args.size() && p < 4; p++) {
                            u32 sz = callee.param_types[p].type.size_bytes();
                            emitter->mark_instr();
                            emitter->mov_rm(reg_for_size(sz, param_registers[p]),
                                emitter->var(args[p]).rbp_offset);
                        }

                        emitter->mark_instr();
                        emitter->emit_mov_imm_reg(Reg::RAX, reinterpret_cast<u64>(&callee.jit_code));
                        emitter->mark_instr();
                        emitter->emit({ 0xFF, 0x10 });                 // call qword ptr [rax]

                        if (instr.dst.valid() && !callee.return_type.is_void()) {
                            if (!emitter->contains_var(instr.dst.id))
                                emitter->declare_var(instr.dst.id, instr.dst.type.size_bytes());
                            emitter->mark_instr();
                            emitter->emit_store_reg(instr.dst.id, 0);  // RAX
                        }
                        break;
                    }
                    case Op::Ret:
                        if (instr.src_count > 0) {
                            emitter->mark_instr();
                            if (instr.src[0].is_reg())
                                emitter->mov_rm(Reg::RAX, emitter->var(instr.src[0].reg).rbp_offset);   // size-aware if you return non-i32
                            else
                                emitter->emit_mov_imm_reg(Reg::RAX, instr.src[0].imm_int);
                        }

                        emitter->emit_epilogue();
                        //ex = true;
                        break;
                    }
                    
                }
            }
            
            fn.assign_jit_code(emitter->commit());
        }

        static Reg reg_for_size(u8 size, u8 index)
        {
            switch (size * 8) {
            case 32:
                return get_32_reg(index);
            case 64:
                return get_64_reg(index);
            default:
                throw std::logic_error("reg_for_size: unsupported size");
            }
        }

        
};