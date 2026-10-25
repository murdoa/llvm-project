// RUN: %clang_cc1 -triple m6809 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple m6800 -fsyntax-only -verify %s

// _BitInt goes up to 64 bits: wider ones would need 128-bit division
// routines, which the runtime does not have.

unsigned _BitInt(1) one;
signed _BitInt(37) odd;
unsigned _BitInt(64) widest;
unsigned _BitInt(65) too_wide; // expected-error {{unsigned _BitInt of bit sizes greater than 64 not supported}}
