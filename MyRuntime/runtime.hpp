#pragma once
#include <iostream>
#include <chrono>
#include "pch.hpp"
#include "emitter.hpp"
#include "loader.hpp"
#include "parser2.hpp"
#include "compiler.hpp"
#include "il.hpp"

class Runtime {
	bool lazy{};
	std::unique_ptr<Loader> loader;
	std::unique_ptr<Parser> parser;
	std::unique_ptr<Compiler> compiler;
	//Optimiser optimiser;
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
            //print_function(fun, il_representation);
        }
        std::cout << "\n\n";
        //begin cmp and exec
        if (!lazy) {
            Eager_comp(il_representation);


        }

		
	}

    void Eager_comp(const std::shared_ptr<ILProgram> il_representation) {
        compiler = std::make_unique<Compiler>(il_representation, emitter);
        compiler->compile();
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

    std::string hex_dump(std::vector<u8> buf_) const {
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
        case Op::Mod:        return "mod";
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