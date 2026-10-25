//===-- M6800OpcodeTable.h - 6800 opcode bytes ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The table of HD6301 opcodes shared by the encoder, the assembler and the
// disassembler. See M6800Opcodes.def.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800OPCODETABLE_H
#define LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800OPCODETABLE_H

#include "llvm/ADT/ArrayRef.h"
#include <cstdint>

namespace llvm {
class MCInst;
class MCInstrInfo;
class MCRegisterInfo;

namespace M6800 {

enum OperandForm : uint8_t {
  Inh,      // no operand bytes
  Imm8,     // #n
  Imm16,    // #nn
  Mem,      // direct, indexed or extended: Code, Code + 0x10, Code + 0x20
  MemIX,    // indexed or extended only: Code, Code + 0x10
  ImmMem,   // #n, then direct or indexed (AIM, OIM, EIM, TIM): Code, Code - 0x10
  Rel8,     // 8-bit branch offset
  Cond8,    // Bcc: the opcode's low nibble is the condition
  Abs16,    // a 16-bit address (the long forms of BRA and BSR)
  LongCond, // b<opposite> *+5, then jmp to a 16-bit address
};

struct OpcodeEntry {
  const char *Mnemonic;
  unsigned Opcode; // MC opcode
  unsigned Reg;    // register named by the mnemonic, or NoRegister
  OperandForm Form;
  uint8_t Code;    // opcode byte
};

inline bool hasDirect(const OpcodeEntry &E) {
  return E.Form == Mem || E.Form == ImmMem;
}
inline bool hasExtended(const OpcodeEntry &E) {
  return E.Form == Mem || E.Form == MemIX;
}

// Opcode bytes for the memory forms of an entry.
inline uint8_t getDirectCode(const OpcodeEntry &E) { return E.Code; }
inline uint8_t getIndexedCode(const OpcodeEntry &E) {
  switch (E.Form) {
  case Mem: return E.Code + 0x10;
  case ImmMem: return E.Code - 0x10;
  default: return E.Code;
  }
}
inline uint8_t getExtendedCode(const OpcodeEntry &E) {
  return E.Form == Mem ? E.Code + 0x20 : E.Code + 0x10;
}

ArrayRef<OpcodeEntry> getOpcodeTable();

// The entry for an MC instruction whose first register operand is Reg.
const OpcodeEntry *findOpcodeEntry(unsigned Opcode, unsigned Reg);

// Adds the first Count operands of Inst, which must all be registers: Reg,
// or when that is NoRegister the only member of the operand's class (D for
// ADDD and CPX). Returns false if an operand is not such a register.
bool addFixedRegisters(MCInst &Inst, const MCInstrInfo &MII,
                       const MCRegisterInfo &MRI, unsigned Count,
                       unsigned Reg);

} // namespace M6800
} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800OPCODETABLE_H
