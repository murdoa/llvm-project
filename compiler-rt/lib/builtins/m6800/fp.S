//===-- fp.S - The frame pointer word -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The 6800 has no register to spare for a frame pointer. A function with
// variable-sized stack objects keeps it here instead: SP + 1 after its
// prologue. The prologue saves the caller's value on the stack and the
// epilogue puts it back.
//
//===----------------------------------------------------------------------===//

	.bss
	.globl	__m6800_fp
	.type	__m6800_fp, @object
	.size	__m6800_fp, 2
__m6800_fp:
	.zero	2
