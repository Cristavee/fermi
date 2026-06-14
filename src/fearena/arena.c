#include <stdlib.h>
#include "arena.h"

Arena arena_new(size_t block_size) {
    Arena a;
    a.block_size = block_size;
    ArenaBlock *b = malloc(sizeof(ArenaBlock));
    b->data = malloc(block_size);
    b->used = 0;
    b->cap = block_size;
    b->next = NULL;
    a.head = b;
    return a;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b->data);
        free(b);
        b = next;
    }
    a->head = NULL;
}
