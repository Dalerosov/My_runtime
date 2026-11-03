#pragma once
#include <iostream>
#include <chrono>
#include "pch.hpp"
#include "emitter.hpp"
#include "loader.hpp"
#include "parser2.hpp"
#include "compiler.hpp"
#include "profiler.h"
#include "il.hpp"

class Runtime {
	bool lazy{};
	std::unique_ptr<Loader> loader;
	std::unique_ptr<Parser> parser;
	std::unique_ptr<Compiler> compiler;
    std::unique_ptr<Profiler> profiler;
	std::shared_ptr<Emitter> emitter;

	std::string file_location{ "D:/mylang/test.sil" };

public:
	Runtime(bool lazy = false) : lazy(lazy) {
		loader = std::make_unique<Loader>(file_location);
		parser = std::make_unique<Parser>();
        emitter = std::make_shared<Emitter>();
        
	}

	void Run() {
        auto start = std::chrono::high_resolution_clock::now();
		auto lines = loader->load();
        auto load = std::chrono::high_resolution_clock::now();
		auto il_representation = std::make_shared<ILProgram>(parser -> parse(lines));
        auto parse = std::chrono::high_resolution_clock::now();
        // Delete loader and parser
        loader.reset();
        parser.reset();

        std::cout << "Load: " << (std::chrono::duration<double, std::milli>) (load - start) << "; Parse: " << (std::chrono::duration<double, std::milli>) (parse - load) << "\n";

        for each (auto fun in il_representation->functions)
        {
            print_function(fun, il_representation);
        }
        std::cout << "\n\n";

        // START PROFILER 

        profiler = std::make_unique<Profiler>(il_representation);
        profiler->start();

        auto exec_start = std::chrono::high_resolution_clock::now();
        //begin cmp and exec
        if (!lazy) {
            Eager_comp(il_representation);
        }
        
        else {


        }
        auto exec_end = std::chrono::high_resolution_clock::now();
        profiler->end();
        std::cout << "Ran for: " << (std::chrono::duration<double, std::milli>) (exec_end - exec_start);
	}

    void Eager_comp(const std::shared_ptr<ILProgram> il_representation) {
        compiler = std::make_unique<Compiler>(il_representation, emitter);

        // Compile whole program
        compiler->compile_eager();

        for (auto& fu : il_representation->functions) {
            std::cout << hex_dump_raw(fu) << "\n\n";
        }
        std::cout << "\n\n\n";
        for (auto& fu : il_representation->functions) {
            std::cout << hex_dump(fu) << "\n\n";
        }

        // EXECUTE CODE
        ILFunction* main_fn = il_representation->find("main");
        if (!main_fn || !main_fn->jit_code) {
            std::cerr << "main not compiled\n";
            return;
        }

        using MainFn = u64(*)();
        u64 result = reinterpret_cast<MainFn>(main_fn->jit_code)();
        std::cout << "main returned: " << result << "\n";

        std::cout << "\n\n\n";
        for (auto& fu : il_representation->functions) {
            std::cout << fu.name << " called: " << fu.call_count << " times\n\n";
        }
    }

    std::string hex_dump(const ILFunction& fu) const {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        if (!fu.jit_code || fu.code_size == 0) return out;

        const auto* p = static_cast<const unsigned char*>(fu.jit_code);
        const auto& s = fu.instr_starts;

        auto line = [&](std::size_t begin, std::size_t end) {
            if (end > fu.code_size) end = fu.code_size;   // paranoia
            out += std::format("{:04X}: ", begin);
            for (std::size_t i = begin; i < end; ++i) {
                out += hex[p[i] >> 4];
                out += hex[p[i] & 0xF];
                out += ' ';
            }
            out += '\n';
            };

        if (s.empty()) {                       // nothing recorded, dump it all as one line
            line(0, fu.code_size);
            return out;
        }

        if (s[0] > 0) line(0, s[0]);           // prologue (bytes before the first mark)

        for (std::size_t i = 0; i < s.size(); ++i) {
            std::size_t end = (i + 1 < s.size()) ? s[i + 1] : fu.code_size;
            line(s[i], end);
        }
        return out;
    }

    std::string hex_dump_raw(const ILFunction& fu) const {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        if (!fu.jit_code || fu.code_size == 0) return out;

        const auto* p = static_cast<const unsigned char*>(fu.jit_code);


        out.reserve(fu.code_size * 3);
        for (std::size_t i = 0; i < fu.code_size; i++) {
            out += hex[p[i] >> 4];
            out += hex[p[i] & 0xF];
            out += ' ';
        }
        if (!out.empty()) out.pop_back();
        return out;
    }

    void Lazy_comp(const std::shared_ptr<ILProgram> il_representation) {
        compiler = std::make_unique<Compiler>(il_representation, emitter);
    }

	// temporary: print an operand for debugging
	static void print_operand(const Operand& o) {
		if (o.is_reg())
			std::cout << "$" << o.reg;
		else if (o.kind == Operand::Kind::ImmInt)
			std::cout << o.imm_int;
		else
			std::cout << o.imm_float;
	}

    

	static void print_function(const ILFunction& fn, const std::shared_ptr<ILProgram> il_representation) {
        std::cout << "\n";
        std::cout << "fn " << fn.name << "(";
        for (size_t i = 0; i < fn.param_types.size(); i++) {
            const auto& param = fn.param_types[i];
            std::cout << "$" << param.reg << " : " << ILType_to_string(param.type);
            if (i + 1 < fn.param_types.size()) std::cout << ", ";
        }
        std::cout << ")\n";
        std::cout << " #regs: " << fn.reg_count << "\n";
        for (auto& bb : fn.blocks) {
            std::cout << "  " << bb.id << ":\n";
            for (auto& instr : bb.instrs) {
                std::cout << "    ";
                if (instr.dst.valid())
                    std::cout << "$" << instr.dst.id << " = ";
                std::cout << op_name(instr.op) << " "; // raw for now
                if (op_name(instr.op) == "call") {
                    std::cout << il_representation->find_by_id(instr.fn_id)->name << ": ";
                    for (int a = instr.param_info[0]; a < instr.param_info[1]; a++) {
                        std::cout << "$" << fn.arg_pool[a];
                        if (a != instr.param_info[1] - 1) {
                            std::cout << ", ";
                        }
                    }
                }
                for (u8 x = 0; x < instr.src_count; x++) {
                    print_operand(instr.src[x]);
                    if (x < instr.src_count - 1) {
                        std::cout << ", ";
                    }
                }
                if (op_name(instr.op) == "jmp" || op_name(instr.op) == "jz" || op_name(instr.op) == "jnz") {
                    if (op_name(instr.op) != "jmp") {
                        std::cout << ", ";
                    }
                    std::cout << instr.block_target;
                }
                
                
				std::cout << "\n";
			}
		}
	}
    static constexpr inline const char* op_name(Op op) {
        switch (op) {
            // arithmetic
        case Op::Add:        return "add";
        case Op::Sub:        return "sub";
        case Op::Mul:        return "mul";
        case Op::Div:        return "div";
        case Op::Rem:        return "rems";
        case Op::Neg:        return "neg";
        case Op::And:        return "and";
        case Op::Or:         return "or";
        case Op::Xor:        return "xor";
        case Op::Shl:        return "shl";
        case Op::Shr:        return "shr";
        case Op::Not:        return "not";
            // comparison
        case Op::Eq:         return "eq";
        case Op::Ne:         return "ne";
        case Op::Lt:         return "lt";
        case Op::Le:         return "le";
        case Op::Gt:         return "gt";
        case Op::Ge:         return "ge";
            // memory
        case Op::Alloc:      return "alloc";
        case Op::StackAlloc: return "stackAlloc";
        case Op::Free:       return "free";
        case Op::Load:       return "load";
        case Op::Store:      return "store";
        case Op::Offset:     return "offset";
            // arrays
        case Op::AllocArray: return "allocArray";
        case Op::ArrayLoad:  return "arrayLoad";
        case Op::ArrayStore: return "arrayStore";
        case Op::ArrayLen:   return "arrayLen";
            // structs
        case Op::FieldPtr:   return "fieldPtr";
            // casts
        case Op::Bitcast:    return "bitcast";
        case Op::PtrToInt:   return "ptrToInt";
        case Op::IntToPtr:   return "intToPtr";
            // control flow
        case Op::Jmp:        return "jmp";
        case Op::Jz:         return "jz";
        case Op::Js:         return "js";
        case Op::Jnz:        return "jnz";
        case Op::Call:       return "call";
        case Op::Ret:        return "ret";
            // misc
        case Op::Copy:       return "copy";
        }
        return "unknown";
    }

    static constexpr inline const std::string ILType_to_string(const ILType& t) {
        switch (t.kind) {
        case ILTypeKind::I8:    return "i8";
        case ILTypeKind::I16:   return "i16";
        case ILTypeKind::I32:   return "i32";
        case ILTypeKind::I64:   return "i64";
        case ILTypeKind::U8:    return "u8";
        case ILTypeKind::U16:   return "u16";
        case ILTypeKind::U32:   return "u32";
        case ILTypeKind::U64:   return "u64";
        case ILTypeKind::F32:   return "f32";
        case ILTypeKind::F64:   return "f64";
        case ILTypeKind::Bool:  return "bool";
        case ILTypeKind::USize: return "usize";
        case ILTypeKind::Void:  return "void";
        case ILTypeKind::Ptr:   return t.inner_id ? "ptr<" + std::to_string(t.inner_id) + ">" : "ptr";
        case ILTypeKind::Array: return t.inner_id ? "array<" + std::to_string(t.inner_id) + ">" : "array";
        case ILTypeKind::Fn:    return t.inner_id ? "fn<" + std::to_string(t.inner_id) + ">" : "fn";
        default:                return "<unknown>";
        }
    }
};