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
// a*b mod 2^16 = al*bl + ((ah*bl + al*bh) << 8). MUL does A*B -> D.
//
//===----------------------------------------------------------------------===//

	.text
	.globl	__mulhi3
	.type	__mulhi3, @function
__mulhi3:
	pshb
	psha
	tsx			; 0,x = ah, 1,x = al; b at 4,x (bh) and 5,x (bl)
	ldaa	1,x
	ldab	5,x
	mul			; al*bl
	pshb
	psha
	tsx			; 0,x = result; ah at 2,x, al 3,x, bh 6,x, bl 7,x
	ldaa	2,x
	ldab	7,x
	mul			; ah*bl: only its low byte reaches the result
	addb	0,x
	stab	0,x
	ldaa	3,x
	ldab	6,x
	mul			; al*bh
	addb	0,x
	stab	0,x
	pula
	pulb
	ins
	ins
	rts
	.size	__mulhi3, .-__mulhi3
