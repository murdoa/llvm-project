// The m6809 target uses the bare-metal toolchain: ld.lld, crt0.o, libc and
// the compiler-rt builtins, with no frame pointer by default.

// RUN: %clang -### --target=m6809 --sysroot=%S/Inputs/basic_baremetal_tree \
// RUN:   -ccc-install-dir %S/Inputs/basic_baremetal_tree/bin %s 2>&1 \
// RUN:   | FileCheck %s
// CHECK: "-cc1" "-triple" "m6809"
// CHECK-SAME: "-mframe-pointer=none"
// CHECK: ld.lld{{(.exe)?}}" {{.*}}"-Bstatic" "-m" "m6809elf"
// CHECK-SAME: "{{[^"]*}}crt0.o"
// CHECK-SAME: libclang_rt.builtins.a" "-lc"

// RUN: %clang -### --target=m6809 -mcpu=6309 %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CPU %s
// CPU: "-target-cpu" "6309"

// RUN: %clang -E -dM --target=m6809 -mcpu=6309 %s \
// RUN:   | FileCheck --check-prefix=MACROS %s
// MACROS: #define __HD6309__ 1
// MACROS: #define __M6809__ 1
// MACROS: #define __SIZEOF_INT__ 2
// MACROS: #define __SIZEOF_POINTER__ 2
