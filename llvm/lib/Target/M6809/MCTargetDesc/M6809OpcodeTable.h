//===-- M6809OpcodeTable.h - 6809 opcode bytes ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The table of 6809 opcodes shared by the encoder, the assembler and the
// disassembler. See M6809Opcodes.def.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809OPCODETABLE_H
#define LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809OPCODETABLE_H

#include "llvm/ADT/ArrayRef.h"
#include <cstdint>

namespace llvm {
class MCInst;
class MCInstrInfo;
class MCRegisterInfo;

namespace M6809 {

enum OperandForm : uint8_t {
  Inh,     // no operand bytes
  Imm8,    // #n
  Imm16,   // #nn
  Mem,     // direct, indexed or extended
  Idx,     // indexed only (LEA)
  Rel8,    // 8-bit branch offset
  Rel16,   // 16-bit branch offset
  Cond8,   // Bcc: the opcode's low nibble is the condition
  Cond16,  // LBcc
  RegPair, // TFR/EXG postbyte
  RegList, // PSHS/PULS/PSHU/PULU postbyte
  Imm32,   // #nnnn (LDQ)
  ImmMem,  // #n, then a memory operand (AIM, OIM, EIM, TIM)
  Tfm,     // TFM postbyte; the code gives the pointer directions
};

struct OpcodeEntry {
  const char *Mnemonic;
  unsigned Opcode; // MC opcode
  unsigned Reg;    // register named by the mnemonic, or NoRegister
  OperandForm Form;
  uint16_t Code; // opcode byte, with any page prefix in the high byte
  bool Only6309; // an HD6309 instruction
};

// Opcode bytes for the three memory forms of a Mem entry.
inline uint16_t getDirectCode(const OpcodeEntry &E) { return E.Code; }
inline uint16_t getIndexedCode(const OpcodeEntry &E) {
  if (E.Form == Idx)
    return E.Code;
  // The read-modify-write group, and AIM/OIM/EIM/TIM beside it, put direct
  // at 0x0X and indexed at 0x6X; everything else has indexed 0x10 above
  // direct.
  return (E.Code & 0xF0) == 0 ? E.Code + 0x60 : E.Code + 0x10;
}
inline uint16_t getExtendedCode(const OpcodeEntry &E) {
  return getIndexedCode(E) + 0x10;
}

ArrayRef<OpcodeEntry> getOpcodeTable();

// The entry for an MC instruction whose first register operand is Reg.
const OpcodeEntry *findOpcodeEntry(unsigned Opcode, unsigned Reg);

// Adds the first Count operands of Inst, which must all be registers: Reg,
// or when that is NoRegister the only member of the operand's class (D for
// SEX and ADDD). Returns false if an operand is not such a register.
bool addFixedRegisters(MCInst &Inst, const MCInstrInfo &MII,
                       const MCRegisterInfo &MRI, unsigned Count, unsigned Reg);

} // namespace M6809
} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809OPCODETABLE_H
