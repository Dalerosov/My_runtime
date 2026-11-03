#pragma once
#include "pch.hpp"
#include 

class Interpreter {
    TypeTable& types;

    // execution state per frame
    struct Frame {
        ILFunction* fn;
        std::vector<Value>   regs;   // indexed by reg_id
        u32                  bb;     // current basic block
        u32                  ip;     // instruction index within bb
    };

    std::vector<Frame> call_stack;

public:
    Value execute(ILFunction* fn, std::span<Value> args);
};