//===-- M6809OpcodeTable.cpp - 6809 opcode bytes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809OpcodeTable.h"
#include "M6809MCTargetDesc.h"

using namespace llvm;
using namespace llvm::M6809;

static const OpcodeEntry Table[] = {
#define M6809_OP(MNEM, OPC, REG, FORM, CODE)                                   \
  {MNEM, M6809::OPC, REG, M6809::FORM, CODE},
#include "M6809Opcodes.def"
};

ArrayRef<OpcodeEntry> M6809::getOpcodeTable() { return Table; }

const OpcodeEntry *M6809::findOpcodeEntry(unsigned Opcode, unsigned Reg) {
  // A few rows per opcode at most, so a linear search is fine.
  for (const OpcodeEntry &E : Table)
    if (E.Opcode == Opcode && (E.Reg == M6809::NoRegister || E.Reg == Reg))
      return &E;
  return nullptr;
}
