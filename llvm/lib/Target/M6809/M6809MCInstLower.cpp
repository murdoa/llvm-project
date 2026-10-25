//===-- M6809MCInstLower.cpp - Lower MachineInstr to MCInst ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809MCInstLower.h"
#include "M6809Subtarget.h"
#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

MCOperand
M6809MCInstLower::lowerSymbolOperand(const MachineOperand &MO) const {
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
    llvm_unreachable("M6809: unknown symbol operand");
  }

  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (Offset)
    Expr = MCBinaryExpr::createAdd(Expr, MCConstantExpr::create(Offset, Ctx),
                                   Ctx);
  return MCOperand::createExpr(Expr);
}

// Codegen-only forms that read their operand from the stack, and the plain
// instruction with a ",s+" or ",s++" memory operand that replaces them.
static bool getPopForm(unsigned Opc, unsigned &MemOpc, unsigned &Mode) {
  switch (Opc) {
#define POP(From, To, M)                                                       \
  case M6809::From:                                                            \
    MemOpc = M6809::To;                                                        \
    Mode = M6809Mem::M;                                                        \
    return true;
    POP(ADD8pop, ADD8m, PostInc1)
    POP(SUB8pop, SUB8m, PostInc1)
    POP(AND8pop, AND8m, PostInc1)
    POP(OR8pop, OR8m, PostInc1)
    POP(EOR8pop, EOR8m, PostInc1)
    POP(CMP8pop, CMP8m, PostInc1)
    POP(ADC8pop2, ADC8m, PostInc2)
    POP(SBC8pop2, SBC8m, PostInc2)
    POP(ADDDpop, ADDDm, PostInc2)
    POP(SUBDpop, SUBDm, PostInc2)
    POP(CMP16pop, CMP16m, PostInc2)
#undef POP
  default:
    return false;
  }
}

static void addMem(MCInst &MI, MCRegister Base, MCOperand Off,
                   unsigned Mode = M6809Mem::Auto) {
  MI.addOperand(MCOperand::createReg(Base));
  MI.addOperand(Off);
  MI.addOperand(MCOperand::createImm(Mode));
}

// Turn codegen-only forms into the instructions the assembler and encoder
// know.
static void toRealInstruction(MCInst &MI) {
  MCInst Out;
  unsigned Opc = MI.getOpcode(), MemOpc, Mode;
  if (getPopForm(Opc, MemOpc, Mode)) {
    Out.setOpcode(MemOpc);
    for (const MCOperand &Op : MI)
      Out.addOperand(Op);
    addMem(Out, M6809::S, MCOperand::createImm(0), Mode);
    MI = Out;
    return;
  }
  switch (Opc) {
  default:
    return;
  // Codegen branches start short; the assembler makes them long when the
  // target is out of range (see M6809AsmBackend).
  case M6809::LBRA:
    MI.setOpcode(M6809::BRA);
    return;
  case M6809::LBcc:
    MI.setOpcode(M6809::Bcc);
    return;
  case M6809::JSRe: // jsr sym
    Out.setOpcode(M6809::JSRm);
    addMem(Out, MCRegister(), MI.getOperand(0));
    break;
  case M6809::JSRr: // jsr ,r
    Out.setOpcode(M6809::JSRm);
    addMem(Out, MI.getOperand(0).getReg(), MCOperand::createImm(0));
    break;
  case M6809::LD8dr: // ld dst, d,base
  case M6809::LD16dr:
    Out.setOpcode(Opc == M6809::LD8dr ? M6809::LD8m : M6809::LD16m);
    Out.addOperand(MI.getOperand(0));
    addMem(Out, MI.getOperand(1).getReg(), MCOperand::createImm(0),
           M6809Mem::AccD);
    break;
  case M6809::ST16dr: // st src, d,base
    Out.setOpcode(M6809::ST16m);
    Out.addOperand(MI.getOperand(0));
    addMem(Out, MI.getOperand(1).getReg(), MCOperand::createImm(0),
           M6809Mem::AccD);
    break;
  case M6809::LEA16dr: // lea dst, d,base
    Out.setOpcode(M6809::LEA16);
    Out.addOperand(MI.getOperand(0));
    addMem(Out, MI.getOperand(1).getReg(), MCOperand::createImm(0),
           M6809Mem::AccD);
    break;
  case M6809::LEASi: // leas n,s
    Out.setOpcode(M6809::LEASm);
    addMem(Out, M6809::S, MI.getOperand(0));
    break;
  case M6809::PSHS8:
  case M6809::PSHS16:
  case M6809::PULS8:
  case M6809::PULS16: {
    bool Push = Opc == M6809::PSHS8 || Opc == M6809::PSHS16;
    if (MI.getOperand(0).getReg() == M6809::W) { // not in a register list
      Out.setOpcode(Push ? M6809::PSHSW : M6809::PULSW);
      break;
    }
    Out.setOpcode(Push ? M6809::PSHSr : M6809::PULSr);
    Out.addOperand(MCOperand::createImm(
        M6809::getPushPullMask(MI.getOperand(0).getReg(), false)));
    break;
  }
  case M6809::CLRA: // the high byte of D
    Out.setOpcode(M6809::CLR8);
    Out.addOperand(MCOperand::createReg(M6809::A));
    break;
  case M6809::TFR8:
  case M6809::TFR16: // (dst, src) -> tfr src, dst
    Out.setOpcode(M6809::TFRrr);
    Out.addOperand(MI.getOperand(1));
    Out.addOperand(MI.getOperand(0));
    break;
  }
  MI = Out;
}

MCOperand M6809MCInstLower::lowerOperand(const MachineOperand &MO) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  default:
    return lowerSymbolOperand(MO);
  }
}

void M6809MCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands()) {
    if ((MO.isReg() && MO.isImplicit()) || MO.isRegMask())
      continue;
    OutMI.addOperand(lowerOperand(MO));
  }
  toRealInstruction(OutMI);

  // "ldd #0" is "clrd" on the HD6309, a byte shorter ("ldw #0": two). CLR
  // also clears C, which LD leaves alone, but LD16i is modelled as
  // clobbering the flags, so nothing reads C across it.
  if (OutMI.getOpcode() == M6809::LD16i &&
      MI->getMF()->getSubtarget<M6809Subtarget>().has6309() &&
      OutMI.getOperand(1).isImm() && OutMI.getOperand(1).getImm() == 0) {
    MCRegister R = OutMI.getOperand(0).getReg();
    if (R == M6809::D || R == M6809::W) {
      OutMI = MCInst();
      OutMI.setOpcode(M6809::CLR16r);
      OutMI.addOperand(MCOperand::createReg(R));
    }
  }
}
