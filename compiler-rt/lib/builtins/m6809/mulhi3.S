//===-- mulhi3.S - 16-bit multiply ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// uint16_t __mulhi3(uint16_t a, uint16_t b): a in D, b on the stack, the low
// 16 bits of the product in D.
//
// a*b mod 2^16 = al*bl + ((ah*bl + al*bh) << 8). The 6809 MUL does A*B -> D.
//
//===----------------------------------------------------------------------===//

	.text
	.globl	__mulhi3
	.type	__mulhi3, @function
__mulhi3:
	pshs	d		; 0,s = ah, 1,s = al; b at 4,s (bh) and 5,s (bl)
	lda	1,s
	ldb	5,s
	mul			; al*bl
	pshs	d		; 0,s = result; ah at 2,s, al 3,s, bh 6,s, bl 7,s
	lda	2,s
	ldb	7,s
	mul			; ah*bl: only its low byte reaches the result
	addb	,s
	stb	,s
	lda	3,s
	ldb	6,s
	mul			; al*bh
	addb	,s
	stb	,s
	puls	d
	leas	2,s
	rts
	.size	__mulhi3, .-__mulhi3
