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
  `tstd`, `clrd` and the W pushes and pulls.

Missing on the HD6309 so far: `tfm` for block copies, and E and F as 8-bit
registers.

## ABI

This ABI is provisional; `M6809CallingConv.td` is the source of truth.

- **Arguments.** The first scalar argument goes in B (8 bits) or D
  (16 bits). Everything else goes on the stack, the first argument at the
  lowest address, just above the return address. Variadic arguments always
  go on the stack. Structs passed by value are copied into the argument
  area, which is packed: there is no alignment padding.
- **Results.** 8-bit values return in B and 16-bit values in D. A 32-bit
  value returns its high half in D and its low half in X.
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
