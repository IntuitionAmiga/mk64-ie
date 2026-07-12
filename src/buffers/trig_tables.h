#ifndef TRIG_TABLES_H
#define TRIG_TABLES_H

#include <PR/ultratypes.h>

/*
 * On N64 the sine and cosine tables overlapped: gSineTable was cut
 * short and reads overflowed into an adjacent gCosineTable, which is
 * undefined behavior under separate arrays. Here the full 0x1400
 * entries live in one array and gCosineTable is a pointer offset a
 * quarter turn (0x400 entries) into it, so every coss() read stays
 * inside the array and is well-defined.
 */
extern const f32 gSineTable[0x1400];
#define gCosineTable (gSineTable + 0x400)

extern const s16 gArctanTable[0x401];

#endif
