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

The same C calling convention as the M6809 backend; only the registers,
the frame pointer and the stack differ.

- **Types.** `char` is 8 bits and signed, `short` and `int` 16, `long`
  32, `long long` 64, pointers and `size_t` 16. `float` is IEEE single
  and `double` and `long double` IEEE double, in software. Big-endian,
  and every type is byte-aligned.
- **Arguments.** The first argument goes in B if it is 8 bits or D if it
  is 16, and the first half of a 32-bit one (its high word) in D. Everything
  else goes on the stack, the first argument at the lowest address, just
  above the return address. The area is packed: there is no padding.
  Variadic arguments always go on the stack. An 8-bit argument is not
  widened: it takes B or a single stack byte.
- **Aggregates.** A struct or union passed by value is copied into the
  argument area, whatever its size. One returned by value goes through a
  hidden pointer, which is the first argument (so it is in D). Empty
  structs take no space.
- **Results.** 8-bit values return in B, 16-bit ones in D, and 32-bit ones
  (`long`, `float`) with the high word in D and the low word in X.
  Anything larger, such as `long long` and `double`, is returned through a
  hidden pointer, as aggregates are.
- **Registers.** A, B and X are all caller-saved.
- **Frame pointer.** A function with variable-sized objects keeps its frame
  pointer in the word `__m6800_fp` (from the runtime). It saves the word on
  entry and restores it on return, so the word is in effect callee-saved.
- **Stack.** SP points to the next free byte, as the CPU pushes. The caller
  allocates and frees the outgoing argument area.
