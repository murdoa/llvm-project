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
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include <set>
#include <string>

using namespace llvm;

static cl::opt<unsigned> CopyEntry(
    "m6809-copy-entry", cl::Hidden, cl::init(0),
    cl::desc("Give each copy routine an entry for copies of this many "
             "bytes, which loads B itself (0 for none)"));

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
    if (SkipReturn && MI->getOpcode() == M6809::RTS) {
      SkipReturn = false;
      return;
    }
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
  void emitEndOfAsmFile(Module &M) override;

  // The shared copy and fill routines the module calls (see emitLoop).
  std::set<std::string> LoopRoutines;
  // The loop before this RTS jumped to its routine, which returns instead.
  bool SkipReturn = false;

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;
};

} // namespace

// The loops live in shared routines, one for each pair of registers, which
// each object file that uses one emits in a COMDAT group of its own:
//
//   ldb #len; jsr __m6809_copy_<src><dst>
//     __m6809_copy_xu: lda ,x+; sta ,u+; decb; bne __m6809_copy_xu; rts
//   ldb #len; jsr __m6809_clear_<dst>          (value 0)
//   lda #val; ldb #len; jsr __m6809_fill_<dst>
//     __m6809_clear_x: clra
//     __m6809_fill_x:  sta ,x+; decb; bne __m6809_fill_x; rts
//
// A call is five bytes where the loop is nine, and a call just before a
// return jumps instead (see SkipReturn). The routines leave what the
// loop would: the pointers advanced, B zero, A the last byte, the flags set.
//
// With -m6809-copy-entry=n, a program that copies n bytes often (a number
// type, say) calls __m6809_copy<n>_<src><dst>, which loads B itself and
// falls into the loop: three bytes a call. The group's name has n in it, so
// objects built without the option, whose group lacks the entry, do not
// stand in for it; their weak loop is then a duplicate.
static char regLetter(MCRegister R) {
  return R == M6809::X ? 'x' : R == M6809::Y ? 'y' : 'u';
}

void M6809AsmPrinter::emitLoop(const MachineInstr &MI) {
  bool Cpy = MI.getOpcode() == M6809::COPYLOOP;
  MCRegister Dst = MI.getOperand(Cpy ? 2 : 1).getReg();
  std::string Name;
  int64_t Len = MI.getOperand(Cpy ? 4 : 3).getImm();
  bool Entry = false;
  if (Cpy) {
    MCRegister Src = MI.getOperand(3).getReg();
    std::string Pair = std::string() + regLetter(Src) + regLetter(Dst);
    Name = "__m6809_copy_" + Pair;
    LoopRoutines.insert(Name);
    Entry = CopyEntry && Len == CopyEntry;
    if (Entry)
      Name = "__m6809_copy" + std::to_string(Len) + "_" + Pair;
  } else {
    int64_t Val = MI.getOperand(2).getImm();
    if (Val != 0)
      EmitToStreamer(*OutStreamer,
                     MCInstBuilder(M6809::LD8i).addReg(M6809::A).addImm(Val));
    Name = std::string(Val == 0 ? "__m6809_clear_" : "__m6809_fill_") +
           regLetter(Dst);
  }
  if (!Entry)
    EmitToStreamer(*OutStreamer,
                   MCInstBuilder(M6809::LD8i).addReg(M6809::B).addImm(Len));
  // "jsr routine; rts" is "jmp routine".
  MachineBasicBlock::const_iterator Next = std::next(MI.getIterator());
  while (Next != MI.getParent()->end() && Next->isMetaInstruction())
    ++Next;
  SkipReturn = Next != MI.getParent()->end() && Next->getOpcode() == M6809::RTS;
  MCInst Call;
  Call.setOpcode(SkipReturn ? M6809::JMPm : M6809::JSRm);
  Call.addOperand(MCOperand::createReg(MCRegister()));
  Call.addOperand(MCOperand::createExpr(
      MCSymbolRefExpr::create(OutContext.getOrCreateSymbol(Name), OutContext)));
  Call.addOperand(MCOperand::createImm(M6809Mem::Auto));
  EmitToStreamer(*OutStreamer, Call);
  // A copy names its pair; clear and fill share one routine per register.
  if (!Cpy)
    LoopRoutines.insert(std::string("fill_") + regLetter(Dst));
}

void M6809AsmPrinter::emitEndOfAsmFile(Module &M) {
  auto Mem = [](MCInst &I, MCRegister Base) {
    I.addOperand(MCOperand::createReg(Base));
    I.addOperand(MCOperand::createImm(0));
    I.addOperand(MCOperand::createImm(M6809Mem::PostInc1));
  };
  auto Reg = [](char C) {
    return C == 'x' ? M6809::X : C == 'y' ? M6809::Y : M6809::U;
  };
  // No function is current here, so the module's subtarget encodes.
  const MCSubtargetInfo &STI = TM.getMCSubtargetInfo();
  auto Emit = [&](const MCInst &I) { OutStreamer->emitInstruction(I, STI); };
  auto Define = [&](MCSymbol *Sym) {
    OutStreamer->emitSymbolAttribute(Sym, MCSA_Weak);
    OutStreamer->emitSymbolAttribute(Sym, MCSA_Hidden);
    OutStreamer->emitSymbolAttribute(Sym, MCSA_ELF_TypeFunction);
    OutStreamer->emitLabel(Sym);
  };
  for (const std::string &R : LoopRoutines) {
    bool Cpy = StringRef(R).starts_with("__m6809_copy_");
    char D = R.back();
    std::string Routine = Cpy ? R : std::string("__m6809_fill_") + D;
    std::string Group = Routine;
    if (Cpy && CopyEntry)
      Group += "." + std::to_string(CopyEntry);
    OutStreamer->switchSection(OutContext.getELFSection(
        ".text." + Group, ELF::SHT_PROGBITS,
        ELF::SHF_ALLOC | ELF::SHF_EXECINSTR | ELF::SHF_GROUP, 0, Group,
        /*IsComdat=*/true));
    if (!Cpy) {
      Define(OutContext.getOrCreateSymbol(std::string("__m6809_clear_") + D));
      Emit(MCInstBuilder(M6809::CLR8).addReg(M6809::A));
    } else if (CopyEntry) {
      Define(OutContext.getOrCreateSymbol("__m6809_copy" +
                                          std::to_string(CopyEntry) + "_" +
                                          R.substr(R.size() - 2)));
      Emit(MCInstBuilder(M6809::LD8i).addReg(M6809::B).addImm(CopyEntry));
    }
    Define(OutContext.getOrCreateSymbol(Routine));
    // Branch to a local label: the global one is weak, so the assembler
    // could not resolve a branch to it.
    MCSymbol *Loop = OutContext.createTempSymbol();
    OutStreamer->emitLabel(Loop);
    if (Cpy) {
      MCInst Ld;
      Ld.setOpcode(M6809::LD8m);
      Ld.addOperand(MCOperand::createReg(M6809::A));
      Mem(Ld, Reg(R[R.size() - 2]));
      Emit(Ld);
    }
    MCInst St;
    St.setOpcode(M6809::ST8m);
    St.addOperand(MCOperand::createReg(M6809::A));
    Mem(St, Reg(D));
    Emit(St);
    Emit(MCInstBuilder(M6809::DEC8).addReg(M6809::B).addReg(M6809::B));
    Emit(MCInstBuilder(M6809::Bcc)
             .addImm(M6809CC::NE)
             .addExpr(MCSymbolRefExpr::create(Loop, OutContext)));
    Emit(MCInstBuilder(M6809::RTS));
  }
  LoopRoutines.clear();
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
