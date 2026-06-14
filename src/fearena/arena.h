#ifndef FEARENA_H
#define FEARENA_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock {
    char *data;
    size_t used;
    size_t cap;
    ArenaBlock *next;
};

typedef struct {
    ArenaBlock *head;
    size_t block_size;
} Arena;

#define ARENA_ALIGN 8
#define ARENA_ALIGN_UP(n) (((size_t)(n)+(size_t)(ARENA_ALIGN)-1u)&~((size_t)(ARENA_ALIGN)-1u))

Arena arena_new(size_t block_size);
void arena_free(Arena *a);

static inline void *arena_alloc(Arena *a, size_t size) {
    size = ARENA_ALIGN_UP(size);
    ArenaBlock *b = a->head;
    if (__builtin_expect(b->used+size <= b->cap, 1)) {
        void *ptr = b->data + b->used;
        b->used += size;
        return ptr;
    }
    size_t cap = size > a->block_size ? size : a->block_size;
    ArenaBlock *nb = malloc(sizeof(ArenaBlock));
    nb->data = malloc(cap);
    nb->used = size;
    nb->cap = cap;
    nb->next = a->head;
    a->head = nb;
    return nb->data;
}

static inline char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *dst = arena_alloc(a, n+1);
    memcpy(dst, s, n);
    dst[n] = '\0';
    return dst;
}

static inline char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    return arena_strndup(a, s, strlen(s));
}
#endif
