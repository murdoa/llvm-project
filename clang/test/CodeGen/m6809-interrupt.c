// RUN: %clang_cc1 -triple m6809 -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple m6800 -emit-llvm %s -o - | FileCheck %s

// CHECK: define{{.*}} void @irq() [[IRQ:#[0-9]+]]
__attribute__((interrupt)) void irq(void) {}

// CHECK: define{{.*}} void @plain() [[PLAIN:#[0-9]+]]
void plain(void) {}

// CHECK: attributes [[IRQ]] = {{{.*}}"interrupt"
// CHECK-NOT: attributes [[PLAIN]] = {{{.*}}"interrupt"
