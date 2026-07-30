#include "pch.hpp"
//#include "il.hpp"
#include "runtime.hpp"
#include <iostream>



int main() {

    std::unique_ptr<Runtime> rt = std::make_unique<Runtime>();

    rt->Run();

    return 0;

    //ILProgram program;

    //// build: i32 add(i32 a, i32 b) { return a + b; }
    //auto& fn = program.add_function("add", ILTypes::I32, { ILTypes::I32, ILTypes::I32 });

    //// params come in as regs 0 and 1 by convention
    //V_reg a = fn.alloc_reg(); // reg 0 = param a
    //V_reg b = fn.alloc_reg(); // reg 1 = param b

    //auto  result = fn.alloc_typed_reg(ILTypes::I32);
    //auto& bb = fn.entry();

    //bb.instrs.push_back(Instr::make_binop(
    //    Op::Add, result,
    //    Operand::from_reg(a),
    //    Operand::from_reg(b)
    //));
    //bb.instrs.push_back(Instr::make_ret(Operand::from_reg(result.id)));

    //assert(bb.is_terminated());

    //std::cout << "IL looks good\n";
}