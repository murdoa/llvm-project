//===-- M6809OpcodeTable.cpp - 6809 opcode bytes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809OpcodeTable.h"
#include "M6809MCTargetDesc.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"

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

bool M6809::addFixedRegisters(MCInst &Inst, const MCInstrInfo &MII,
                              const MCRegisterInfo &MRI, unsigned Count,
                              unsigned Reg) {
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (unsigned I = 0; I != Count; ++I) {
    const MCOperandInfo &Info = Desc.operands()[I];
    if (Info.OperandType != MCOI::OPERAND_REGISTER)
      return false;
    unsigned R = Reg;
    if (R == M6809::NoRegister) {
      const MCRegisterClass &RC = MRI.getRegClass(Info.RegClass);
      if (RC.getNumRegs() != 1)
        return false;
      R = RC.getRegister(0);
    }
    Inst.addOperand(MCOperand::createReg(R));
  }
  return true;
}
