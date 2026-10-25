//===-- M6800MCInstLower.cpp - Lower MachineInstr to MCInst ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800MCInstLower.h"
#include "MCTargetDesc/M6800MCTargetDesc.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MCOperand
M6800MCInstLower::lowerSymbolOperand(const MachineOperand &MO) const {
  const MCSymbol *Sym;
  int64_t Offset = 0;

  switch (MO.getType()) {
  case MachineOperand::MO_MachineBasicBlock:
    Sym = MO.getMBB()->getSymbol();
    break;
  case MachineOperand::MO_GlobalAddress:
    Sym = Printer.getSymbol(MO.getGlobal());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_ExternalSymbol:
    Sym = Printer.GetExternalSymbolSymbol(MO.getSymbolName());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_BlockAddress:
    Sym = Printer.GetBlockAddressSymbol(MO.getBlockAddress());
    Offset = MO.getOffset();
    break;
  case MachineOperand::MO_JumpTableIndex:
    Sym = Printer.GetJTISymbol(MO.getIndex());
    break;
  case MachineOperand::MO_ConstantPoolIndex:
    Sym = Printer.GetCPISymbol(MO.getIndex());
    Offset = MO.getOffset();
    break;
  default:
    llvm_unreachable("M6800: unknown symbol operand");
  }

  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (Offset)
    Expr = MCBinaryExpr::createAdd(Expr, MCConstantExpr::create(Offset, Ctx),
                                   Ctx);
  return MCOperand::createExpr(Expr);
}

static void addMem(MCInst &MI, MCRegister Base, MCOperand Off) {
  MI.addOperand(MCOperand::createReg(Base));
  MI.addOperand(Off);
  MI.addOperand(MCOperand::createImm(M6800Mem::Auto));
}

// Turn codegen-only forms into the instructions the assembler and encoder
// know.
static void toRealInstruction(MCInst &MI) {
  MCInst Out;
  switch (MI.getOpcode()) {
  default:
    return;
  case M6800::JSRe: // jsr sym
    Out.setOpcode(M6800::JSRm);
    addMem(Out, MCRegister(), MI.getOperand(0));
    break;
  case M6800::JSRr: // jsr 0,x
    Out.setOpcode(M6800::JSRm);
    addMem(Out, MI.getOperand(0).getReg(), MCOperand::createImm(0));
    break;
  }
  MI = Out;
}

MCOperand M6800MCInstLower::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  default:
    return lowerSymbolOperand(MO);
  }
}

void M6800MCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    if ((MO.isReg() && MO.isImplicit()) || MO.isRegMask())
      continue;
    OutMI.addOperand(lowerOperand(MO));
  }
  toRealInstruction(OutMI);
}
