//===-- M6809Disassembler.cpp - Disassembler for M6809 --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decodes with the shared opcode table (M6809Opcodes.def). Branch targets and
// ,pcr operands are given as absolute addresses, so the output assembles back
// to the same bytes.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "MCTargetDesc/M6809OpcodeTable.h"
#include "TargetInfo/M6809TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "m6809-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

// What a code (with page prefix) decodes to.
struct Decoded {
  const M6809::OpcodeEntry *Entry = nullptr;
  enum Kind : uint8_t { Plain, Direct, Indexed, Extended } Mode = Plain;
  int8_t Cond = -1;
};

class M6809Disassembler : public MCDisassembler {
  std::unique_ptr<const MCInstrInfo> MCII;
  // Indexed by page (0, 0x10, 0x11) and opcode byte.
  Decoded Codes[3][256];

public:
  M6809Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                    const MCInstrInfo *MCII);

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // namespace

static unsigned pageIndex(uint16_t Code) {
  switch (Code >> 8) {
  case 0x10: return 1;
  case 0x11: return 2;
  default: return 0;
  }
}

M6809Disassembler::M6809Disassembler(const MCSubtargetInfo &STI,
                                     MCContext &Ctx, const MCInstrInfo *MCII)
    : MCDisassembler(STI, Ctx), MCII(MCII) {
  auto Set = [&](uint16_t Code, const M6809::OpcodeEntry &E, Decoded::Kind K,
                 int Cond = -1) {
    Decoded &D = Codes[pageIndex(Code)][Code & 0xFF];
    assert(!D.Entry && "two table entries with the same opcode");
    D.Entry = &E;
    D.Mode = K;
    D.Cond = Cond;
  };
  for (const M6809::OpcodeEntry &E : M6809::getOpcodeTable()) {
    switch (E.Form) {
    case M6809::Mem:
      Set(M6809::getDirectCode(E), E, Decoded::Direct);
      Set(M6809::getIndexedCode(E), E, Decoded::Indexed);
      Set(M6809::getExtendedCode(E), E, Decoded::Extended);
      break;
    case M6809::Idx:
      Set(E.Code, E, Decoded::Indexed);
      break;
    case M6809::Cond8:
    case M6809::Cond16:
      for (int CC = M6809CC::HI; CC <= M6809CC::LE; ++CC)
        Set(E.Code + 2 + CC, E, Decoded::Plain, CC);
      break;
    default:
      Set(E.Code, E, Decoded::Plain);
      break;
    }
  }
}

// The register with a TFR/EXG postbyte code.
static MCRegister getTransferRegister(unsigned Code) {
  static const unsigned Regs[16] = {
      M6809::D, M6809::X, M6809::Y, M6809::U, M6809::S, M6809::PC, 0, 0,
      M6809::A, M6809::B, M6809::CC, M6809::DP, 0, 0, 0, 0};
  return Regs[Code & 0xF];
}

DecodeStatus M6809Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                               ArrayRef<uint8_t> Bytes,
                                               uint64_t Address,
                                               raw_ostream &CStream) const {
  unsigned Pos = 0;
  auto Avail = [&](unsigned N) { return Pos + N <= Bytes.size(); };
  auto U8 = [&] { return Bytes[Pos++]; };
  auto U16 = [&] {
    unsigned V = Bytes[Pos] << 8 | Bytes[Pos + 1];
    Pos += 2;
    return V;
  };
  Size = 1;

  if (!Avail(1))
    return MCDisassembler::Fail;
  unsigned Page = 0;
  if (Bytes[0] == 0x10 || Bytes[0] == 0x11) {
    Page = Bytes[0] == 0x10 ? 1 : 2;
    ++Pos;
    if (!Avail(1))
      return MCDisassembler::Fail;
  }
  const Decoded &D = Codes[Page][U8()];
  const M6809::OpcodeEntry *E = D.Entry;
  if (!E)
    return MCDisassembler::Fail;

  MI.setOpcode(E->Opcode);
  unsigned NumOps = MCII->get(E->Opcode).getNumOperands();
  const MCRegisterInfo &MRI = *getContext().getRegisterInfo();
  auto Fixed = [&](unsigned N) {
    return M6809::addFixedRegisters(MI, *MCII, MRI, N, E->Reg);
  };
  auto Done = [&] {
    Size = Pos;
    return MCDisassembler::Success;
  };

  switch (E->Form) {
  case M6809::Inh:
    if (!Fixed(NumOps))
      return MCDisassembler::Fail;
    return Done();
  case M6809::Imm8:
  case M6809::Imm16: {
    bool Wide = E->Form == M6809::Imm16;
    if (!Avail(Wide ? 2 : 1) || !Fixed(NumOps - 1))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(Wide ? U16() : U8()));
    return Done();
  }
  case M6809::Rel8:
  case M6809::Rel16:
  case M6809::Cond8:
  case M6809::Cond16: {
    bool Wide = E->Form == M6809::Rel16 || E->Form == M6809::Cond16;
    if (!Avail(Wide ? 2 : 1))
      return MCDisassembler::Fail;
    if (D.Cond >= 0)
      MI.addOperand(MCOperand::createImm(D.Cond));
    int64_t Off = Wide ? SignExtend64<16>(U16()) : SignExtend64<8>(U8());
    // Pos is now the end of the instruction.
    MI.addOperand(MCOperand::createImm((Address + Pos + Off) & 0xFFFF));
    return Done();
  }
  case M6809::RegPair: {
    if (!Avail(1))
      return MCDisassembler::Fail;
    unsigned Post = U8();
    MCRegister R1 = getTransferRegister(Post >> 4);
    MCRegister R2 = getTransferRegister(Post);
    // Mixing 8- and 16-bit registers is not a defined 6809 transfer.
    if (!R1 || !R2 || ((Post ^ (Post << 4)) & 0x80))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createReg(R1));
    MI.addOperand(MCOperand::createReg(R2));
    return Done();
  }
  case M6809::RegList:
    if (!Avail(1))
      return MCDisassembler::Fail;
    MI.addOperand(MCOperand::createImm(U8()));
    return Done();
  case M6809::Mem:
  case M6809::Idx:
    break;
  }

  // Memory operand: base, offset, mode.
  if (!Fixed(NumOps - 3))
    return MCDisassembler::Fail;
  MCRegister Base;
  int64_t Off = 0;
  unsigned Mode = M6809Mem::Auto;
  if (D.Mode == Decoded::Direct) {
    if (!Avail(1))
      return MCDisassembler::Fail;
    Off = U8();
    Mode = M6809Mem::Direct;
  } else if (D.Mode == Decoded::Extended) {
    if (!Avail(2))
      return MCDisassembler::Fail;
    Off = U16();
    // Without ">" an address below 256 could read as direct.
    if (Off < 0x100)
      Mode = M6809Mem::Extended;
  } else {
    if (!Avail(1))
      return MCDisassembler::Fail;
    unsigned Post = U8();
    static const unsigned IndexRegs[4] = {M6809::X, M6809::Y, M6809::U,
                                          M6809::S};
    Base = IndexRegs[(Post >> 5) & 3];
    if (!(Post & 0x80)) {
      // n,R with a 5-bit offset. Auto would pick ,R for zero.
      Off = SignExtend64<5>(Post);
      Mode = Off == 0 ? M6809Mem::Off5 : M6809Mem::Auto;
    } else {
      bool Ind = Post & 0x10;
      switch (Post & 0x0F) {
      case 0x0:
      case 0x2:
        if (Ind)
          return MCDisassembler::Fail;
        Mode = (Post & 0x0F) == 0 ? M6809Mem::PostInc1 : M6809Mem::PreDec1;
        break;
      case 0x1: Mode = M6809Mem::PostInc2; break;
      case 0x3: Mode = M6809Mem::PreDec2; break;
      case 0x4: Mode = M6809Mem::Auto; break;
      case 0x5: Mode = M6809Mem::AccB; break;
      case 0x6: Mode = M6809Mem::AccA; break;
      case 0xB: Mode = M6809Mem::AccD; break;
      case 0x8:
        if (!Avail(1))
          return MCDisassembler::Fail;
        Off = SignExtend64<8>(U8());
        // Auto gives 8 bits only to offsets that do not fit in 5 (or any
        // non-zero offset when indirect).
        Mode = Off != 0 && (Ind || !isInt<5>(Off)) ? M6809Mem::Auto
                                                    : M6809Mem::Off8;
        break;
      case 0x9:
        if (!Avail(2))
          return MCDisassembler::Fail;
        Off = SignExtend64<16>(U16());
        Mode = isInt<8>(Off) ? M6809Mem::Off16 : M6809Mem::Auto;
        break;
      case 0xC:
        if (!Avail(1))
          return MCDisassembler::Fail;
        Base = M6809::PC;
        Off = SignExtend64<8>(U8());
        Mode = M6809Mem::Off8;
        break;
      case 0xD:
        // label,pcr: the operand is the target address.
        if (!Avail(2))
          return MCDisassembler::Fail;
        Off = SignExtend64<16>(U16());
        Off = (Address + Pos + Off) & 0xFFFF;
        Base = M6809::PC;
        Mode = M6809Mem::PCRel;
        break;
      case 0xF:
        // [nn]
        if (Post != 0x9F || !Avail(2))
          return MCDisassembler::Fail;
        Base = MCRegister();
        Off = U16();
        Mode = M6809Mem::Auto;
        break;
      default:
        return MCDisassembler::Fail;
      }
      if (Ind)
        Mode |= M6809Mem::Indirect;
    }
  }
  MI.addOperand(MCOperand::createReg(Base));
  MI.addOperand(MCOperand::createImm(Off));
  MI.addOperand(MCOperand::createImm(Mode));
  return Done();
}

static MCDisassembler *createM6809Disassembler(const Target &T,
                                               const MCSubtargetInfo &STI,
                                               MCContext &Ctx) {
  return new M6809Disassembler(STI, Ctx, T.createMCInstrInfo());
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeM6809Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheM6809Target(),
                                         createM6809Disassembler);
}
