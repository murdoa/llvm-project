# M6809 backend (experimental)

Motorola 6809, with the Hitachi HD6309 as a subtarget (`-mcpu=6309`).

## Status

- Code generation for freestanding C at `-O0`, `-O2` and `-Oz`. Every
  applicable GCC c-torture execute test passes in a simulator for both
  CPUs, and 6809 code also passes when run on an HD6309.
- MC layer: code emitter, ELF object files with relocations, assembly
  parser and disassembler. HD6309-only instructions are rejected unless
  `-mcpu=6309` is given. Encodings were checked byte for byte against
  lwasm.
- `ld.lld` links the objects; no outside tools are needed to build a ROM.
- HD6309: W is a fifth 16-bit register, and codegen uses the 16-bit ALU on
  D and W, the register-register ops (`addr`, `cmpr`, ...), `muld`, `divq`
  (signed 16-bit division, and unsigned when the divisor is below 0x8000),
  `tstd`, `clrd`, `tfm` for `memcpy` and `memset`, and the W pushes and
  pulls.

Missing on the HD6309 so far: E and F as 8-bit registers, and `tfm` for
`memmove`.

## ABI

The two backends share one C calling convention; clang's M6809ABIInfo and
`M6809CallingConv.td` define it.

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
- **Registers.** U is callee-saved and is the frame pointer when the
  function needs one. A, B, X and Y are caller-saved. CC and DP are never
  allocated.
- **HD6309.** W (E:F) is caller-saved. V is never allocated. MD is not
  changed, so the CPU stays in 6809 emulation mode and 6809 code runs
  unchanged.
- **Stack.** S is the stack pointer and grows downwards. A frame is a
  single `leas -N,s`, or `pshs u; leau ,s; leas -N,s` with a frame
  pointer. The caller allocates and frees the outgoing argument area.

## References

- `~/solarpi/Solartron 7081 Mickle T/Diagrams/6809 Set.TXT` has a few
  wrong rows (for example the COM/COMA cycle counts are swapped). Check
  against `6809 Datasheet.pdf`.
- `6303 Set.txt` (HD6303 = ROM-less HD6301) is for the separate M6800
  backend.
