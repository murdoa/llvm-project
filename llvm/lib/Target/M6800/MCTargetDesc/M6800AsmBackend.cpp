//===-- M6800AsmBackend.cpp - M6800 assembler backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6800FixupKinds.h"
#include "MCTargetDesc/M6800MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {
class M6800AsmBackend : public MCAsmBackend {
public:
  M6800AsmBackend() : MCAsmBackend(llvm::endianness::big) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createM6800ELFObjectWriter(ELF::ELFOSABI_STANDALONE);
  }

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    static const MCFixupKindInfo Infos[M6800::NumTargetFixupKinds] = {
        // name             offset bits flags
        {"fixup_pcrel8", 0, 8, 0},
        {"fixup_dir8", 0, 8, 0},
    };
    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  // Short branches become long ones when the target is out of range or not
  // known (another section or symbol).
  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override {
    return getRelaxedOpcode(Opcode) != Opcode;
  }

  bool fixupNeedsRelaxationAdvanced(const MCFragment &, const MCFixup &Fixup,
                                    const MCValue &, uint64_t Value,
                                    bool Resolved) const override {
    // The offset counts from the end of the instruction (see applyFixup).
    return !Resolved || !isInt<8>(static_cast<int64_t>(Value) - 1);
  }

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override {
    Inst.setOpcode(getRelaxedOpcode(Inst.getOpcode()));
  }

  static unsigned getRelaxedOpcode(unsigned Opcode) {
    switch (Opcode) {
    case M6800::BRA: return M6800::LBRA;
    case M6800::Bcc: return M6800::LBcc;
    case M6800::BSR: return M6800::LBSR;
    default: return Opcode;
    }
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    for (uint64_t I = 0; I != Count; ++I)
      OS << '\x01'; // NOP
    return true;
  }
};
} // namespace

void M6800AsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                 const MCValue &Target, uint8_t *Data,
                                 uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);
  MCContext &Ctx = getContext();
  int64_t V = static_cast<int64_t>(Value);
  unsigned Size;
  switch (Fixup.getKind()) {
  case FK_Data_1:
    Size = 1;
    if (IsResolved && !isIntN(8, V) && !isUIntN(8, V))
      Ctx.reportError(Fixup.getLoc(), "value does not fit in 8 bits");
    break;
  case FK_Data_2:
    Size = 2;
    if (IsResolved && !isIntN(16, V) && !isUIntN(16, V))
      Ctx.reportError(Fixup.getLoc(), "value does not fit in 16 bits");
    break;
  case FK_Data_4:
    Size = 4;
    break;
  case M6800::fixup_dir8:
    Size = 1;
    if (IsResolved && !isUIntN(8, V))
      Ctx.reportError(Fixup.getLoc(), "direct address must be below $100");
    break;
  case M6800::fixup_pcrel8:
    Size = 1;
    // Value is S + A - P; the offset counts from the next byte.
    V -= 1;
    if (IsResolved && !isIntN(8, V))
      Ctx.reportError(Fixup.getLoc(), "branch target out of range");
    break;
  default:
    llvm_unreachable("unknown fixup kind");
  }
  if (!IsResolved)
    return; // The relocation holds the value; leave zeros.
  for (unsigned I = 0; I != Size; ++I)
    Data[I] = static_cast<uint8_t>(V >> (8 * (Size - 1 - I)));
}

MCAsmBackend *llvm::createM6800AsmBackend(const Target &T,
                                          const MCSubtargetInfo &STI,
                                          const MCRegisterInfo &MRI,
                                          const MCTargetOptions &Options) {
  return new M6800AsmBackend();
}
