# VPU Model

This repository contains the C++ simulation code for the VPU hardware. It uses the VPU\_ASM submodule to provide configuration generation and the assembler.

## Building

Build with `cmake`. Make sure the submodules are synced as building depends on the scripts there.

## Running

### Compiling Programs

See the README in the VPU\_ASM submodule for steps on writing and compiling programs.

### Running Simulator

See options with `vpu --help`. `vpu <binary program>` will execute the input binary. Note that the program _must_ be a compiled binary not a text program, it is loaded directly into memory and executed.

To see feedback from the execution you need to set some other options.

- `--trace` will print the register state each cycle
- `--pipeline` will print the instruction in each pipeline stage of the management core
- `--dump_mem/regs` will dump the entire memory state and end register state in files after completion. Note that the memory file is quite large
- `--step` will wait after executing each cycle, provide a number to step a specific number of cycles or just press enter to run one
