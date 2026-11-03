// cgo has one set of flags per package, so a variant selects its ISA from the source.
#define ZXC_FUNCTION_SUFFIX _avx512
#ifdef __clang__
#pragma clang attribute push(                                                          \
    __attribute__((target("avx512f,avx512bw,avx512vbmi,avx512vbmi2,bmi,bmi2,lzcnt"))), \
    apply_to = function)
#define __AVX512F__ 1
#define __AVX512BW__ 1
#define __AVX512VBMI__ 1
#define __AVX512VBMI2__ 1
#define __BMI__ 1
#define __BMI2__ 1
#define __LZCNT__ 1
#else
#pragma GCC target("avx512f,avx512bw,avx512vbmi,avx512vbmi2,bmi,bmi2,lzcnt")
#endif
