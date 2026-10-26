; RUN: llc -mtriple=m6809 -m6809-direct-page-bytes=8 < %s | FileCheck %s
; RUN: llc -mtriple=m6809 < %s | FileCheck %s --check-prefix=OFF
; RUN: llc -mtriple=m6809 -m6809-direct-page-bytes=8 -filetype=obj < %s \
; RUN:   | llvm-readelf -S - | FileCheck %s --check-prefix=ELF

; With -m6809-direct-page-bytes=8, the variables loaded and stored most go
; to the direct page, as many as fit in 8 bytes less the 2 that @fixed
; already takes there. @hot (4 bytes, 3 accesses), @warm and @data (1 byte,
; 2 accesses each) beat @big (6 bytes, 4 accesses). @data has a value, so
; it goes to .dp.data, the others to .dp.bss. @ro is constant and @cold is
; only addressed, so they stay. .dp.bss takes no space in the object file,
; as .bss does.

; CHECK-LABEL: f:
; CHECK: {{st[dx]}} <hot+2
; CHECK: {{st[dx]}} <hot
; CHECK: {{st[dx]}} <fixed
; CHECK: stb <warm
; CHECK: {{st[dx]}} big
; CHECK: stb <data

; CHECK-LABEL: g:
; CHECK: .section .dp.bss,"aw",@nobits
; CHECK-NOT: .section
; CHECK: fixed:
; CHECK-NOT: .section
; CHECK: hot:
; CHECK-NOT: .section
; CHECK: warm:
; CHECK: .section .bss,"aw",@nobits
; CHECK-NOT: .section
; CHECK: big:
; CHECK: .section .dp.data,"aw",@progbits
; CHECK-NOT: .section
; CHECK: data:

; OFF-LABEL: f:
; OFF: {{st[dx]}} hot+2
; OFF: {{st[dx]}} <fixed
; OFF: stb warm
; OFF: stb data

; ELF: .dp.bss NOBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000007
; ELF: .dp.data PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} 000001

@fixed = global i16 0, section ".dp.bss"
@hot = global [2 x i16] zeroinitializer
@warm = global i8 0
@big = global [3 x i16] zeroinitializer
@data = global i8 1
@ro = constant i8 0
@cold = global i8 0

define void @f(i16 %v) {
  store i16 %v, ptr @hot
  %p = getelementptr [2 x i16], ptr @hot, i16 0, i16 1
  store i16 %v, ptr %p
  %w = load i16, ptr @hot
  store i16 %w, ptr @fixed
  store i8 0, ptr @warm
  store i8 1, ptr @warm
  store i16 %v, ptr @big
  %b1 = getelementptr [3 x i16], ptr @big, i16 0, i16 1
  store i16 %v, ptr %b1
  %b2 = getelementptr [3 x i16], ptr @big, i16 0, i16 2
  store i16 %v, ptr %b2
  store i16 %w, ptr %b2
  store i8 2, ptr @data
  %r = load i8, ptr @ro
  store i8 %r, ptr @data
  ret void
}

define ptr @g() {
  ret ptr @cold
}
