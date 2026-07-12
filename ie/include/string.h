#ifndef IE_SHIM_STRING_H
#define IE_SHIM_STRING_H

/*
 * Freestanding <string.h> for the bare-metal Intuition Engine build;
 * implementations live in ie/ie_libc.c.
 */

#include <stddef.h>

void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int value, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
void* memmove(void* dst, const void* src, size_t n);
int strcmp(const char* a, const char* b);
char* strcpy(char* dst, const char* src);

#endif /* IE_SHIM_STRING_H */
