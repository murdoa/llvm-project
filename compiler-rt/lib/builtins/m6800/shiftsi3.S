//===-- shiftsi3.S - 32-bit shifts ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// __ashlsi3, __lshrsi3 and __ashrsi3(uint32_t a, int n): the high half of a
// in D, its low half and then n on the stack. The result comes back in D
// (high) and X (low). One bit per round.
//
//===----------------------------------------------------------------------===//

	.text

	.globl	__ashlsi3
	.type	__ashlsi3, @function
__ashlsi3:
	tsx
	ldx	2,x
	pshx
	pshb
	psha
	tsx			; 0..3,x = value, most significant byte first
	ldab	9,x		; count; only 0-31 is defined
	andb	#31
	beq	2f
1:	asl	3,x
	rol	2,x
	rol	1,x
	rol	0,x
	decb
	bne	1b
2:	pula
	pulb
	pulx
	rts
	.size	__ashlsi3, .-__ashlsi3

	.globl	__lshrsi3
	.type	__lshrsi3, @function
__lshrsi3:
	tsx
	ldx	2,x
	pshx
	pshb
	psha
	tsx
	ldab	9,x
	andb	#31
	beq	2f
1:	lsr	0,x
	ror	1,x
	ror	2,x
	ror	3,x
	decb
	bne	1b
2:	pula
	pulb
	pulx
	rts
	.size	__lshrsi3, .-__lshrsi3

	.globl	__ashrsi3
	.type	__ashrsi3, @function
__ashrsi3:
	tsx
	ldx	2,x
	pshx
	pshb
	psha
	tsx
	ldab	9,x
	andb	#31
	beq	2f
1:	asr	0,x
	ror	1,x
	ror	2,x
	ror	3,x
	decb
	bne	1b
2:	pula
	pulb
	pulx
	rts
	.size	__ashrsi3, .-__ashrsi3
