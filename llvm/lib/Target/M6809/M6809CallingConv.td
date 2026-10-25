//===-- M6809CallingConv.td - Calling Conventions M6809 ----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PROVISIONAL ABI, expected to change:
//   - The first scalar argument goes in B (i8) or D (i16). Because B is part
//     of D, using one makes the other unavailable. Variadic arguments always
//     go on the stack.
//   - All other arguments go on the stack, first argument at the lowest
//     address (just above the return address).
//   - i8 results return in B, i16 in D, and the two halves of an i32 in D
//     then X.
//   - U is callee-saved and reserved as a future frame pointer. Everything
//     else is caller-saved.
//
//===----------------------------------------------------------------------===//

def RetCC_M6809 : CallingConv<[
  CCIfType<[i1], CCPromoteToType<i8>>,
  CCIfType<[i8], CCAssignToReg<[B]>>,
  CCIfType<[i16], CCAssignToReg<[D, X]>>
]>;

def CC_M6809 : CallingConv<[
  CCIfType<[i1], CCPromoteToType<i8>>,
  CCIfArgVarArg<CCIfType<[i8], CCAssignToStack<1, 1>>>,
  CCIfArgVarArg<CCIfType<[i16], CCAssignToStack<2, 1>>>,
  CCIfType<[i8], CCAssignToReg<[B]>>,
  CCIfType<[i16], CCAssignToReg<[D]>>,
  CCIfType<[i8], CCAssignToStack<1, 1>>,
  CCIfType<[i16], CCAssignToStack<2, 1>>
]>;

def CSR_M6809 : CalleeSavedRegs<(add U)>;
