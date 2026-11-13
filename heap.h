#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>

struct HeapItem {
    uint64_t val; // Timestamp d'expiration en millisecondes
    size_t *ref;  // Pointeur vers `Entry::heap_idx` pour mise à jour en O(log N)
};

void heap_update(HeapItem *a, size_t pos, size_t len);
void heap_upsert(std::vector<HeapItem> &a, size_t pos, HeapItem t);
void heap_delete(std::vector<HeapItem> &a, size_t pos);

