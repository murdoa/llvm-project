//===-- M6809InstPrinter.cpp - Convert M6809 MCInst to asm ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809InstPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "M6809GenAsmWriter.inc"

void M6809InstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  OS << getRegisterName(Reg);
}

void M6809InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot, const MCSubtargetInfo &STI,
                                 raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void M6809InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
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

// Prints "off,reg", leaving out a zero offset (",x").
void M6809InstPrinter::printMemIndexedOperand(const MCInst *MI, unsigned OpNo,
                                              raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Off = MI->getOperand(OpNo + 1);
  if (Off.isImm()) {
    if (Off.getImm() != 0)
      O << Off.getImm();
  } else {
    MAI.printExpr(O, *Off.getExpr());
  }
  O << ',';
  printRegName(O, Base.getReg());
}
