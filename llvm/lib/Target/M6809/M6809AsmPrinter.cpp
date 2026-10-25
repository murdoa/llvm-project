//===-- M6809AsmPrinter.cpp - M6809 LLVM assembly writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809MCInstLower.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class M6809AsmPrinter : public AsmPrinter {
  M6809MCInstLower MCInstLowering;

public:
  static char ID;

  M6809AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID),
        MCInstLowering(OutContext, *this) {}

  StringRef getPassName() const override { return "M6809 Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override {
    M6809_MC::verifyInstructionPredicates(MI->getOpcode(),
                                          getSubtargetInfo().getFeatureBits());
    MCInst Inst;
    MCInstLowering.lower(MI, Inst);
    EmitToStreamer(*OutStreamer, Inst);
  }
};

} // namespace

char M6809AsmPrinter::ID = 0;

INITIALIZE_PASS(M6809AsmPrinter, "m6809-asm-printer", "M6809 Assembly Printer",
                false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6809AsmPrinter() {
  RegisterAsmPrinter<M6809AsmPrinter> X(getTheM6809Target());
}
