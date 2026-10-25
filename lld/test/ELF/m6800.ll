; REQUIRES: m6800
; RUN: llc -mtriple=m6800 -filetype=obj -o %t1.o %s
; RUN: ld.lld -o %t.exe --Ttext=0x1000 --Tdata=0x2000 --defsym=far=0x1100 \
; RUN:   --defsym=zp=0x40 %t1.o
; RUN: llvm-objdump -s %t.exe | FileCheck %s
; RUN: llvm-readelf -h %t.exe | FileCheck --check-prefix=HDR %s

;; The 6800 relocations: R_M6800_16 for "jsr far", "tst zp" and the
;; address in .data, and R_M6800_PCREL8 for the loop branch, which counts
;; from the end of the instruction.

; HDR: Machine: Motorola 6800

@zp = external global i8
@tab = global ptr @_start

declare void @far()

define void @_start() {
entry:
  br label %loop
loop:
  call void @far()
  %v = load volatile i8, ptr @zp
  %c = icmp eq i8 %v, 0
  br i1 %c, label %loop, label %done
done:
  ret void
}

;; jsr $1100; tst $0040; beq -8 (to $1000); rts
; CHECK:      Contents of section .text:
; CHECK-NEXT: 1000 bd11007d 004027f8 39
; CHECK:      Contents of section .data:
; CHECK-NEXT: 2000 1000
