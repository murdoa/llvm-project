//===-- M6800AsmPrinter.cpp - M6800 LLVM assembly writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6800.h"
#include "M6800MCInstLower.h"
#include "MCTargetDesc/M6800InstPrinter.h"
#include "TargetInfo/M6800TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class M6800AsmPrinter : public AsmPrinter {
  M6800MCInstLower MCInstLowering;

public:
  static char ID;

  M6800AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID),
        MCInstLowering(OutContext, *this) {}

  StringRef getPassName() const override { return "M6800 Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override {
    M6800_MC::verifyInstructionPredicates(MI->getOpcode(),
                                          getSubtargetInfo().getFeatureBits());
    MCInst Inst;
    MCInstLowering.lower(MI, Inst);
    EmitToStreamer(*OutStreamer, Inst);
  }

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;
};

} // namespace

bool M6800AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &OS) {
  if (ExtraCode && ExtraCode[0])
    return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    OS << M6800InstPrinter::getRegisterName(MO.getReg());
    return false;
  case MachineOperand::MO_Immediate:
    OS << MO.getImm();
    return false;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    return false;
  default:
    return true;
  }
}

// A memory operand is the base, offset and mode of an indexed operand, as
// SelectInlineAsmMemoryOperand made them.
bool M6800AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                            unsigned OpNo,
                                            const char *ExtraCode,
                                            raw_ostream &OS) {
  if (ExtraCode && ExtraCode[0])
    return true;
  MCInst Inst;
  for (unsigned I = 0; I != 3; ++I)
    Inst.addOperand(MCInstLowering.lowerOperand(MI->getOperand(OpNo + I)));
  M6800InstPrinter Printer(MAI, *TM.getMCInstrInfo(), *MF->getSubtarget().getRegisterInfo());
  Printer.printMemOperand(&Inst, 0, OS);
  return false;
}

char M6800AsmPrinter::ID = 0;

INITIALIZE_PASS(M6800AsmPrinter, "m6800-asm-printer", "M6800 Assembly Printer",
                false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6800AsmPrinter() {
  RegisterAsmPrinter<M6800AsmPrinter> X(getTheM6800Target());
}
