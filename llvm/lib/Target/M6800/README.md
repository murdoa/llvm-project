# M6800 backend

An experimental backend for the Motorola 6800 family, aimed at the Hitachi
HD6301/HD6303 (the default CPU, `-mcpu=6301`). It shares its design with the
M6809 backend, which it was forked from.

## Status

- Code generation for C: integer types up to 64 bits, calls, varargs.
- MC layer: the HD6301 instruction set, encoded by the integrated
  assembler, with branch relaxation (BRA to JMP, Bcc to a reversed Bcc over
  a JMP), and an assembly parser. There is no disassembler yet.

## Code generation

The 6800 has one index register and no stack-relative addressing, so the
backend is built around reaching the stack through X:

- Until late in the pipeline a stack slot is a memory operand with SP as its
  base and an offset from SP + 1. `M6800FrameAccess` rewrites each to `tsx`
  and `off,x`, pushing X around the access when it holds a live value, and
  reusing one `tsx` while X and SP are unchanged.
- Register-register operations go through the stack: `pshx; tsx; addd 0,x;
  pulx` for D + X, and `pshb; tsx; adda 0,x; ins` for bytes, except that
  `aba`, `sba` and `cba` cover A against B.
- Copies between D and X use `xgdx` when the source dies, and the stack
  otherwise.
- A function with variable-sized stack objects (or a local with more than
  byte alignment, which becomes one) has a frame pointer. No register is
  free for it, so it is the word `__m6800_fp` in memory, saved by the
  prologue and put back by the epilogue. Stack slots are then reached with
  `ldx __m6800_fp` instead of `tsx`.
- At -O0 too the greedy register allocator is used: the fast allocator
  cannot work with one-register classes.

## ABI

- `char` is 8 bits, `int` 16, `long` 32, pointers 16. Big-endian, byte
  alignment.
- The first argument goes in B (8-bit) or D (16-bit); the rest go on the
  stack, above the return address.
- Results come back in B (8-bit), D (16-bit) or D:X (32-bit, D high).
- Every register is caller-saved.
