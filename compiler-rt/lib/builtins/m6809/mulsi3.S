//===-- mulsi3.S - 32-bit multiply ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// uint32_t __mulsi3(uint32_t a, uint32_t b). The high half of a is in D and
// the rest is on the stack: a.lo, b.hi, b.lo. The product returns in D (high)
// and X (low).
//
// With a = AH:AL and b = BH:BL in 16-bit halves, the low 32 bits are
// AL*BL + ((AH*BL + AL*BH) << 16), and only the low 16 bits of the two cross
// products matter.
//
//===----------------------------------------------------------------------===//

	.text
	.globl	__mulsi3
	.type	__mulsi3, @function
__mulsi3:
	pshs	d		; AH
	leas	-4,s		; 0..3,s = r, most significant byte first
	; Frame: AH at 4,s; return address 6,s; AL at 8,s; BH at 10,s; BL at 12,s.
	; r = AL * BL, from four byte products.
	lda	9,s
	ldb	13,s
	mul
	std	2,s		; al.lo * bl.lo
	lda	8,s
	ldb	12,s
	mul
	std	,s		; al.hi * bl.hi
	lda	8,s
	ldb	13,s
	mul			; al.hi * bl.lo, at bytes 1-2
	addd	1,s
	std	1,s
	bcc	1f
	inc	,s
1:	lda	9,s
	ldb	12,s
	mul			; al.lo * bl.hi, at bytes 1-2
	addd	1,s
	std	1,s
	bcc	2f
	inc	,s
2:	; r.hi += low 16 bits of AH * BL
	lda	5,s
	ldb	13,s
	mul
	addd	,s
	std	,s
	lda	4,s
	ldb	13,s
	mul
	addb	,s
	stb	,s
	lda	5,s
	ldb	12,s
	mul
	addb	,s
	stb	,s
	; r.hi += low 16 bits of AL * BH
	lda	9,s
	ldb	11,s
	mul
	addd	,s
	std	,s
	lda	8,s
	ldb	11,s
	mul
	addb	,s
	stb	,s
	lda	9,s
	ldb	10,s
	mul
	addb	,s
	stb	,s
	ldx	2,s
	ldd	,s
	leas	6,s
	rts
	.size	__mulsi3, .-__mulsi3
