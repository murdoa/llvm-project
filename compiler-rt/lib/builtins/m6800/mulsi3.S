//===-- mulsi3.S - 32-bit multiply ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// uint32_t __mulsi3(uint32_t a, uint32_t b): the high half of a (AH) in D,
// then its low half (AL), BH and BL on the stack. The low 32 bits of the
// product come back in D (high) and X (low).
//
// a*b mod 2^32 = AL*BL + ((AH*BL + AL*BH) << 16), each 16-bit product built
// from 8x8 MULs.
//
//===----------------------------------------------------------------------===//

	.text
	.globl	__mulsi3
	.type	__mulsi3, @function
__mulsi3:
	pshb
	psha			; AH
	des
	des
	des
	des			; 0..3,x = r, most significant byte first
	tsx
	; Frame: AH at 4,x; return address 6,x; AL at 8,x; BH at 10,x; BL at 12,x.
	; r = AL * BL, from four byte products.
	ldaa	9,x
	ldab	13,x
	mul
	std	2,x		; al.lo * bl.lo
	ldaa	8,x
	ldab	12,x
	mul
	std	0,x		; al.hi * bl.hi
	ldaa	8,x
	ldab	13,x
	mul			; al.hi * bl.lo, at bytes 1-2
	addd	1,x
	std	1,x
	bcc	1f
	inc	0,x
1:	ldaa	9,x
	ldab	12,x
	mul			; al.lo * bl.hi, at bytes 1-2
	addd	1,x
	std	1,x
	bcc	2f
	inc	0,x
2:	; r.hi += low 16 bits of AH * BL
	ldaa	5,x
	ldab	13,x
	mul
	addd	0,x
	std	0,x
	ldaa	4,x
	ldab	13,x
	mul
	addb	0,x
	stab	0,x
	ldaa	5,x
	ldab	12,x
	mul
	addb	0,x
	stab	0,x
	; r.hi += low 16 bits of AL * BH
	ldaa	9,x
	ldab	11,x
	mul
	addd	0,x
	std	0,x
	ldaa	8,x
	ldab	11,x
	mul
	addb	0,x
	stab	0,x
	ldaa	9,x
	ldab	10,x
	mul
	addb	0,x
	stab	0,x
	pula
	pulb			; r.hi
	pulx			; r.lo
	ins
	ins
	rts
	.size	__mulsi3, .-__mulsi3
