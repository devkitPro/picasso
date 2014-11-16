#pragma once
#include <stdint.h>

typedef uint64_t dword_t;
typedef uint32_t word_t;
typedef uint16_t hword_t;
typedef uint8_t byte_t;
typedef int64_t dlong_t;
typedef int32_t long_t;
typedef int16_t short_t;
typedef int8_t char_t;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

#define BIT(n) (1U << (n))

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define be_dword(a)  __builtin_bswap64(a)
#define be_word(a)  __builtin_bswap32(a)
#define be_hword(a) __builtin_bswap16(a)
#define le_dword(a)  (a)
#define le_word(a)  (a)
#define le_hword(a) (a)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define be_dword(a)  (a)
#define be_word(a)  (a)
#define be_hword(a) (a)
#define le_dword(a)  __builtin_bswap64(a)
#define le_word(a)  __builtin_bswap32(a)
#define le_hword(a) __builtin_bswap16(a)
#else
#error "What's the endianness of the platform you're targeting?"
#endif
