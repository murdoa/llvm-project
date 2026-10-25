//===-- M6800.td - Describe the M6800 Target Machine -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Top-level description of the Motorola 6800 family, as implemented by the
// Hitachi HD6301: the 6801 instruction set (D, 16-bit arithmetic, MUL, ABX,
// PSHX/PULX) plus AIM/OIM/EIM/TIM, XGDX and SLP.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Descriptions
//===----------------------------------------------------------------------===//

include "M6800RegisterInfo.td"
include "M6800CallingConv.td"
include "M6800InstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<PTR16>;

def M6800InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Processors
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", NoSchedModel, []>;
def : ProcessorModel<"6301", NoSchedModel, []>;

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

def M6800 : Target {
  let InstructionSet = M6800InstrInfo;
}
