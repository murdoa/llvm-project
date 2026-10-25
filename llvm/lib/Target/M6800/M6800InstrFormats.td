//===-- M6800InstrFormats.td - M6800 Instruction Formats ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Encodings are not given here: the encoder, assembler and disassembler
// share the opcode table in MCTargetDesc/M6800Opcodes.def.
//
//===----------------------------------------------------------------------===//

class M6800Inst<dag outs, dag ins, string asmstr, list<dag> pattern,
                int size = 0>
    : Instruction {
  let Namespace = "M6800";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Size = size;
}

class M6800Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : M6800Inst<outs, ins, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}
