//===-- shiftsi3.S - 32-bit shifts ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// __ashlsi3, __lshrsi3 and __ashrsi3: the value's high half in D and low
// half on the stack, then the int shift count. The result returns in D (high)
// and X (low). The 6809 shifts one bit at a time, through memory here.
//
//===----------------------------------------------------------------------===//

	.text

	.globl	__ashlsi3
	.type	__ashlsi3, @function
__ashlsi3:
	ldx	2,s
	pshs	x, d		; 0..3,s = value, most significant byte first
	ldb	9,s		; count; only 0-31 is defined
	andb	#31
	beq	2f
1:	asl	3,s
	rol	2,s
	rol	1,s
	rol	,s
	decb
	bne	1b
2:	puls	d, x
	rts
	.size	__ashlsi3, .-__ashlsi3

	.globl	__lshrsi3
	.type	__lshrsi3, @function
__lshrsi3:
	ldx	2,s
	pshs	x, d
	ldb	9,s
	andb	#31
	beq	2f
1:	lsr	,s
	ror	1,s
	ror	2,s
	ror	3,s
	decb
	bne	1b
2:	puls	d, x
	rts
	.size	__lshrsi3, .-__lshrsi3

	.globl	__ashrsi3
	.type	__ashrsi3, @function
__ashrsi3:
	ldx	2,s
	pshs	x, d
	ldb	9,s
	andb	#31
	beq	2f
1:	asr	,s
	ror	1,s
	ror	2,s
	ror	3,s
	decb
	bne	1b
2:	puls	d, x
	rts
	.size	__ashrsi3, .-__ashrsi3
