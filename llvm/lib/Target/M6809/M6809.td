//===-- M6809.td - Describe the M6809 Target Machine -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Top-level description of the Motorola 6809 and Hitachi HD6309.
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Subtarget features
//===----------------------------------------------------------------------===//

// HD6309 extensions that work in emulation mode (E/F/W/Q/V registers, extra
// arithmetic, block moves). Native-mode-only behaviour (different interrupt
// frame, faster cycle counts) will get its own feature later.
def Feature6309 : SubtargetFeature<"6309", "Has6309", "true",
                                   "Enable Hitachi HD6309 extensions">;

//===----------------------------------------------------------------------===//
// Descriptions
//===----------------------------------------------------------------------===//

include "M6809RegisterInfo.td"
include "M6809CallingConv.td"
include "M6809InstrInfo.td"
include "M6309InstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<PTR16>;

def M6809InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Processors
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", NoSchedModel, []>;
def : ProcessorModel<"6809", NoSchedModel, []>;
def : ProcessorModel<"6309", NoSchedModel, [Feature6309]>;

//===----------------------------------------------------------------------===//
// Target
//===----------------------------------------------------------------------===//

def M6809 : Target {
  let InstructionSet = M6809InstrInfo;
}
