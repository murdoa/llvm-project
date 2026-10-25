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

// HD6309 registers: E and F, W = E:F, and V, which only TFR/EXG and the
// register-register ops reach. Q = D:W is only ever an implicit operand
// (LDQ, STQ, MULD, DIVQ), so it is not modelled. W is a fifth allocatable
// 16-bit register on the HD6309; like Y it needs a prefix byte. On the 6809
// all of them are reserved.
def E : M6809Reg<0xE, "e">, DwarfRegNum<[10]>;
def F : M6809Reg<0xF, "f">, DwarfRegNum<[11]>;
let SubRegIndices = [sub_hi, sub_lo], CoveredBySubRegs = 1, CostPerUse = [1] in
def W : M6809Reg<0x6, "w", [E, F]>, DwarfRegNum<[12]>;
def V : M6809Reg<0x7, "v">, DwarfRegNum<[13]>;

//===----------------------------------------------------------------------===//
// Register classes
//===----------------------------------------------------------------------===//

// B first: it is the low byte of D, so i8 results land where i16 code wants
// them.
def ACC8 : RegisterClass<"M6809", [i8], 8, (add B, A)> {
  // Only two, and both inside D: place bytes before the 16-bit values,
  // which then take X or Y rather than D when a byte is live.
  let AllocationPriority = 1;
}

def ACC16 : RegisterClass<"M6809", [i16], 8, (add D)>;

// Every allocatable 16-bit register. W exists only on the HD6309.
def GR16 : RegisterClass<"M6809", [i16], 8, (add D, X, U, Y, W)>;

// Index registers that LEA can write.
def IDX16 : RegisterClass<"M6809", [i16], 8, (add X, U, Y)>;

// Index registers that hold their value through the epilogue (U is
// callee-saved): the target of a sibling call through a register.
def TAILIDX16 : RegisterClass<"M6809", [i16], 8, (add X, Y)>;

// X and B alone: the operands of ABX.
def XREG : RegisterClass<"M6809", [i16], 8, (add X)>;
def BREG : RegisterClass<"M6809", [i8], 8, (add B)>;

// Registers usable as the base of an indexed address.
def PTR16 : RegisterClass<"M6809", [i16], 8, (add X, U, Y, S)>;

// Anything TFR/EXG can move as a 16-bit value.
def XFER16 : RegisterClass<"M6809", [i16], 8, (add D, X, Y, U, S, W)>;

def CCR : RegisterClass<"M6809", [i8], 8, (add CC)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}

// W alone, and D or W: the 16-bit registers with inherent operations on
// the HD6309. DW16 is also the GR16 subclass with byte halves, which
// INSERT_SUBREG and EXTRACT_SUBREG of a GR16 use.
def WREG : RegisterClass<"M6809", [i16], 8, (add W)> {
  let isAllocatable = 0;
}
def DW16 : RegisterClass<"M6809", [i16], 8, (add D, W)>;

// Every register TFR and EXG can name. Only the assembler uses it.
def TFRREG : RegisterClass<"M6809", [i16], 8,
                           (add D, X, Y, U, S, PC, A, B, CC, DP, W, V, E, F)> {
  let isAllocatable = 0;
}
