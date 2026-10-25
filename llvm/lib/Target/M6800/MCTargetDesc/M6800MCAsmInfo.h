//===-- M6800MCAsmInfo.h - M6800 asm properties -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800MCASMINFO_H
#define LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class Triple;

class M6800MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit M6800MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800MCASMINFO_H
