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
	.type	__m6800_udivmodhi4, @function
__m6800_udivmodhi4:
	pshx			; divisor
	pshb
	psha			; dividend; its bits shift out as quotient bits shift in
	ldab	#16
	pshb
	tsx			; 0,x = count, 1,x = dividend/quotient, 3,x = divisor
	clra
	clrb			; D = remainder
1:	asl	2,x		; next dividend bit into C; quotient bit 0 clear
	rol	1,x
	rolb
	rola
	bcs	2f		; the remainder overflowed 16 bits: it exceeds the divisor
	subd	3,x
	bcc	3f
	addd	3,x		; it did not fit: put it back
	bra	4f
2:	subd	3,x
3:	inc	2,x		; quotient bit = 1
4:	dec	0,x
	bne	1b
	std	3,x		; remainder
	ins
	pula
	pulb			; quotient
	pulx			; remainder
	rts
	.size	__m6800_udivmodhi4, .-__m6800_udivmodhi4

	.globl	__udivhi3
	.type	__udivhi3, @function
__udivhi3:
	tsx
	ldx	2,x
	bra	__m6800_udivmodhi4
	.size	__udivhi3, .-__udivhi3

	.globl	__umodhi3
	.type	__umodhi3, @function
__umodhi3:
	tsx
	ldx	2,x
	bsr	__m6800_udivmodhi4
	xgdx
	rts
	.size	__umodhi3, .-__umodhi3

// The signed forms divide the magnitudes. They push d, n and the sign the
// result takes (in bit 7), then call __m6800_sdivmod.
	.globl	__divhi3
	.type	__divhi3, @function
__divhi3:
	tsx
	ldx	2,x		; d
	pshx
	pshb
	psha			; n
	tsx
	eora	2,x		; the quotient is negative if the signs differ
	psha
	bsr	__m6800_sdivmod
	bra	__m6800_sdivmod_end
	.size	__divhi3, .-__divhi3

	.globl	__modhi3
	.type	__modhi3, @function
__modhi3:
	tsx
	ldx	2,x		; d
	pshx
	pshb
	psha			; n
	psha			; the remainder has the sign of n
	bsr	__m6800_sdivmod
	xgdx
	; Fall through.

// Negates D if the sign byte is negative, drops sign, n and d, and returns.
__m6800_sdivmod_end:
	tsx
	tst	0,x
	bpl	1f
	nega
	negb
	sbca	#0
1:	ins
	ins
	ins
	ins
	ins
	rts
	.size	__modhi3, .-__modhi3

// Called with 2,x = sign, 3,x = n, 5,x = d after TSX (the return address
// at 0,x). Returns D = |n| / |d|, X = |n| % |d|.
__m6800_sdivmod:
	tsx
	ldd	5,x
	bpl	1f
	nega
	negb
	sbca	#0
1:	pshb
	psha			; |d|; n is now at 5,x
	tsx
	ldd	5,x
	bpl	2f
	nega
	negb
	sbca	#0
2:	pulx			; |d|
	bra	__m6800_udivmodhi4
