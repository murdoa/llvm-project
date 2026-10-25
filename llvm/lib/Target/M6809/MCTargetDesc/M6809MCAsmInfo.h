//===-- M6809MCAsmInfo.h - M6809 asm properties -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809MCASMINFO_H
#define LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class Triple;

class M6809MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit M6809MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809MCASMINFO_H
