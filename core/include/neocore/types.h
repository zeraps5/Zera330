#ifndef NEOCORE_TYPES_H
#define NEOCORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Tipos básicos usados em todo o core. Mantemos nomes curtos e explícitos
 * sobre largura de bits porque emulação de CPU depende de wraparound exato
 * (ex: um ADD.B tem que dar overflow em 8 bits, não em 32). */

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

#endif /* NEOCORE_TYPES_H */
