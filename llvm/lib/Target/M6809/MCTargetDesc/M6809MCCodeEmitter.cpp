//===-- M6809MCCodeEmitter.cpp - Convert M6809 MCInst to bytes ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The 6809 encoding is byte-oriented and irregular: an optional page prefix,
// an opcode that depends on the register and addressing mode, then for
// indexed addressing a postbyte and 0-2 offset bytes. The opcodes come from
// the shared table in M6809Opcodes.def, not from TableGen.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6809FixupKinds.h"
#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "MCTargetDesc/M6809OpcodeTable.h"
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
class M6809MCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;

public:
  M6809MCCodeEmitter(MCContext &Ctx) : Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

private:
  void emitValue(const MCInst &MI, const MCOperand &Op, unsigned Size,
                 MCFixupKind Kind, SmallVectorImpl<char> &CB,
                 SmallVectorImpl<MCFixup> &Fixups) const;
  void encodeMemory(const MCInst &MI, const M6809::OpcodeEntry &E,
                    SmallVectorImpl<char> &CB,
                    SmallVectorImpl<MCFixup> &Fixups,
                    const MCOperand *Imm8 = nullptr) const;
  void error(const MCInst &MI, const Twine &Msg) const {
    Ctx.reportError(MI.getLoc(), Msg);
  }
};
} // namespace

static void emitByte(SmallVectorImpl<char> &CB, unsigned B) {
  CB.push_back(static_cast<char>(B & 0xFF));
}

static void emitCode(SmallVectorImpl<char> &CB, uint16_t Code) {
  if (Code > 0xFF)
    emitByte(CB, Code >> 8); // page prefix
  emitByte(CB, Code);
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
void M6809MCCodeEmitter::emitValue(const MCInst &MI, const MCOperand &Op,
                                   unsigned Size, MCFixupKind Kind,
                                   SmallVectorImpl<char> &CB,
                                   SmallVectorImpl<MCFixup> &Fixups) const {
  bool PCRel = Kind == M6809::fixup_pcrel8 || Kind == M6809::fixup_pcrel16;
  int64_t V;
  if (!PCRel && getConstant(Op, V)) {
    if (Size == 1 && Kind != M6809::fixup_dp8 && !isIntN(8, V) &&
        !isUIntN(8, V))
      error(MI, "value does not fit in 8 bits");
    if (Size == 2 && !isIntN(16, V) && !isUIntN(16, V))
      error(MI, "value does not fit in 16 bits");
    if (Size == 4 && !isIntN(32, V) && !isUIntN(32, V))
      error(MI, "value does not fit in 32 bits");
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

// The rr field of an indexed postbyte.
static int getIndexBits(MCRegister Reg) {
  switch (Reg.id()) {
  case M6809::X: return 0x00;
  case M6809::Y: return 0x20;
  case M6809::U: return 0x40;
  case M6809::S: return 0x60;
  default: return -1;
  }
}

// A memory operand and the opcode before it. Imm8, for AIM and the like,
// goes between the opcode and the address bytes.
void M6809MCCodeEmitter::encodeMemory(const MCInst &MI,
                                      const M6809::OpcodeEntry &E,
                                      SmallVectorImpl<char> &CB,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCOperand *Imm8) const {
  unsigned N = MI.getNumOperands();
  MCRegister Base = MI.getOperand(N - 3).getReg();
  const MCOperand &Off = MI.getOperand(N - 2);
  unsigned Mode = MI.getOperand(N - 1).getImm();
  bool Ind = Mode & M6809Mem::Indirect;
  unsigned IndBit = Ind ? 0x10 : 0;
  Mode &= M6809Mem::ModeMask;
  bool IdxOnly = E.Form == M6809::Idx;

  auto EmitCode = [&](uint16_t Code) {
    emitCode(CB, Code);
    if (Imm8)
      emitValue(MI, *Imm8, 1, FK_Data_1, CB, Fixups);
  };
  auto Indexed = [&](unsigned Post) {
    EmitCode(M6809::getIndexedCode(E));
    emitByte(CB, Post);
  };

  // PC-relative: the operand is the target address, and the 16-bit offset
  // is counted from the end of the instruction.
  if (Mode == M6809Mem::PCRel) {
    Indexed(0x8D | IndBit);
    emitValue(MI, Off, 2, M6809::fixup_pcrel16, CB, Fixups);
    return;
  }

  if (!Base) {
    if (Ind) { // [nn]: extended indirect
      if (Mode != M6809Mem::Auto && Mode != M6809Mem::Extended)
        return error(MI, "invalid indirect address");
      Indexed(0x9F);
      emitValue(MI, Off, 2, FK_Data_2, CB, Fixups);
      return;
    }
    if (IdxOnly)
      return error(MI, "instruction needs an indexed address");
    if (Mode == M6809Mem::Direct) {
      EmitCode(M6809::getDirectCode(E));
      emitValue(MI, Off, 1, M6809::fixup_dp8, CB, Fixups);
      return;
    }
    if (Mode != M6809Mem::Auto && Mode != M6809Mem::Extended)
      return error(MI, "invalid address");
    EmitCode(M6809::getExtendedCode(E));
    emitValue(MI, Off, 2, FK_Data_2, CB, Fixups);
    return;
  }

  // W as the base (HD6309): ,W  n16,W  ,W++  ,--W, and each indirect.
  if (Base == M6809::W) {
    unsigned Ind1 = Ind ? 1 : 0;
    switch (Mode) {
    case M6809Mem::PostInc2:
      Indexed(0xCF + Ind1);
      return;
    case M6809Mem::PreDec2:
      Indexed(0xEF + Ind1);
      return;
    case M6809Mem::Auto:
    case M6809Mem::Off16: {
      int64_t V;
      if (Mode == M6809Mem::Auto && getConstant(Off, V) && V == 0) {
        Indexed(0x8F + Ind1);
        return;
      }
      Indexed(0xAF + Ind1);
      emitValue(MI, Off, 2, FK_Data_2, CB, Fixups);
      return;
    }
    default:
      return error(MI, "W takes only ,w  n,w  ,w++ and ,--w");
    }
  }

  // n,PC: a raw offset from the end of the instruction.
  if (Base == M6809::PC) {
    int64_t V;
    bool Known = getConstant(Off, V);
    bool Short = Mode == M6809Mem::Off8 || Mode == M6809Mem::Off5 ||
                 (Mode == M6809Mem::Auto && Known && isIntN(8, V));
    if (Mode != M6809Mem::Auto && Mode != M6809Mem::Off5 &&
        Mode != M6809Mem::Off8 && Mode != M6809Mem::Off16)
      return error(MI, "invalid PC-relative address");
    Indexed((Short ? 0x8C : 0x8D) | IndBit);
    emitValue(MI, Off, Short ? 1 : 2, Short ? FK_Data_1 : FK_Data_2, CB,
              Fixups);
    return;
  }

  int RR = getIndexBits(Base);
  if (RR < 0)
    return error(MI, "invalid index register");

  switch (Mode) {
  case M6809Mem::PostInc1:
  case M6809Mem::PreDec1:
    if (Ind)
      return error(MI, "single increment or decrement cannot be indirect");
    Indexed(RR | (Mode == M6809Mem::PostInc1 ? 0x80 : 0x82));
    return;
  case M6809Mem::PostInc2:
    Indexed(RR | 0x81 | IndBit);
    return;
  case M6809Mem::PreDec2:
    Indexed(RR | 0x83 | IndBit);
    return;
  case M6809Mem::AccB:
    Indexed(RR | 0x85 | IndBit);
    return;
  case M6809Mem::AccA:
    Indexed(RR | 0x86 | IndBit);
    return;
  case M6809Mem::AccD:
    Indexed(RR | 0x8B | IndBit);
    return;
  case M6809Mem::AccE:
    Indexed(RR | 0x87 | IndBit);
    return;
  case M6809Mem::AccF:
    Indexed(RR | 0x8A | IndBit);
    return;
  case M6809Mem::AccW:
    Indexed(RR | 0x8E | IndBit);
    return;
  case M6809Mem::Auto:
  case M6809Mem::Off5:
  case M6809Mem::Off8:
  case M6809Mem::Off16:
    break;
  default:
    return error(MI, "invalid indexed address");
  }

  int64_t V;
  bool Known = getConstant(Off, V);
  unsigned Width = Mode == M6809Mem::Off5    ? 5
                   : Mode == M6809Mem::Off8  ? 8
                   : Mode == M6809Mem::Off16 ? 16
                                             : 0;
  if (!Width) {
    // The shortest form that holds the offset. An unknown one gets 16 bits.
    if (!Known)
      Width = 16;
    else if (V == 0)
      Width = 1; // ,R
    else if (isIntN(5, V) && !Ind)
      Width = 5;
    else if (isIntN(8, V))
      Width = 8;
    else
      Width = 16;
  }
  switch (Width) {
  case 1:
    Indexed(RR | 0x84 | IndBit);
    return;
  case 5:
    if (Ind)
      return error(MI, "a 5-bit offset cannot be indirect");
    if (!Known || !isIntN(5, V))
      return error(MI, "offset does not fit in 5 bits");
    Indexed(RR | (V & 0x1F));
    return;
  case 8:
    Indexed(RR | 0x88 | IndBit);
    emitValue(MI, Off, 1, FK_Data_1, CB, Fixups);
    return;
  default:
    Indexed(RR | 0x89 | IndBit);
    emitValue(MI, Off, 2, FK_Data_2, CB, Fixups);
    return;
  }
}

void M6809MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                           SmallVectorImpl<char> &CB,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  unsigned Reg = M6809::NoRegister;
  for (const MCOperand &Op : MI)
    if (Op.isReg()) {
      Reg = Op.getReg().id();
      break;
    }
  const M6809::OpcodeEntry *E = M6809::findOpcodeEntry(MI.getOpcode(), Reg);
  if (!E)
    return error(MI, "instruction has no encoding");

  unsigned N = MI.getNumOperands();
  const MCRegisterInfo &MRI = *Ctx.getRegisterInfo();
  switch (E->Form) {
  case M6809::Inh:
    emitCode(CB, E->Code);
    return;
  case M6809::Imm8:
  case M6809::Imm16:
  case M6809::Imm32: {
    emitCode(CB, E->Code);
    unsigned Size = E->Form == M6809::Imm8    ? 1
                    : E->Form == M6809::Imm16 ? 2
                                              : 4;
    emitValue(MI, MI.getOperand(N - 1), Size,
              Size == 1   ? FK_Data_1
              : Size == 2 ? FK_Data_2
                          : FK_Data_4,
              CB, Fixups);
    return;
  }
  case M6809::ImmMem:
    encodeMemory(MI, *E, CB, Fixups, &MI.getOperand(0));
    return;
  case M6809::Mem:
  case M6809::Idx:
    encodeMemory(MI, *E, CB, Fixups);
    return;
  case M6809::Rel8:
  case M6809::Cond8:
  case M6809::Rel16:
  case M6809::Cond16: {
    uint16_t Code = E->Code;
    bool Cond = E->Form == M6809::Cond8 || E->Form == M6809::Cond16;
    if (Cond)
      Code += 2 + MI.getOperand(0).getImm(); // HI is 0x22
    emitCode(CB, Code);
    bool Long = E->Form == M6809::Rel16 || E->Form == M6809::Cond16;
    emitValue(MI, MI.getOperand(N - 1), Long ? 2 : 1,
              Long ? M6809::fixup_pcrel16 : M6809::fixup_pcrel8, CB, Fixups);
    return;
  }
  case M6809::RegPair:
  case M6809::Tfm: {
    emitCode(CB, E->Code);
    unsigned A = MRI.getEncodingValue(MI.getOperand(0).getReg());
    unsigned B = MRI.getEncodingValue(MI.getOperand(1).getReg());
    emitByte(CB, A << 4 | B);
    return;
  }
  case M6809::RegList:
    emitCode(CB, E->Code);
    emitByte(CB, MI.getOperand(0).getImm());
    return;
  }
  llvm_unreachable("unknown operand form");
}

MCCodeEmitter *llvm::createM6809MCCodeEmitter(const MCInstrInfo &MCII,
                                              MCContext &Ctx) {
  return new M6809MCCodeEmitter(Ctx);
}
