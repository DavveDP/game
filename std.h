#pragma once

// Essentials from C stdlib
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t  i8;

#define false 0
#define true 1

#define GB(n) (n * (1ull << 30))
#define MB(n) (n * (1ull << 20))
#define KB(n) (n * (1ull << 10))
#define LEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define MIN(X, Y) (X) < (Y) ? (X) : (Y)
#define MAX(X, Y) (X) > (Y) ? (X) : (Y)
#define CLAMP(X, A, B) (MIN(MAX((A),(X)), (B)))
#define ODD(X) ((X) & 1)
#define EVEN(X) (ODD(X)^1)

#define CONCAT_INNER(a, b) a##b
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define UNIQUE(name) CONCAT(name, __LINE__)

#define RUN_EVERY(Call, Freq, Delta)                       \
    do {                                                   \
        static double UNIQUE(__acc) = 0;                     \
        if (UNIQUE(__acc) >= (Freq)) {                     \
            Call;                                          \
            UNIQUE(__acc) = 0;                             \
        } else {                                           \
            UNIQUE(__acc) += (Delta);                      \
        }                                                  \
    } while(0)


