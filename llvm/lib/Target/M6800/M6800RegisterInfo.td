//===-- M6800RegisterInfo.td - M6800 Register defs ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class M6800Reg<string n, list<Register> subregs = []> : Register<n> {
  let Namespace = "M6800";
  let SubRegs = subregs;
}

let Namespace = "M6800" in {
  def sub_lo : SubRegIndex<8>;
  def sub_hi : SubRegIndex<8, 8>;
}

// 8-bit accumulators. D = A:B with A as the high byte.
def A : M6800Reg<"a">, DwarfRegNum<[0]>;
def B : M6800Reg<"b">, DwarfRegNum<[1]>;

let SubRegIndices = [sub_hi, sub_lo], CoveredBySubRegs = 1 in
def D : M6800Reg<"d", [A, B]>, DwarfRegNum<[2]>;

// The index register, the stack pointer, the program counter and the
// condition codes. SP points at the first free byte below the stack.
def X : M6800Reg<"x">, DwarfRegNum<[3]>;
def SP : M6800Reg<"sp">, DwarfRegNum<[4]>;
def PC : M6800Reg<"pc">, DwarfRegNum<[5]>;
def CC : M6800Reg<"cc">, DwarfRegNum<[6]>;

// The frame pointer of a function with variable-sized stack objects. It is
// not a register but the word __m6800_fp in memory: the base of a stack slot
// until M6800FrameAccess loads it into X.
def FP : M6800Reg<"fp">;

//===----------------------------------------------------------------------===//
// Register classes
//===----------------------------------------------------------------------===//

// B first: it is the low byte of D, so i8 results land where i16 code wants
// them.
def ACC8 : RegisterClass<"M6800", [i8], 8, (add B, A)>;

def ACC16 : RegisterClass<"M6800", [i16], 8, (add D)>;

// The index register alone.
def IDX16 : RegisterClass<"M6800", [i16], 8, (add X)>;

// Every allocatable 16-bit register.
def GR16 : RegisterClass<"M6800", [i16], 8, (add D, X)>;

// Registers usable as the base of a memory operand. SP and FP (reserved, so
// never allocated) are the base of a stack slot until M6800FrameAccess
// rewrites it to X.
def PTR16 : RegisterClass<"M6800", [i16], 8, (add X, SP, FP)>;

def CCR : RegisterClass<"M6800", [i8], 8, (add CC)> {
  let CopyCost = -1;
  let isAllocatable = 0;
}

def SPR : RegisterClass<"M6800", [i16], 8, (add SP)> {
  let isAllocatable = 0;
}
