//===-- M6800FixupKinds.h - M6800 fixup kinds -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Absolute 8- and 16-bit values use the generic FK_Data_1 and FK_Data_2.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800FIXUPKINDS_H
#define LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace M6800 {
enum Fixups {
  // An 8-bit branch offset, from the end of the instruction: the byte holds
  // S + A - (P + 1).
  fixup_pcrel8 = FirstTargetFixupKind,
  // A direct-page address: S + A, which must be below $100.
  fixup_dir8,

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
} // namespace M6800
} // namespace llvm

#endif // LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800FIXUPKINDS_H
