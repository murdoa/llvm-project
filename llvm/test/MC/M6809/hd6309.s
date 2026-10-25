; RUN: llvm-mc -triple=m6809 -mcpu=6309 -show-encoding %s | FileCheck %s
; RUN: llvm-mc -triple=m6809 -mcpu=6309 -filetype=obj %s | llvm-objdump -d --mcpu=6309 - | FileCheck --check-prefix=DIS %s

; The HD6309 instructions and addressing modes. Encodings checked against
; lwasm --6309.

	negd
; CHECK: negd ; encoding: [0x10,0x40]
	comd
; CHECK: comd ; encoding: [0x10,0x43]
	lsrd
; CHECK: lsrd ; encoding: [0x10,0x44]
	rord
; CHECK: rord ; encoding: [0x10,0x46]
	asrd
; CHECK: asrd ; encoding: [0x10,0x47]
	asld
; CHECK: asld ; encoding: [0x10,0x48]
	rold
; CHECK: rold ; encoding: [0x10,0x49]
	decd
; CHECK: decd ; encoding: [0x10,0x4a]
	incd
; CHECK: incd ; encoding: [0x10,0x4c]
	tstd
; CHECK: tstd ; encoding: [0x10,0x4d]
	clrd
; CHECK: clrd ; encoding: [0x10,0x4f]
	comw
; CHECK: comw ; encoding: [0x10,0x53]
	lsrw
; CHECK: lsrw ; encoding: [0x10,0x54]
	rorw
; CHECK: rorw ; encoding: [0x10,0x56]
	rolw
; CHECK: rolw ; encoding: [0x10,0x59]
	decw
; CHECK: decw ; encoding: [0x10,0x5a]
	incw
; CHECK: incw ; encoding: [0x10,0x5c]
	tstw
; CHECK: tstw ; encoding: [0x10,0x5d]
	clrw
; CHECK: clrw ; encoding: [0x10,0x5f]
	come
; CHECK: come ; encoding: [0x11,0x43]
	dece
; CHECK: dece ; encoding: [0x11,0x4a]
	ince
; CHECK: ince ; encoding: [0x11,0x4c]
	tste
; CHECK: tste ; encoding: [0x11,0x4d]
	clre
; CHECK: clre ; encoding: [0x11,0x4f]
	comf
; CHECK: comf ; encoding: [0x11,0x53]
	decf
; CHECK: decf ; encoding: [0x11,0x5a]
	incf
; CHECK: incf ; encoding: [0x11,0x5c]
	tstf
; CHECK: tstf ; encoding: [0x11,0x5d]
	clrf
; CHECK: clrf ; encoding: [0x11,0x5f]
	sexw
; CHECK: sexw ; encoding: [0x14]
	pshsw
; CHECK: pshsw ; encoding: [0x10,0x38]
	pulsw
; CHECK: pulsw ; encoding: [0x10,0x39]
	pshuw
; CHECK: pshuw ; encoding: [0x10,0x3a]
	puluw
; CHECK: puluw ; encoding: [0x10,0x3b]
	sube #$12
; CHECK: sube #18 ; encoding: [0x11,0x80,0x12]
	sube <$40
; CHECK: sube <64 ; encoding: [0x11,0x90,0x40]
	sube 5,x
; CHECK: sube 5,x ; encoding: [0x11,0xa0,0x05]
	sube >$1234
; CHECK: sube >4660 ; encoding: [0x11,0xb0,0x12,0x34]
	cmpe #1
; CHECK: cmpe #1 ; encoding: [0x11,0x81,0x01]
	lde #2
; CHECK: lde #2 ; encoding: [0x11,0x86,0x02]
	lde ,y
; CHECK: lde ,y ; encoding: [0x11,0xa6,0xa4]
	adde #3
; CHECK: adde #3 ; encoding: [0x11,0x8b,0x03]
	subf #4
; CHECK: subf #4 ; encoding: [0x11,0xc0,0x04]
	cmpf <$41
; CHECK: cmpf <65 ; encoding: [0x11,0xd1,0x41]
	ldf 2,s
; CHECK: ldf 2,s ; encoding: [0x11,0xe6,0x62]
	addf >$2000
; CHECK: addf >8192 ; encoding: [0x11,0xfb,0x20,0x00]
	ste ,x
; CHECK: ste ,x ; encoding: [0x11,0xa7,0x84]
	stf <$42
; CHECK: stf <66 ; encoding: [0x11,0xd7,0x42]
	subw #$1234
; CHECK: subw #4660 ; encoding: [0x10,0x80,0x12,0x34]
	subw ,x
; CHECK: subw ,x ; encoding: [0x10,0xa0,0x84]
	cmpw #$100
; CHECK: cmpw #256 ; encoding: [0x10,0x81,0x01,0x00]
	cmpw <$40
; CHECK: cmpw <64 ; encoding: [0x10,0x91,0x40]
	sbcd #1
; CHECK: sbcd #1 ; encoding: [0x10,0x82,0x00,0x01]
	andd #$FF00
; CHECK: andd #65280 ; encoding: [0x10,0x84,0xff,0x00]
	bitd >$3000
; CHECK: bitd >12288 ; encoding: [0x10,0xb5,0x30,0x00]
	ldw #$5678
; CHECK: ldw #22136 ; encoding: [0x10,0x86,0x56,0x78]
	ldw 10,u
; CHECK: ldw 10,u ; encoding: [0x10,0xa6,0x4a]
	stw ,y
; CHECK: stw ,y ; encoding: [0x10,0xa7,0xa4]
	stw >$4000
; CHECK: stw >16384 ; encoding: [0x10,0xb7,0x40,0x00]
	eord #1
; CHECK: eord #1 ; encoding: [0x10,0x88,0x00,0x01]
	adcd ,x++
; CHECK: adcd ,x++ ; encoding: [0x10,0xa9,0x81]
	ord #$8000
; CHECK: ord #32768 ; encoding: [0x10,0x8a,0x80,0x00]
	addw #2
; CHECK: addw #2 ; encoding: [0x10,0x8b,0x00,0x02]
	addw [,x]
; CHECK: addw [,x] ; encoding: [0x10,0xab,0x94]
	muld #300
; CHECK: muld #300 ; encoding: [0x11,0x8f,0x01,0x2c]
	muld ,x
; CHECK: muld ,x ; encoding: [0x11,0xaf,0x84]
	divq #7
; CHECK: divq #7 ; encoding: [0x11,0x8e,0x00,0x07]
	divq <$44
; CHECK: divq <68 ; encoding: [0x11,0x9e,0x44]
	divd #9
; CHECK: divd #9 ; encoding: [0x11,0x8d,0x09]
	divd 3,x
; CHECK: divd 3,x ; encoding: [0x11,0xad,0x03]
	ldq #$12345678
; CHECK: ldq #305419896 ; encoding: [0xcd,0x12,0x34,0x56,0x78]
	ldq ,x
; CHECK: ldq ,x ; encoding: [0x10,0xec,0x84]
	ldq >$5000
; CHECK: ldq >20480 ; encoding: [0x10,0xfc,0x50,0x00]
	stq 4,s
; CHECK: stq 4,s ; encoding: [0x10,0xed,0x64]
	stq <$46
; CHECK: stq <70 ; encoding: [0x10,0xdd,0x46]
	addr x,d
; CHECK: addr x, d ; encoding: [0x10,0x30,0x10]
	adcr y,x
; CHECK: adcr y, x ; encoding: [0x10,0x31,0x21]
	subr a,b
; CHECK: subr a, b ; encoding: [0x10,0x32,0x89]
	sbcr e,f
; CHECK: sbcr e, f ; encoding: [0x10,0x33,0xef]
	andr w,d
; CHECK: andr w, d ; encoding: [0x10,0x34,0x60]
	orr d,w
; CHECK: orr d, w ; encoding: [0x10,0x35,0x06]
	eorr u,y
; CHECK: eorr u, y ; encoding: [0x10,0x36,0x32]
	cmpr x,y
; CHECK: cmpr x, y ; encoding: [0x10,0x37,0x12]
	tfm x+,y+
; CHECK: tfm x+, y+ ; encoding: [0x11,0x38,0x12]
	tfm u-,d-
; CHECK: tfm u-, d- ; encoding: [0x11,0x39,0x30]
	tfm s+,x
; CHECK: tfm s+, x ; encoding: [0x11,0x3a,0x41]
	tfm y,u+
; CHECK: tfm y, u+ ; encoding: [0x11,0x3b,0x23]
	oim #$01,<$40
; CHECK: oim #1, <64 ; encoding: [0x01,0x01,0x40]
	aim #$fe,5,x
; CHECK: aim #254, 5,x ; encoding: [0x62,0xfe,0x05]
	eim #$ff,>$1234
; CHECK: eim #255, >4660 ; encoding: [0x75,0xff,0x12,0x34]
	tim #$80,,u
; CHECK: tim #128, ,u ; encoding: [0x6b,0x80,0xc4]
	ldmd #0
; CHECK: ldmd #0 ; encoding: [0x11,0x3d,0x00]
	bitmd #$80
; CHECK: bitmd #128 ; encoding: [0x11,0x3c,0x80]
	lda ,w
; CHECK: lda ,w ; encoding: [0xa6,0x8f]
	lda 300,w
; CHECK: lda 300,w ; encoding: [0xa6,0xaf,0x01,0x2c]
	ldb ,w++
; CHECK: ldb ,w++ ; encoding: [0xe6,0xcf]
	ldd ,--w
; CHECK: ldd ,--w ; encoding: [0xec,0xef]
	lda [,w]
; CHECK: lda [,w] ; encoding: [0xa6,0x90]
	lda [2,w]
; CHECK: lda [2,w] ; encoding: [0xa6,0xb0,0x00,0x02]
	ldx [,w++]
; CHECK: ldx [,w++] ; encoding: [0xae,0xd0]
	ldx [,--w]
; CHECK: ldx [,--w] ; encoding: [0xae,0xf0]
	lda e,x
; CHECK: lda e,x ; encoding: [0xa6,0x87]
	ldb f,y
; CHECK: ldb f,y ; encoding: [0xe6,0xaa]
	ldd w,u
; CHECK: ldd w,u ; encoding: [0xec,0xce]
	lda [e,s]
; CHECK: lda [e,s] ; encoding: [0xa6,0xf7]
	ldx [w,x]
; CHECK: ldx [w,x] ; encoding: [0xae,0x9e]
	tfr w,x
; CHECK: tfr w, x ; encoding: [0x1f,0x61]
	tfr x,w
; CHECK: tfr x, w ; encoding: [0x1f,0x16]
	exg v,d
; CHECK: exg v, d ; encoding: [0x1e,0x70]
	tfr a,e
; CHECK: tfr a, e ; encoding: [0x1f,0x8e]
	tfr f,b
; CHECK: tfr f, b ; encoding: [0x1f,0xf9]
	exg e,f
; CHECK: exg e, f ; encoding: [0x1e,0xef]
	leax w,y
; CHECK: leax w,y ; encoding: [0x30,0xae]

; DIS: negd
; DIS: tfm y, u+
; DIS: lda [e,s]
