//===-- divmodhi4.S - 16-bit division and remainder -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// __udivhi3, __umodhi3, __divhi3 and __modhi3: the dividend in D, the
// divisor on the stack, the result in D. Signed results truncate toward
// zero, and the remainder takes the sign of the dividend, as in C.
//
//===----------------------------------------------------------------------===//

	.text

// D = dividend, X = divisor -> D = quotient, X = remainder. Shift-and-
// subtract, one quotient bit per round. Division by zero gives quotient
// 0xffff and the dividend as remainder.
	.type	__m6809_udivmodhi4, @function
__m6809_udivmodhi4:
	pshs	x		; divisor
	pshs	d		; dividend; its bits shift out as quotient bits shift in
	ldb	#16
	pshs	b		; 0,s = count, 1,s = dividend/quotient, 3,s = divisor
	clra
	clrb			; D = remainder
1:	asl	2,s		; next dividend bit into C; quotient bit 0 clear
	rol	1,s
	rolb
	rola
	bcs	2f		; the remainder overflowed 16 bits: it exceeds the divisor
	cmpd	3,s
	blo	3f
2:	subd	3,s
	inc	2,s		; quotient bit = 1
3:	dec	,s
	bne	1b
	tfr	d, x		; remainder
	leas	1,s
	puls	d		; quotient
	leas	2,s
	rts
	.size	__m6809_udivmodhi4, .-__m6809_udivmodhi4

	.globl	__udivhi3
	.type	__udivhi3, @function
__udivhi3:
	ldx	2,s
	bsr	__m6809_udivmodhi4
	rts
	.size	__udivhi3, .-__udivhi3

	.globl	__umodhi3
	.type	__umodhi3, @function
__umodhi3:
	ldx	2,s
	bsr	__m6809_udivmodhi4
	tfr	x, d
	rts
	.size	__umodhi3, .-__umodhi3

// The signed forms divide the magnitudes. Byte 0,s holds the sign the result
// takes in bit 7.
	.globl	__divhi3
	.type	__divhi3, @function
__divhi3:
	pshs	d		; 0,s = n; d at 4,s
	eora	4,s		; the quotient is negative if the signs differ
	pshs	a		; 0,s = sign, 1,s = n, 5,s = d
	bra	__m6809_divmod_signed
	.size	__divhi3, .-__divhi3

	.globl	__modhi3
	.type	__modhi3, @function
__modhi3:
	pshs	d		; 0,s = n; d at 4,s
	pshs	a		; the remainder has the sign of n
	bsr	__m6809_divmod_signed_body
	tfr	x, d
	tst	,s
	bpl	1f
	nega
	negb
	sbca	#0
1:	leas	3,s
	rts
	.size	__modhi3, .-__modhi3

// Shared tail of __divhi3: 0,s = sign, 1,s = n, 5,s = d.
__m6809_divmod_signed:
	bsr	__m6809_divmod_signed_body
	tst	,s
	bpl	1f
	nega
	negb
	sbca	#0
1:	leas	3,s
	rts

// Called with 2,s = sign, 3,s = n, 7,s = d (the return address at 0,s).
// Returns D = |n| / |d|, X = |n| % |d|.
__m6809_divmod_signed_body:
	ldd	7,s
	bpl	1f
	nega
	negb
	sbca	#0
1:	tfr	d, x		; X = |d|
	ldd	3,s
	bpl	2f
	nega
	negb
	sbca	#0
2:	bra	__m6809_udivmodhi4
