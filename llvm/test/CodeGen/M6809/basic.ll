; RUN: llc -mtriple=m6809 < %s | FileCheck %s
; RUN: llc -mtriple=m6809 -mcpu=6309 < %s | FileCheck %s

define void @ret_void() {
  ret void
}

define i8 @ret_i8() {
  ret i8 42
}

define i16 @ret_i16() {
  ret i16 4660
}

define i32 @ret_i32() {
  ret i32 305419896
}

define i8 @arg_i8(i8 %a) {
  ret i8 %a
}

define i16 @arg_i16(i16 %a) {
  ret i16 %a
}

define i16 @second_arg(i16 %a, i16 %b) {
  ret i16 %b
}

define i16 @add_imm(i16 %a) {
  %r = add i16 %a, 5
  ret i16 %r
}

define i16 @sub_imm(i16 %a) {
  %r = sub i16 %a, 300
  ret i16 %r
}

define i16 @add_stack_arg(i16 %a, i16 %b) {
  %r = add i16 %a, %b
  ret i16 %r
}

define i8 @add8_stack_arg(i8 %a, i8 %b) {
  %r = add i8 %a, %b
  ret i8 %r
}

define i8 @and8_imm(i8 %a) {
  %r = and i8 %a, 15
  ret i8 %r
}

define i16 @third_arg(i16 %a, i16 %b, i8 %c, i16 %d) {
  ret i16 %d
}
