// RUN: %clang_cc1 -E -dM -ffreestanding -triple m6809 < /dev/null \
// RUN:   | FileCheck --check-prefixes=COMMON,M6809 %s \
// RUN:     --implicit-check-not=__HD6309__ --implicit-check-not=__CHAR_UNSIGNED__
// RUN: %clang_cc1 -E -dM -ffreestanding -triple m6809 -target-cpu 6309 < /dev/null \
// RUN:   | FileCheck --check-prefixes=COMMON,HD6309 %s \
// RUN:     --implicit-check-not=__CHAR_UNSIGNED__
// RUN: %clang_cc1 -E -dM -ffreestanding -triple m6800 < /dev/null \
// RUN:   | FileCheck --check-prefixes=COMMON,M6800 %s \
// RUN:     --implicit-check-not=__CHAR_UNSIGNED__

// The macros come out in no fixed order. char is signed.

// M6809-DAG: #define __M6809__ 1
// M6809-DAG: #define __m6809__ 1
// HD6309-DAG: #define __HD6309__ 1
// HD6309-DAG: #define __M6809__ 1
// HD6309-DAG: #define __hd6309__ 1
// HD6309-DAG: #define __m6809__ 1
// M6800-DAG: #define __HD6301__ 1
// M6800-DAG: #define __M6800__ 1
// M6800-DAG: #define __hd6301__ 1
// M6800-DAG: #define __m6800__ 1

// COMMON-DAG: #define __BIGGEST_ALIGNMENT__ 1
// COMMON-DAG: #define __BIG_ENDIAN__ 1
// COMMON-DAG: #define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__
// COMMON-DAG: #define __INT32_TYPE__ long int
// COMMON-DAG: #define __INTPTR_TYPE__ int
// COMMON-DAG: #define __INT_MAX__ 32767
// COMMON-DAG: #define __LONG_MAX__ 2147483647L
// COMMON-DAG: #define __PTRDIFF_TYPE__ int
// COMMON-DAG: #define __SIZEOF_DOUBLE__ 8
// COMMON-DAG: #define __SIZEOF_FLOAT__ 4
// COMMON-DAG: #define __SIZEOF_INT__ 2
// COMMON-DAG: #define __SIZEOF_LONG_DOUBLE__ 8
// COMMON-DAG: #define __SIZEOF_LONG_LONG__ 8
// COMMON-DAG: #define __SIZEOF_LONG__ 4
// COMMON-DAG: #define __SIZEOF_POINTER__ 2
// COMMON-DAG: #define __SIZEOF_SHORT__ 2
// COMMON-DAG: #define __SIZEOF_SIZE_T__ 2
// COMMON-DAG: #define __SIZE_TYPE__ unsigned int

// 8- and 16-bit atomics are lock-free; 32-bit ones are not.
// COMMON-DAG: #define __CLANG_ATOMIC_CHAR_LOCK_FREE 2
// COMMON-DAG: #define __CLANG_ATOMIC_INT_LOCK_FREE 2
// COMMON-DAG: #define __CLANG_ATOMIC_POINTER_LOCK_FREE 2
// COMMON-DAG: #define __CLANG_ATOMIC_LONG_LOCK_FREE 1
// COMMON-DAG: #define __BITINT_MAXWIDTH__ 64
