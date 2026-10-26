; RUN: llc -mtriple=m6809 -verify-machineinstrs < %s | FileCheck %s

; A value known to be 0 or 1 compared with 1 is compared with 0 instead,
; so the load's own flags take the branch or select and no cmpb is needed.

@flag = global i8 0
@g = global i8 0

define void @branch_bool() {
; CHECK-LABEL: branch_bool:
; CHECK:         ldb flag
; CHECK-NEXT:    beq
entry:
  %v = load i8, ptr @flag, !range !0, !noundef !1
  %b = trunc nuw i8 %v to i1
  br i1 %b, label %then, label %end
then:
  store volatile i8 1, ptr @g
  br label %end
end:
  ret void
}

define void @branch_masked(i8 %a) {
; CHECK-LABEL: branch_masked:
; CHECK:         andb #1
; CHECK-NEXT:    bne
entry:
  %m = and i8 %a, 1
  %c = icmp eq i8 %m, 1
  br i1 %c, label %end, label %then
then:
  store volatile i8 1, ptr @g
  br label %end
end:
  ret void
}

; Without the range the compare with 1 stays.
define void @branch_byte() {
; CHECK-LABEL: branch_byte:
; CHECK:         ldb flag
; CHECK-NEXT:    cmpb #1
entry:
  %v = load i8, ptr @flag
  %c = icmp eq i8 %v, 1
  br i1 %c, label %then, label %end
then:
  store volatile i8 1, ptr @g
  br label %end
end:
  ret void
}

define i8 @select_bool(i8 %x, i8 %y) {
; CHECK-LABEL: select_bool:
; CHECK:         lda flag
; CHECK-NEXT:    bne
entry:
  %v = load i8, ptr @flag, !range !0, !noundef !1
  %c = icmp eq i8 %v, 1
  %r = select i1 %c, i8 %x, i8 %y
  ret i8 %r
}

!0 = !{i8 0, i8 2}
!1 = !{}
