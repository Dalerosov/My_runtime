# MyRuntime

A JIT compiler and runtime written in C++, targeting x86-64 on Windows.

> **Status: early but working.** IL programs are parsed, compiled to native x86-64 at runtime, and executed. The language is close to Turing complete; arrays and pointers are the main missing pieces. This is a personal learning project exploring runtime and JIT internals.

## What this is

MyRuntime is an experiment in building a JIT compiler and runtime from scratch: take a simple intermediate representation, generate x86-64 machine code for it directly (no LLVM, no assembler library), and execute it. It's inspired by how production runtimes like the JVM and .NET CLR work under the hood, scaled down to something one person can build and understand end to end.

## How it works

```
 IL source file ──► parser ──► IL in memory ──► x86-64 emitter ──► executable memory ──► run
                                              
```

- **IL**: a three-address language with named, typed variables (`i32`, `u64`). Programs are loaded from a file at runtime. Operands are stored using type punning.
- **Emitter**: encodes x86-64 directly: REX prefixes, ModRM bytes, jump opcodes, stack frames.
- **Calling convention**: Windows x64 ABI prologue/epilogue generation and calls.
- **Register allocation**: basic allocation with spill handling, plus a shadow value stack that tracks where values live so the emitter avoids redundant stack-to-register round trips.
- **Jumps and labels**: forward and backward jumps are resolved with a patch list, fixed up when the label is emitted.
- **Executable memory**: code is written to `VirtualAlloc`'d pages as read/write, then flipped to read/execute before running (never RWX).
- **Instrumentation**: each function's prologue increments a call counter, which is the groundwork for tiered compilation.
- **Hexdump**: the emitted machine code can be dumped for inspection.

## IL example

```
fn is_prime($n : u64) -> u64
    $small : u64 = lt $n, 2
    jnz $small, no

    $d : u64 = 2
    loop:
    $sq : u64 = mul $d, $d
    $over : u64 = gt $sq, $n
    jnz $over, yes
    $r : u64 = rem $n, $d
    jz $r, no
    $d : u64 = add $d, 1
    jmp loop

    yes:
    $one : u64 = 1
    ret $one
    no:
    $zero : u64 = 0
    ret $zero

fn main() -> u64
    $n : u64 = 2
    $count : u64 = 0

    outer:
    $c : u64 = lt $n, 10000000
    jz $c, done
    $p : u64 = call is_prime, $n
    jz $p, skip
    $count : u64 = add $count, 1
    skip:
    $n : u64 = add $n, 1
    jmp outer

    done:
    ret $count
```

## Status

| Area | State |
|---|---|
| IL parser (loads from file at runtime) | Working |
| x86-64 emission, Windows x64 ABI | Working |
| Arithmetic, calls, returns | Working |
| Conditional/unconditional jumps, labels (forward and backward) | Working |
| Register allocation with spill handling | Working, basic |
| Per-function call counters | Working |
| Hexdump of emitted code | Working |
| Arrays | Not yet |
| Pointers | Not yet |
| Instruction coverage | Minimal |
| Optimization passes | None yet |
| Tiered compilation | Planned; call counters are the first step |

## Design decisions

- **Direct machine code emission.** More work than using an assembler, but the encoding is the interesting part.

## Running

The program currently loads its IL from `D:\mylang.test.sil`. The path is hardcoded; change it in the source to point at your own file.


## Roadmap

- [ ] Pointers
- [ ] Heap arrays
- [ ] Tiered compilation driven by the call counters
- [ ] Broader instruction coverage
- [ ] Optimization passes
- [ ] Far future: create a language which will be compiled into the IL
