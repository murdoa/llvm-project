; RUN: not llc -mtriple=m6800 < %s 2>&1 | FileCheck %s

; The epilogue cannot free a frame over 252 bytes while X holds the low half
; of an i32 result: the copy of D and X above the frame needs an offset that
; fits the indexed mode.

; CHECK: error: {{.*}}in function big_frame_i32 i32 (i32): a frame this large with an i32 result

declare void @use(ptr)

define i32 @big_frame_i32(i32 %x) {
  %a = alloca [300 x i8]
  call void @use(ptr %a)
  ret i32 %x
}
