# M6809 backend (experimental)

Motorola 6809, with the Hitachi HD6309 as a subtarget (`-mcpu=6309`).

## Status

Assembly output only (`llc -filetype=asm`). Nothing is encoded yet, so
there is no object file output and no assembler or disassembler.

Supported so far:

- Returning `void`, `i8`, `i16` and `i32` values
- `i8`/`i16` constants
- Loading arguments from registers and the stack
- 8-bit `add/sub/and/or/xor` with an immediate or memory operand
- 16-bit `add/sub` on D with an immediate or memory operand
- Spills and reloads, and a fixed-size frame built with `leas`

Next steps, roughly in order:

1. Compares, branches and `select`
2. Shifts, and `mul` (8x8 MUL, plus libcalls)
3. Globals (extended and direct-page addressing), calls and `jsr`/`bsr`
4. Register-register 16-bit arithmetic (no `addd x`, so go through the stack
   or use `leax d,x`)
5. MC code emitter, then ELF output, then an assembly parser
6. HD6309: E/F/W/Q/V registers and extra instructions behind `Feature6309`

## Provisional ABI

See `M6809CallingConv.td`. The first argument goes in B or D, the rest on
the stack. Results return in B, D or D:X. U is callee-saved.

## References

- `~/solarpi/Solartron 7081 Mickle T/Diagrams/6809 Set.TXT` has a few
  wrong rows (for example the COM/COMA cycle counts are swapped). Check
  against `6809 Datasheet.pdf`.
- `6303 Set.txt` (HD6303 = ROM-less HD6301) is for the separate M6800
  backend.
