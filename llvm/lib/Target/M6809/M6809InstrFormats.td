//===-- M6809InstrFormats.td - M6809 Instruction Formats ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Encodings are not modelled yet. The backend only emits assembly for now.
// Byte encodings (opcode, 0x10/0x11 prefix pages, indexed postbytes) will go
// in here when the MC code emitter is added.
//
//===----------------------------------------------------------------------===//

class M6809Inst<dag outs, dag ins, string asmstr, list<dag> pattern,
                int size = 0>
    : Instruction {
  let Namespace = "M6809";
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Size = size;
}

class M6809Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : M6809Inst<outs, ins, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}
