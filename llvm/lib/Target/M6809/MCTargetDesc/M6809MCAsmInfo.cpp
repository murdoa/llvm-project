//===-- M6809MCAsmInfo.cpp - M6809 asm properties -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "M6809MCAsmInfo.h"

using namespace llvm;

void M6809MCAsmInfo::anchor() {}

M6809MCAsmInfo::M6809MCAsmInfo(const Triple &TT,
                               const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  IsLittleEndian = false;
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 2;
  CommentString = ";";
  // $ff, %1010 and @17 as in Motorola assemblers.
  UseMotorolaIntegers = true;
  Data16bitsDirective = "\t.short\t";
  Data32bitsDirective = "\t.long\t";
  Data64bitsDirective = nullptr;
  ZeroDirective = "\t.space\t";
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
}
