//===-- M6800InstPrinter.cpp - Convert M6800 MCInst to asm ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800InstPrinter.h"
#include "M6800MCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "M6800GenAsmWriter.inc"

void M6800InstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void M6800InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot, const MCSubtargetInfo &STI,
                                 raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void M6800InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
    return;
  }
  if (Op.isImm()) {
    O << Op.getImm();
    return;
  }
  assert(Op.isExpr() && "unknown operand kind");
  MAI.printExpr(O, *Op.getExpr());
}

// Prints a memory operand in Motorola syntax: "off,x" for indexed, "addr"
// with no base register, ">addr" and "<addr" when the extended or direct
// form was asked for.
void M6800InstPrinter::printMemOperand(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Off = MI->getOperand(OpNo + 1);
  unsigned Mode = MI->getOperand(OpNo + 2).getImm();

  if (Mode == M6800Mem::Extended)
    O << '>';
  else if (Mode == M6800Mem::Direct)
    O << '<';
  if (Off.isImm())
    O << (Base.getReg() ? Off.getImm()
                        : static_cast<uint16_t>(Off.getImm()));
  else
    MAI.printExpr(O, *Off.getExpr());
  if (Base.getReg()) {
    O << ',';
    printRegName(O, Base.getReg());
  }
}

void M6800InstPrinter::printCondCode(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  O << M6800CC::getName(
      static_cast<M6800CC::CondCode>(MI->getOperand(OpNo).getImm()));
}
