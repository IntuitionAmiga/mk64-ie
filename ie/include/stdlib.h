/* Freestanding stdlib shim for the IE m68k build (see ie/ie_malloc.c). */
#ifndef IE_STDLIB_H
#define IE_STDLIB_H

#include <stddef.h>

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

/* Freestanding: exit() cannot return to a host; it aborts loudly. */
void exit(int status) __attribute__((noreturn));

#endif /* IE_STDLIB_H */
