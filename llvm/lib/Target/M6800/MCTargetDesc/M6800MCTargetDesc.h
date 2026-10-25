//===-- M6800MCTargetDesc.h - M6800 Target Descriptions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800MCTARGETDESC_H
#define LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800MCTARGETDESC_H

#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;

MCCodeEmitter *createM6800MCCodeEmitter(const MCInstrInfo &MCII,
                                        MCContext &Ctx);
MCAsmBackend *createM6800AsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                    const MCRegisterInfo &MRI,
                                    const MCTargetOptions &Options);
std::unique_ptr<MCObjectTargetWriter> createM6800ELFObjectWriter(uint8_t OSABI);

namespace M6800CC {
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

// The condition for "b cc' a" when CC is the one for "a cc b". The flag
// tests (VC, VS, PL, MI) have none; they come back unchanged.
inline CondCode getSwapped(CondCode CC) {
  switch (CC) {
  case HI:
    return LO;
  case LO:
    return HI;
  case LS:
    return HS;
  case HS:
    return LS;
  case GT:
    return LT;
  case LT:
    return GT;
  case GE:
    return LE;
  case LE:
    return GE;
  default:
    return CC;
  }
}

inline const char *getName(CondCode CC) {
  static const char *const Names[] = {"hi", "ls", "cc", "cs", "ne", "eq", "vc",
                                      "vs", "pl", "mi", "ge", "lt", "gt", "le"};
  return Names[CC];
}
} // namespace M6800CC

// The third part of a memory operand (base, offset, mode): which addressing
// mode to use. Code generation always uses Auto. The assembler sets the
// mode the source asked for.
namespace M6800Mem {
enum Mode {
  Auto = 0,     // base X: indexed; no base: extended
  Extended = 1, // >addr
  Direct = 2,   // <addr, an address below $100
};
} // namespace M6800Mem
} // namespace llvm

#define GET_REGINFO_ENUM
#include "M6800GenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "M6800GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "M6800GenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_M6800_MCTARGETDESC_M6800MCTARGETDESC_H
