; RUN: llvm-mc -triple=m6809 -show-encoding %s | FileCheck %s

; Every operand form of the 6809. Encodings checked against the Motorola
; MC6809 datasheet.

	lda #10
; CHECK: lda #10 ; encoding: [0x86,0x0a]
	ldb #$ff
; CHECK: ldb #255 ; encoding: [0xc6,0xff]
	ldd #$1234
; CHECK: ldd #4660 ; encoding: [0xcc,0x12,0x34]
	ldx #%1010
; CHECK: ldx #10 ; encoding: [0x8e,0x00,0x0a]
	ldy #-1
; CHECK: ldy #-1 ; encoding: [0x10,0x8e,0xff,0xff]
	lds #$8000
; CHECK: lds #32768 ; encoding: [0x10,0xce,0x80,0x00]
	ldu #label
; CHECK: ldu #label ; encoding: [0xce,A,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: FK_Data_2
	lda <$40
; CHECK: lda <64 ; encoding: [0x96,0x40]
	lda >$40
; CHECK: lda >64 ; encoding: [0xb6,0x00,0x40]
	lda $1234
; CHECK: lda 4660 ; encoding: [0xb6,0x12,0x34]
	lda ,x
; CHECK: lda ,x ; encoding: [0xa6,0x84]
	lda 5,y
; CHECK: lda 5,y ; encoding: [0xa6,0x25]
	lda -16,u
; CHECK: lda -16,u ; encoding: [0xa6,0x50]
	lda 15,s
; CHECK: lda 15,s ; encoding: [0xa6,0x6f]
	lda 16,x
; CHECK: lda 16,x ; encoding: [0xa6,0x88,0x10]
	lda -128,x
; CHECK: lda -128,x ; encoding: [0xa6,0x88,0x80]
	lda 127,x
; CHECK: lda 127,x ; encoding: [0xa6,0x88,0x7f]
	lda 128,x
; CHECK: lda 128,x ; encoding: [0xa6,0x89,0x00,0x80]
	lda <0,x
; CHECK: lda <0,x ; encoding: [0xa6,0x88,0x00]
	lda <<0,x
; CHECK: lda <<0,x ; encoding: [0xa6,0x00]
	lda >0,x
; CHECK: lda >0,x ; encoding: [0xa6,0x89,0x00,0x00]
	lda ,x+
; CHECK: lda ,x+ ; encoding: [0xa6,0x80]
	lda ,x++
; CHECK: lda ,x++ ; encoding: [0xa6,0x81]
	lda ,-x
; CHECK: lda ,-x ; encoding: [0xa6,0x82]
	lda ,--x
; CHECK: lda ,--x ; encoding: [0xa6,0x83]
	lda a,x
; CHECK: lda a,x ; encoding: [0xa6,0x86]
	lda b,y
; CHECK: lda b,y ; encoding: [0xa6,0xa5]
	lda d,u
; CHECK: lda d,u ; encoding: [0xa6,0xcb]
	lda [,x]
; CHECK: lda [,x] ; encoding: [0xa6,0x94]
	lda [5,x]
; CHECK: lda [5,x] ; encoding: [0xa6,0x98,0x05]
	lda [,x++]
; CHECK: lda [,x++] ; encoding: [0xa6,0x91]
	lda [$1234]
; CHECK: lda [4660] ; encoding: [0xa6,0x9f,0x12,0x34]
	lda 10,pc
; CHECK: lda 10,pc ; encoding: [0xa6,0x8c,0x0a]
	lda label,pcr
; CHECK: lda label,pcr ; encoding: [0xa6,0x8d,A,A]
; CHECK-NEXT: fixup A - offset: 2, value: label, kind: fixup_pcrel16
	jmp [$fffe]
; CHECK: jmp [65534] ; encoding: [0x6e,0x9f,0xff,0xfe]
	jsr ,x
; CHECK: jsr ,x ; encoding: [0xad,0x84]
	jsr label
; CHECK: jsr label ; encoding: [0xbd,A,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: FK_Data_2
	leax 1,x
; CHECK: leax 1,x ; encoding: [0x30,0x01]
	leas -2,s
; CHECK: leas -2,s ; encoding: [0x32,0x7e]
	leau ,--s
; CHECK: leau ,--s ; encoding: [0x33,0xe3]
	inc <$10
; CHECK: inc <16 ; encoding: [0x0c,0x10]
	clr ,x
; CHECK: clr ,x ; encoding: [0x6f,0x84]
	tst label
; CHECK: tst label ; encoding: [0x7d,A,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: FK_Data_2
	nega
; CHECK: nega ; encoding: [0x40]
	clrb
; CHECK: clrb ; encoding: [0x5f]
	lsla
; CHECK: asla ; encoding: [0x48]
	sex
; CHECK: sex ; encoding: [0x1d]
	mul
; CHECK: mul ; encoding: [0x3d]
	abx
; CHECK: abx ; encoding: [0x3a]
	rts
; CHECK: rts ; encoding: [0x39]
	rti
; CHECK: rti ; encoding: [0x3b]
	swi
; CHECK: swi ; encoding: [0x3f]
	swi2
; CHECK: swi2 ; encoding: [0x10,0x3f]
	swi3
; CHECK: swi3 ; encoding: [0x11,0x3f]
	nop
; CHECK: nop ; encoding: [0x12]
	sync
; CHECK: sync ; encoding: [0x13]
	daa
; CHECK: daa ; encoding: [0x19]
	orcc #$50
; CHECK: orcc #80 ; encoding: [0x1a,0x50]
	andcc #$af
; CHECK: andcc #175 ; encoding: [0x1c,0xaf]
	cwai #$ef
; CHECK: cwai #239 ; encoding: [0x3c,0xef]
	tfr a,b
; CHECK: tfr a, b ; encoding: [0x1f,0x89]
	tfr d,x
; CHECK: tfr d, x ; encoding: [0x1f,0x01]
	exg x,y
; CHECK: exg x, y ; encoding: [0x1e,0x12]
	tfr s,d
; CHECK: tfr s, d ; encoding: [0x1f,0x40]
	tfr a,dp
; CHECK: tfr a, dp ; encoding: [0x1f,0x8b]
	pshs a,b,x,pc
; CHECK: pshs a,b,x,pc ; encoding: [0x34,0x96]
	puls d,u
; CHECK: puls a,b,u ; encoding: [0x35,0x46]
	pshu s,cc
; CHECK: pshu cc,s ; encoding: [0x36,0x41]
	pshs #$ff
; CHECK: pshs cc,a,b,dp,x,y,u,pc ; encoding: [0x34,0xff]
label:
	bra label
; CHECK: bra label ; encoding: [0x20,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel8
	beq label
; CHECK: beq label ; encoding: [0x27,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel8
	bcc label
; CHECK: bhs label ; encoding: [0x24,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel8
	blo label
; CHECK: blo label ; encoding: [0x25,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel8
	lbne label
; CHECK: lbne label ; encoding: [0x10,0x26,A,A]
; CHECK-NEXT: fixup A - offset: 2, value: label, kind: fixup_pcrel16
	lbra label
; CHECK: lbra label ; encoding: [0x16,A,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel16
	lbsr label
; CHECK: lbsr label ; encoding: [0x17,A,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel16
	bsr label
; CHECK: bsr label ; encoding: [0x8d,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel8
	brn label
; CHECK: brn label ; encoding: [0x21,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: fixup_pcrel8
	lbrn label
; CHECK: lbrn label ; encoding: [0x10,0x21,A,A]
; CHECK-NEXT: fixup A - offset: 2, value: label, kind: fixup_pcrel16
	cmpx #1
; CHECK: cmpx #1 ; encoding: [0x8c,0x00,0x01]
	cmpd #1
; CHECK: cmpd #1 ; encoding: [0x10,0x83,0x00,0x01]
	cmpy ,x
; CHECK: cmpy ,x ; encoding: [0x10,0xac,0x84]
	cmpu 2,s
; CHECK: cmpu 2,s ; encoding: [0x11,0xa3,0x62]
	cmps #0
; CHECK: cmps #0 ; encoding: [0x11,0x8c,0x00,0x00]
	sts ,x
; CHECK: sts ,x ; encoding: [0x10,0xef,0x84]
	sty 2,u
; CHECK: sty 2,u ; encoding: [0x10,0xaf,0x42]
	stu label
; CHECK: stu label ; encoding: [0xff,A,A]
; CHECK-NEXT: fixup A - offset: 1, value: label, kind: FK_Data_2
	addd 2,s
; CHECK: addd 2,s ; encoding: [0xe3,0x62]
	subd #1
; CHECK: subd #1 ; encoding: [0x83,0x00,0x01]
	bita #1
; CHECK: bita #1 ; encoding: [0x85,0x01]
	bitb ,x
; CHECK: bitb ,x ; encoding: [0xe5,0x84]
	sbca #0
; CHECK: sbca #0 ; encoding: [0x82,0x00]
	adcb ,s+
; CHECK: adcb ,s+ ; encoding: [0xe9,0xe0]
	sta ,-s
; CHECK: sta ,-s ; encoding: [0xa7,0xe2]
