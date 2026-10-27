# RUN: llvm-mc -triple m6809 -filetype=obj %s -o - | llvm-readelf -S - | FileCheck %s
# RUN: llvm-mc -triple m6800 -filetype=obj %s -o - | llvm-readelf -S - | FileCheck %s
# RUN: llc -mtriple=m6809 -filetype=obj %S/Inputs/text-align.ll -o - | llvm-readelf -S - | FileCheck %s
# RUN: llc -mtriple=m6800 -filetype=obj %S/Inputs/text-align.ll -o - | llvm-readelf -S - | FileCheck %s

# Code needs no alignment, so .text is byte aligned: objects linked one
# after another have no filler between them.

# CHECK: .text PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} {{[0-9a-f]+}} 00 AX 0 0 1{{$}}

  rts
