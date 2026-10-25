; RUN: llc -mtriple=m6809 -show-mc-encoding < %s | FileCheck %s
; RUN: llc -mtriple=m6809 -filetype=obj < %s | llvm-readelf -h -r - \
; RUN:   | FileCheck --check-prefix=ELF %s

; Machine code for the forms code generation uses.

; ELF: Data: 2's complement, big endian
; ELF: Machine: Motorola 6809
; ELF: R_M6809_16 {{.*}} g + 0
; ELF: R_M6809_16 {{.*}} h + 0
; ELF: R_M6809_16 {{.*}} g + 2

@g = global [2 x i16] zeroinitializer

define i16 @f(i16 %a, ptr %p, i8 %c) {
; CHECK-LABEL: f:
; CHECK: ldx 4,s ; encoding: [0xae,0x64]
; CHECK: addd ,x ; encoding: [0xe3,0x84]
; CHECK: subd g ; encoding: [0xb3,A,A]
; CHECK: pshs x ; encoding: [0x34,0x10]
; CHECK: eora ,s+ ; encoding: [0xa8,0xe0]
; CHECK: jsr h ; encoding: [0xbd,A,A]
; CHECK: rts ; encoding: [0x39]
  %v = load i16, ptr %p
  %s = add i16 %a, %v
  %g = load i16, ptr @g
  %t = sub i16 %s, %g
  %cc = zext i8 %c to i16
  %u = xor i16 %t, %cc
  %w = call i16 @h(i16 %u)
  ret i16 %w
}

declare i16 @h(i16)

; Long offsets and branches.
define void @far(ptr %p, i16 %n) {
; CHECK-LABEL: far:
; CHECK: ldx 2,s ; encoding: [0xae,0x62]
; CHECK: lbeq .LBB1_2 ; encoding: [0x10,0x27,A,A]
; CHECK-NEXT: fixup A - offset: 2, value: .LBB1_2, kind: fixup_pcrel16
; CHECK: stb -100,y ; encoding: [0xe7,0xa8,0x9c]
; CHECK: stb 300,y ; encoding: [0xe7,0xa9,0x01,0x2c]
; CHECK: stx g+2 ; encoding: [0xbf,A,A]
; CHECK-NEXT: fixup A - offset: 1, value: g+2, kind: FK_Data_2
entry:
  %z = icmp eq i16 %n, 0
  br i1 %z, label %done, label %body
body:
  %q = getelementptr i8, ptr %p, i16 300
  store i8 1, ptr %q
  %r = getelementptr i8, ptr %p, i16 -100
  store i8 2, ptr %r
  store i16 %n, ptr getelementptr (i16, ptr @g, i16 1)
  br label %done
done:
  ret void
}
