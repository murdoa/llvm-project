//===-- M6800OpcodeTable.cpp - 6800 opcode bytes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800OpcodeTable.h"
#include "M6800MCTargetDesc.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"

using namespace llvm;
using namespace llvm::M6800;

static const OpcodeEntry Table[] = {
#define M6800_OP(MNEM, OPC, REG, FORM, CODE)                                   \
  {MNEM, M6800::OPC, REG, M6800::FORM, CODE},
#include "M6800Opcodes.def"
};

ArrayRef<OpcodeEntry> M6800::getOpcodeTable() { return Table; }

const OpcodeEntry *M6800::findOpcodeEntry(unsigned Opcode, unsigned Reg) {
  // A few rows per opcode at most, so a linear search is fine.
  for (const OpcodeEntry &E : Table)
    if (E.Opcode == Opcode && (E.Reg == M6800::NoRegister || E.Reg == Reg))
      return &E;
  return nullptr;
}

bool M6800::addFixedRegisters(MCInst &Inst, const MCInstrInfo &MII,
                              const MCRegisterInfo &MRI, unsigned Count,
                              unsigned Reg) {
  const MCInstrDesc &Desc = MII.get(Inst.getOpcode());
  for (unsigned I = 0; I != Count; ++I) {
    const MCOperandInfo &Info = Desc.operands()[I];
    if (Info.OperandType != MCOI::OPERAND_REGISTER)
      return false;
    unsigned R = Reg;
    if (R == M6800::NoRegister) {
      const MCRegisterClass &RC = MRI.getRegClass(Info.RegClass);
      if (RC.getNumRegs() != 1)
        return false;
      R = RC.getRegister(0);
    }
    Inst.addOperand(MCOperand::createReg(R));
  }
  return true;
}
