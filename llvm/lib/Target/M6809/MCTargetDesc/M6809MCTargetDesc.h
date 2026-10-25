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

// The third part of a memory operand (base, offset, mode): which 6809
// addressing mode to use. Code generation always uses Auto. The assembler
// sets the mode the source asked for.
namespace M6809Mem {
enum Mode {
  Auto = 0,     // no base: extended; base: off,R in the shortest form
  Extended = 1, // >addr
  Direct = 2,   // <addr, the low byte, with DP supplying the high byte
  Off5 = 3,     // n,R with the offset forced to a given width
  Off8 = 4,
  Off16 = 5,
  PostInc1 = 6, // ,R+
  PostInc2 = 7, // ,R++
  PreDec1 = 8,  // ,-R
  PreDec2 = 9,  // ,--R
  AccA = 10,    // A,R
  AccB = 11,    // B,R
  AccD = 12,    // D,R
  PCRel = 13,   // addr,PCR: the offset operand is the target address
  ModeMask = 0x3F,
  Indirect = 0x40, // [...]
};
} // namespace M6809Mem
} // namespace llvm

#define GET_REGINFO_ENUM
#include "M6809GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "M6809GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "M6809GenSubtargetInfo.inc"

namespace llvm {
namespace M6809 {
// The PSHS/PULS/PSHU/PULU postbyte bit for a register, or 0 if it cannot be
// pushed. Bit 6 names the other stack pointer: U for PSHS/PULS, S for
// PSHU/PULU. D is A and B together.
inline unsigned getPushPullMask(unsigned Reg, bool UStack) {
  switch (Reg) {
  case M6809::CC: return 0x01;
  case M6809::A: return 0x02;
  case M6809::B: return 0x04;
  case M6809::D: return 0x06;
  case M6809::DP: return 0x08;
  case M6809::X: return 0x10;
  case M6809::Y: return 0x20;
  case M6809::U: return UStack ? 0 : 0x40;
  case M6809::S: return UStack ? 0x40 : 0;
  case M6809::PC: return 0x80;
  default: return 0;
  }
}
} // namespace M6809
} // namespace llvm

#endif // LLVM_LIB_TARGET_M6809_MCTARGETDESC_M6809MCTARGETDESC_H
