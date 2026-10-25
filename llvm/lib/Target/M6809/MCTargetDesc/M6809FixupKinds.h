//===-- M6809FixupKinds.h - M6809 fixup kinds -------------------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809FIXUPKINDS_H
#define LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace M6809 {
enum Fixups {
  // An 8-bit branch or ,pcr offset, from the end of the instruction: the
  // byte holds S + A - (P + 1).
  fixup_pcrel8 = FirstTargetFixupKind,
  // A 16-bit one: S + A - (P + 2).
  fixup_pcrel16,
  // A direct-page address: the low byte of S + A. DP supplies the rest.
  fixup_dp8,

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
} // namespace M6809
} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809FIXUPKINDS_H
