//===-- M6809InstPrinter.cpp - Convert M6809 MCInst to asm ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809InstPrinter.h"
#include "M6809MCTargetDesc.h"
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

// Prints a memory operand in Motorola syntax:
//   Auto       "off,reg" (",reg" for zero), or "addr" with no base register
//   Extended   ">addr"            Direct    "<addr"
//   Off5/8/16  "<<off,reg", "<off,reg", ">off,reg"
//   ,R+  ,R++  ,-R  ,--R   a,R  b,R  d,R   addr,pcr
// with "[...]" around it for the indirect forms.
void M6809InstPrinter::printMemIndexedOperand(const MCInst *MI, unsigned OpNo,
                                              raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Off = MI->getOperand(OpNo + 1);
  unsigned Mode = MI->getOperand(OpNo + 2).getImm();
  bool Indirect = Mode & M6809Mem::Indirect;
  Mode &= M6809Mem::ModeMask;
  bool NoBase = !Base.getReg();

  auto PrintOff = [&](bool Absolute) {
    if (Off.isImm()) {
      // An absolute address is unsigned; an index offset is signed.
      if (Absolute)
        O << static_cast<uint16_t>(Off.getImm());
      else
        O << Off.getImm();
    } else {
      MAI.printExpr(O, *Off.getExpr());
    }
  };
  auto PrintReg = [&] { printRegName(O, Base.getReg()); };

  if (Indirect)
    O << '[';
  switch (Mode) {
  case M6809Mem::Auto:
    if (NoBase) {
      PrintOff(true);
      break;
    }
    if (!Off.isImm() || Off.getImm() != 0)
      PrintOff(false);
    O << ',';
    PrintReg();
    break;
  case M6809Mem::Extended:
    O << '>';
    PrintOff(true);
    break;
  case M6809Mem::Direct:
    O << '<';
    PrintOff(true);
    break;
  case M6809Mem::Off5:
  case M6809Mem::Off8:
  case M6809Mem::Off16:
    O << (Mode == M6809Mem::Off5 ? "<<" : Mode == M6809Mem::Off8 ? "<" : ">");
    PrintOff(false);
    O << ',';
    PrintReg();
    break;
  case M6809Mem::PostInc1:
  case M6809Mem::PostInc2:
    O << ',';
    PrintReg();
    O << (Mode == M6809Mem::PostInc1 ? "+" : "++");
    break;
  case M6809Mem::PreDec1:
  case M6809Mem::PreDec2:
    O << (Mode == M6809Mem::PreDec1 ? ",-" : ",--");
    PrintReg();
    break;
  case M6809Mem::AccA:
  case M6809Mem::AccB:
  case M6809Mem::AccD:
    O << (Mode == M6809Mem::AccA ? "a," : Mode == M6809Mem::AccB ? "b," : "d,");
    PrintReg();
    break;
  case M6809Mem::PCRel:
    PrintOff(true);
    O << ",pcr";
    break;
  default:
    O << "<bad mode>";
    break;
  }
  if (Indirect)
    O << ']';
}

void M6809InstPrinter::printCondCode(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  O << M6809CC::getName(
      static_cast<M6809CC::CondCode>(MI->getOperand(OpNo).getImm()));
}
