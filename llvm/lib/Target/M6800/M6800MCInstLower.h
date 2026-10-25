//===-- M6800MCInstLower.h - Lower MachineInstr to MCInst -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_M6800MCINSTLOWER_H
#define LLVM_LIB_TARGET_M6800_M6800MCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class AsmPrinter;
class MCContext;
class MCInst;
class MCOperand;
class MachineInstr;
class MachineOperand;

class LLVM_LIBRARY_VISIBILITY M6800MCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  M6800MCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  void lower(const MachineInstr *MI, MCInst &OutMI) const;
  MCOperand lowerOperand(const MachineOperand &MO) const;

private:
  MCOperand lowerSymbolOperand(const MachineOperand &MO) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_M6800MCINSTLOWER_H
