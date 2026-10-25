//===-- M6800ELFObjectWriter.cpp - M6800 ELF writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/M6800FixupKinds.h"
#include "MCTargetDesc/M6800MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class M6800ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  M6800ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/false, OSABI, ELF::EM_M6800,
                                /*HasRelocationAddend=*/true) {}

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &,
                        bool IsPCRel) const override {
    switch (Fixup.getKind()) {
    case FK_Data_1:
      return ELF::R_M6800_8;
    case FK_Data_2:
      return ELF::R_M6800_16;
    case FK_Data_4:
      return ELF::R_M6800_32;
    case M6800::fixup_pcrel8:
      return ELF::R_M6800_PCREL8;
    case M6800::fixup_dir8:
      return ELF::R_M6800_DIR8;
    default:
      llvm_unreachable("unknown fixup kind");
    }
  }
};
} // namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createM6800ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<M6800ELFObjectWriter>(OSABI);
}
