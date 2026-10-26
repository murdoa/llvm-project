//===-- M6809AsmPrinter.cpp - M6809 LLVM assembly writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809.h"
#include "M6809MCInstLower.h"
#include "MCTargetDesc/M6809InstPrinter.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
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
        MCInstLowering(OutContext, this) {}

  StringRef getPassName() const override { return "M6809 Assembly Printer"; }

  void emitInstruction(const MachineInstr *MI) override {
    if (MI->getOpcode() == M6809::SJLJ_SETUP)
      return;
    M6809_MC::verifyInstructionPredicates(MI->getOpcode(),
                                          getSubtargetInfo().getFeatureBits());
    if (MI->getOpcode() == M6809::COPYLOOP ||
        MI->getOpcode() == M6809::SETLOOP) {
      emitLoop(*MI);
      return;
    }
    MCInst Inst;
    MCInstLowering.lower(MI, Inst);
    EmitToStreamer(*OutStreamer, Inst);
  }

  void emitLoop(const MachineInstr &MI);

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;
};

} // namespace

// ldb #len; 1: lda ,src+; sta ,dst+; decb; bne 1b
// lda #val; ldb #len; 1: sta ,dst+; decb; bne 1b
void M6809AsmPrinter::emitLoop(const MachineInstr &MI) {
  bool Cpy = MI.getOpcode() == M6809::COPYLOOP;
  MCRegister Dst = MI.getOperand(Cpy ? 2 : 1).getReg();
  auto Mem = [](MCInst &I, MCRegister Base) {
    I.addOperand(MCOperand::createReg(Base));
    I.addOperand(MCOperand::createImm(0));
    I.addOperand(MCOperand::createImm(M6809Mem::PostInc1));
  };
  if (!Cpy) {
    int64_t Val = MI.getOperand(2).getImm();
    if (Val == 0)
      EmitToStreamer(*OutStreamer,
                     MCInstBuilder(M6809::CLR8).addReg(M6809::A));
    else
      EmitToStreamer(*OutStreamer,
                     MCInstBuilder(M6809::LD8i).addReg(M6809::A).addImm(Val));
  }
  EmitToStreamer(*OutStreamer, MCInstBuilder(M6809::LD8i)
                                   .addReg(M6809::B)
                                   .addImm(MI.getOperand(Cpy ? 4 : 3).getImm()));
  MCSymbol *Loop = OutContext.createTempSymbol();
  OutStreamer->emitLabel(Loop);
  if (Cpy) {
    MCInst Ld;
    Ld.setOpcode(M6809::LD8m);
    Ld.addOperand(MCOperand::createReg(M6809::A));
    Mem(Ld, MI.getOperand(3).getReg());
    EmitToStreamer(*OutStreamer, Ld);
  }
  MCInst St;
  St.setOpcode(M6809::ST8m);
  St.addOperand(MCOperand::createReg(M6809::A));
  Mem(St, Dst);
  EmitToStreamer(*OutStreamer, St);
  EmitToStreamer(*OutStreamer, MCInstBuilder(M6809::DEC8)
                                   .addReg(M6809::B)
                                   .addReg(M6809::B));
  EmitToStreamer(*OutStreamer,
                 MCInstBuilder(M6809::Bcc)
                     .addImm(M6809CC::NE)
                     .addExpr(MCSymbolRefExpr::create(Loop, OutContext)));
}

bool M6809AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &OS) {
  if (ExtraCode && ExtraCode[0])
    return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS);
  const MachineOperand &MO = MI->getOperand(OpNo);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    OS << M6809InstPrinter::getRegisterName(MO.getReg());
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
bool M6809AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                            unsigned OpNo,
                                            const char *ExtraCode,
                                            raw_ostream &OS) {
  if (ExtraCode && ExtraCode[0])
    return true;
  MCInst Inst;
  for (unsigned I = 0; I != 3; ++I)
    Inst.addOperand(MCInstLowering.lowerOperand(MI->getOperand(OpNo + I)));
  M6809InstPrinter Printer(MAI, *TM.getMCInstrInfo(),
                           *MF->getSubtarget().getRegisterInfo());
  Printer.printMemIndexedOperand(&Inst, 0, OS);
  return false;
}

char M6809AsmPrinter::ID = 0;

INITIALIZE_PASS(M6809AsmPrinter, "m6809-asm-printer", "M6809 Assembly Printer",
                false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6809AsmPrinter() {
  RegisterAsmPrinter<M6809AsmPrinter> X(getTheM6809Target());
}
