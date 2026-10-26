//===-- M6809CallingConv.td - Calling Conventions M6809 ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The C calling convention; the README describes it in full.
//   - The first argument goes in B (i8) or D (i16). Because B is part of D,
//     using one makes the other unavailable. Variadic arguments always go
//     on the stack.
//   - All other arguments go on the stack, first argument at the lowest
//     address (just above the return address).
//   - i8 results return in B, i16 in D, and the two halves of an i32 in D
//     then X. Wider results are returned through a hidden pointer.
//   - U is callee-saved and is the frame pointer when a function needs one.
//     Everything else is caller-saved.
//
//===----------------------------------------------------------------------===//

def RetCC_M6809 : CallingConv<[
  CCIfType<[i1], CCPromoteToType<i8>>,
  CCIfType<[i8], CCAssignToReg<[B]>>,
  CCIfType<[i16], CCAssignToReg<[D, X]>>
]>;

def CC_M6809 : CallingConv<[
  // Structs passed by value are copied into the argument area.
  CCIfByVal<CCCustom<"CC_M6809_ByVal">>,
  CCIfType<[i1], CCPromoteToType<i8>>,
  CCIfArgVarArg<CCIfType<[i8], CCAssignToStack<1, 1>>>,
  CCIfArgVarArg<CCIfType<[i16], CCAssignToStack<2, 1>>>,
  CCIfType<[i8], CCAssignToReg<[B]>>,
  CCIfType<[i16], CCAssignToReg<[D]>>,
  CCIfType<[i8], CCAssignToStack<1, 1>>,
  CCIfType<[i16], CCAssignToStack<2, 1>>
]>;

// fastcc, which LTO gives internal functions whose every call it can see:
// more of the arguments in registers. Bytes take B then A, words X, D then
// Y, each while no part of the register is taken; the rest go on the stack
// as for C. X first leaves D free for the arithmetic, and a pointer argument
// is where indexing wants it.
def CC_M6809_Fast : CallingConv<[
  CCIfByVal<CCCustom<"CC_M6809_ByVal">>,
  CCIfType<[i1], CCPromoteToType<i8>>,
  CCIfType<[i8], CCAssignToReg<[B, A]>>,
  CCIfType<[i16], CCAssignToReg<[X, D, Y]>>,
  CCIfType<[i8], CCAssignToStack<1, 1>>,
  CCIfType<[i16], CCAssignToStack<2, 1>>
]>;

def CSR_M6809 : CalleeSavedRegs<(add U)>;
def CSR_NoRegs : CalleeSavedRegs<(add)>;
// An HD6309 interrupt handler: in emulation mode the CPU does not save W.
def CSR_HD6309_Interrupt : CalleeSavedRegs<(add W)>;
