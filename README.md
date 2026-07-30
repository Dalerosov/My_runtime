# MyRuntime

A JIT compiler and runtime system written in C++, targeting x86-64 on Windows.

> **Status: very early work-in-progress (~1% complete).** Jump patching is not fully implemented, and the JIT does not yet execute generated code end-to-end. This is a personal learning project exploring runtime/JIT internals — not something you can run programs with yet.

## What this is

MyRuntime is an experiment in building a JIT compiler and runtime from scratch: taking a simple intermediate representation, generating x86-64 machine code for it directly, and (eventually) executing that code. It's inspired by how production runtimes like the JVM and .NET CLR work under the hood, scaled down to something one person can build and understand end to end.

## What's implemented so far

- Executable memory allocation for JIT-compiled code (`VirtualAlloc`, RW → RX transition)
- x86-64 instruction encoding basics: REX prefixes, ModRM bytes, jump opcodes
- A simple IL / operand representation, including type punning for operand storage
- Register allocation with basic spill handling
- Groundwork for the Windows x64 calling convention (prologue/epilogue generation)
- Label/jump patching (forward and backward) — in progress, not yet reliable

## What's not working yet

- Jump patching has known issues and isn't complete
- There is no working end-to-end execution path — JIT-compiled code does not run yet
- Instruction coverage is minimal
- No optimization passes
## Why

Built to understand JIT and runtime internals from the ground up — instruction encoding, calling conventions, code generation, and the design tradeoffs that runtimes like the JVM and CLR make.

## Building

The code can be browsed and built, but there's currently no working demo to run — this section will be filled in once there's an end-to-end path from IL to execution.
