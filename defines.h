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
