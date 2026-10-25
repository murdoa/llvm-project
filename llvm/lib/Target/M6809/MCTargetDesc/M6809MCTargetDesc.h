//===-- M6809MCTargetDesc.h - M6809 Target Descriptions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809MCTARGETDESC_H
#define LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"

namespace llvm {
class Target;

namespace M6809CC {
// Branch conditions, in the order of the Bcc opcodes' low nibble from 0x22.
enum CondCode {
  HI, // unsigned >
  LS, // unsigned <=
  HS, // unsigned >= (BCC)
  LO, // unsigned <  (BCS)
  NE,
  EQ,
  VC,
  VS,
  PL,
  MI,
  GE, // signed >=
  LT, // signed <
  GT, // signed >
  LE, // signed <=
};

inline CondCode getOpposite(CondCode CC) {
  // Each condition is paired with its opposite: HI/LS, HS/LO, NE/EQ, ...
  return static_cast<CondCode>(CC ^ 1);
}

inline const char *getName(CondCode CC) {
  static const char *const Names[] = {"hi", "ls", "hs", "lo", "ne",
                                      "eq", "vc", "vs", "pl", "mi",
                                      "ge", "lt", "gt", "le"};
  return Names[CC];
}
} // namespace M6809CC
} // namespace llvm

#define GET_REGINFO_ENUM
#include "M6809GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "M6809GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "M6809GenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809MCTARGETDESC_H
