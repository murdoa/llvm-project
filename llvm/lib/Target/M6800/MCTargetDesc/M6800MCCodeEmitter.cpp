//===-- M6800MCCodeEmitter.cpp - Convert M6800 MCInst to bytes ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An HD6301 instruction is an opcode byte, which depends on the register
// and the addressing mode, then 0-2 operand bytes (3 for AIM and the like).
// The opcodes come from the shared table in M6800Opcodes.def.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6800FixupKinds.h"
#include "MCTargetDesc/M6800MCTargetDesc.h"
#include "MCTargetDesc/M6800OpcodeTable.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

namespace {
class M6800MCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;

public:
  M6800MCCodeEmitter(MCContext &Ctx) : Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

private:
  void emitValue(const MCInst &MI, const MCOperand &Op, unsigned Size,
                 MCFixupKind Kind, SmallVectorImpl<char> &CB,
                 SmallVectorImpl<MCFixup> &Fixups) const;
  void encodeMemory(const MCInst &MI, const M6800::OpcodeEntry &E, unsigned Idx,
                    SmallVectorImpl<char> &CB, SmallVectorImpl<MCFixup> &Fixups,
                    const MCOperand *Imm8 = nullptr) const;
  void error(const MCInst &MI, const Twine &Msg) const {
    Ctx.reportError(MI.getLoc(), Msg);
  }
};
} // namespace

static void emitByte(SmallVectorImpl<char> &CB, unsigned B) {
  CB.push_back(static_cast<char>(B & 0xFF));
}

// The value of an operand if it is a constant, folding expressions such as
// "2+3".
static bool getConstant(const MCOperand &Op, int64_t &V) {
  if (Op.isImm()) {
    V = Op.getImm();
    return true;
  }
  return Op.isExpr() && Op.getExpr()->evaluateAsAbsolute(V);
}

// Writes Size bytes, big-endian, or a fixup when the value is not known yet.
void M6800MCCodeEmitter::emitValue(const MCInst &MI, const MCOperand &Op,
                                   unsigned Size, MCFixupKind Kind,
                                   SmallVectorImpl<char> &CB,
                                   SmallVectorImpl<MCFixup> &Fixups) const {
  bool PCRel = Kind == M6800::fixup_pcrel8;
  int64_t V;
  if (!PCRel && getConstant(Op, V)) {
    if (Kind == M6800::fixup_dir8 && !isUIntN(8, V))
      error(MI, "direct address must be below $100");
    else if (Size == 1 && !isIntN(8, V) && !isUIntN(8, V))
      error(MI, "value does not fit in 8 bits");
    if (Size == 2 && !isIntN(16, V) && !isUIntN(16, V))
      error(MI, "value does not fit in 16 bits");
    for (unsigned I = Size; I-- > 0;)
      emitByte(CB, V >> (8 * I));
    return;
  }
  const MCExpr *Expr =
      Op.isExpr() ? Op.getExpr() : MCConstantExpr::create(Op.getImm(), Ctx);
  Fixups.push_back(MCFixup::create(CB.size(), Expr, Kind, PCRel));
  for (unsigned I = 0; I != Size; ++I)
    emitByte(CB, 0);
}

// The memory operand at Idx (base, offset, mode) and the opcode before it.
// Imm8, for AIM and the like, goes between the opcode and the address.
void M6800MCCodeEmitter::encodeMemory(const MCInst &MI,
                                      const M6800::OpcodeEntry &E, unsigned Idx,
                                      SmallVectorImpl<char> &CB,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCOperand *Imm8) const {
  const MCOperand &Base = MI.getOperand(Idx);
  const MCOperand &Off = MI.getOperand(Idx + 1);
  unsigned Mode = MI.getOperand(Idx + 2).getImm();
  MCRegister BaseReg = Base.isReg() ? Base.getReg() : MCRegister();

  auto EmitImm8 = [&] {
    if (Imm8)
      emitValue(MI, *Imm8, 1, FK_Data_1, CB, Fixups);
  };

  if (BaseReg) {
    if (BaseReg != M6800::X) {
      error(MI, "only X can be an index register");
      return;
    }
    emitByte(CB, M6800::getIndexedCode(E));
    EmitImm8();
    int64_t V;
    if (getConstant(Off, V) && !isUIntN(8, V)) {
      error(MI, "index offset must be 0 to 255");
      return;
    }
    emitValue(MI, Off, 1, FK_Data_1, CB, Fixups);
    return;
  }

  bool Direct = Mode == M6800Mem::Direct;
  if (Mode == M6800Mem::Auto) {
    // A known address below $100 takes the shorter direct form. AIM and the
    // like have no extended form at all.
    int64_t V;
    Direct = !M6800::hasExtended(E) ||
             (M6800::hasDirect(E) && getConstant(Off, V) && isUIntN(8, V));
  }
  if (Direct) {
    if (!M6800::hasDirect(E)) {
      error(MI, "instruction has no direct addressing mode");
      return;
    }
    emitByte(CB, M6800::getDirectCode(E));
    EmitImm8();
    emitValue(MI, Off, 1, MCFixupKind(M6800::fixup_dir8), CB, Fixups);
    return;
  }
  if (!M6800::hasExtended(E)) {
    error(MI, "instruction has no extended addressing mode");
    return;
  }
  emitByte(CB, M6800::getExtendedCode(E));
  emitValue(MI, Off, 2, FK_Data_2, CB, Fixups);
}

void M6800MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  // The register that picks the row is the first register operand, unless
  // that is the base of a memory operand (JSR 0,X).
  unsigned Reg = M6800::NoRegister;
  unsigned FirstNonReg = MI.getNumOperands();
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    if (!MI.getOperand(I).isReg()) {
      FirstNonReg = I;
      break;
    }
  }
  const M6800::OpcodeEntry *Entry = nullptr;
  if (FirstNonReg > 0)
    Entry =
        M6800::findOpcodeEntry(MI.getOpcode(), MI.getOperand(0).getReg().id());
  if (!Entry)
    Entry = M6800::findOpcodeEntry(MI.getOpcode(), Reg);
  if (!Entry) {
    error(MI, "cannot encode this instruction");
    return;
  }
  const M6800::OpcodeEntry &E = *Entry;
  unsigned N = MI.getNumOperands();

  switch (E.Form) {
  case M6800::Inh:
    emitByte(CB, E.Code);
    return;
  case M6800::Imm8:
  case M6800::Imm16:
    emitByte(CB, E.Code);
    emitValue(MI, MI.getOperand(N - 1), E.Form == M6800::Imm8 ? 1 : 2,
              E.Form == M6800::Imm8 ? FK_Data_1 : FK_Data_2, CB, Fixups);
    return;
  case M6800::Mem:
  case M6800::MemIX:
    encodeMemory(MI, E, N - 3, CB, Fixups);
    return;
  case M6800::ImmMem:
    encodeMemory(MI, E, N - 3, CB, Fixups, &MI.getOperand(0));
    return;
  case M6800::Rel8:
    emitByte(CB, E.Code);
    emitValue(MI, MI.getOperand(N - 1), 1, MCFixupKind(M6800::fixup_pcrel8), CB,
              Fixups);
    return;
  case M6800::Cond8:
    emitByte(CB, E.Code + MI.getOperand(0).getImm());
    emitValue(MI, MI.getOperand(1), 1, MCFixupKind(M6800::fixup_pcrel8), CB,
              Fixups);
    return;
  case M6800::Abs16:
    emitByte(CB, E.Code);
    emitValue(MI, MI.getOperand(N - 1), 2, FK_Data_2, CB, Fixups);
    return;
  case M6800::LongCond: {
    // b<opposite> over the jmp, then jmp.
    unsigned CC = MI.getOperand(0).getImm();
    emitByte(CB, E.Code + M6800CC::getOpposite(M6800CC::CondCode(CC)));
    emitByte(CB, 3);
    emitByte(CB, 0x7E);
    emitValue(MI, MI.getOperand(1), 2, FK_Data_2, CB, Fixups);
    return;
  }
  }
}

MCCodeEmitter *llvm::createM6800MCCodeEmitter(const MCInstrInfo &MCII,
                                              MCContext &Ctx) {
  return new M6800MCCodeEmitter(Ctx);
}
