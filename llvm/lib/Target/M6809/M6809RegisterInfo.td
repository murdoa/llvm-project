//===-- M6809RegisterInfo.td - M6809 Register defs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// HWEncoding is the 4-bit register code used in the TFR/EXG postbyte.
//
//===----------------------------------------------------------------------===//

class M6809Reg<bits<16> Enc, string n, list<Register> subregs = []>
    : Register<n> {
  let Namespace = "M6809";
  let HWEncoding = Enc;
  let SubRegs = subregs;
}

let Namespace = "M6809" in {
  def sub_lo : SubRegIndex<8>;
  def sub_hi : SubRegIndex<8, 8>;
}

// 8-bit accumulators. D = A:B with A as the high byte.
def A : M6809Reg<0x8, "a">, DwarfRegNum<[0]>;
def B : M6809Reg<0x9, "b">, DwarfRegNum<[1]>;

let SubRegIndices = [sub_hi, sub_lo], CoveredBySubRegs = 1 in
def D : M6809Reg<0x0, "d", [A, B]>, DwarfRegNum<[2]>;

// 16-bit index and stack registers. Y needs a 0x10 prefix on most
// instructions, so it costs a byte more than X or U.
def X : M6809Reg<0x1, "x">, DwarfRegNum<[3]>;
let CostPerUse = [1] in
def Y : M6809Reg<0x2, "y">, DwarfRegNum<[4]>;
def U : M6809Reg<0x3, "u">, DwarfRegNum<[5]>;
def S : M6809Reg<0x4, "s">, DwarfRegNum<[6]>;
def PC : M6809Reg<0x5, "pc">, DwarfRegNum<[7]>;

// Condition codes and direct page.
def CC : M6809Reg<0xA, "cc">, DwarfRegNum<[8]>;
def DP : M6809Reg<0xB, "dp">, DwarfRegNum<[9]>;

// TODO(HD6309): E, F (8-bit), W = E:F, V, Q = D:W and the zero register.
// They must be modelled with sub-registers like D so the allocator sees the
// overlaps, and only made allocatable when Feature6309 is enabled.

//===----------------------------------------------------------------------===//
// Register classes
//===----------------------------------------------------------------------===//

// B first: it is the low byte of D, so i8 results land where i16 code wants
// them.
def ACC8 : RegisterClass<"M6809", [i8], 8, (add B, A)>;

def ACC16 : RegisterClass<"M6809", [i16], 8, (add D)>;

// Every allocatable 16-bit register.
def GR16 : RegisterClass<"M6809", [i16], 8, (add D, X, U, Y)>;

// Index registers that LEA can write.
def IDX16 : RegisterClass<"M6809", [i16], 8, (add X, U, Y)>;

// Registers usable as the base of an indexed address.
def PTR16 : RegisterClass<"M6809", [i16], 8, (add X, U, Y, S)>;

// Anything TFR/EXG can move as a 16-bit value.
def XFER16 : RegisterClass<"M6809", [i16], 8, (add D, X, Y, U, S)>;

def CCR : RegisterClass<"M6809", [i8], 8, (add CC)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}

// Every register TFR and EXG can name. Only the assembler uses it.
def TFRREG : RegisterClass<"M6809", [i16], 8,
                           (add D, X, Y, U, S, PC, A, B, CC, DP)> {
  let isAllocatable = 0;
}
