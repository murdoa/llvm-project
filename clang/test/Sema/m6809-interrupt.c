// RUN: %clang_cc1 -triple m6809 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple m6800 -fsyntax-only -verify %s

__attribute__((interrupt)) void irq(void);
[[gnu::interrupt]] void nmi(void) {}

__attribute__((interrupt)) void with_param(int x); // expected-warning {{M6809 'interrupt' attribute only applies to functions that have no parameters}}
__attribute__((interrupt)) int with_result(void); // expected-warning {{M6809 'interrupt' attribute only applies to functions that have a 'void' return type}}
__attribute__((interrupt(1))) void with_arg(void); // expected-error {{'interrupt' attribute takes no arguments}}
__attribute__((interrupt)) int not_a_function; // expected-warning {{'interrupt' attribute only applies to functions}}
