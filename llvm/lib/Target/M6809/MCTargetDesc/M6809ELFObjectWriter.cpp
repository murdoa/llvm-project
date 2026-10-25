//===-- M6809ELFObjectWriter.cpp - M6809 ELF writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6809FixupKinds.h"
#include "MCTargetDesc/M6809MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class M6809ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  M6809ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, OSABI, ELF::EM_M6809,
                                /*HasRelocationAddend=*/true) {}

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &,
                        bool IsPCRel) const override {
    switch (Fixup.getKind()) {
    case FK_Data_1:
      return ELF::R_M6809_8;
    case FK_Data_2:
      return ELF::R_M6809_16;
    case FK_Data_4:
      return ELF::R_M6809_32;
    case M6809::fixup_pcrel8:
      return ELF::R_M6809_PCREL8;
    case M6809::fixup_pcrel16:
      return ELF::R_M6809_PCREL16;
    case M6809::fixup_dp8:
      return ELF::R_M6809_DP8;
    default:
      llvm_unreachable("unknown fixup kind");
    }
  }
};
} // namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createM6809ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<M6809ELFObjectWriter>(OSABI);
}
