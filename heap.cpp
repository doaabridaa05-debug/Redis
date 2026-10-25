#include "heap.h"
#include <algorithm>

static size_t heap_parent(size_t i) {
    return (i + 1) / 2 - 1;
}

static size_t heap_left(size_t i) {
    return i * 2 + 1;
}

static size_t heap_right(size_t i) {
    return i * 2 + 2;
}

static void heap_up(HeapItem *a, size_t pos) {
    HeapItem t = a[pos];
    while (pos > 0 && a[heap_parent(pos)].val > t.val) {
        a[pos] = a[heap_parent(pos)];
        if (a[pos].ref) {
            *a[pos].ref = pos;
        }
        pos = heap_parent(pos);
    }
    a[pos] = t;
    if (a[pos].ref) {
        *a[pos].ref = pos;
    }
}

static void heap_down(HeapItem *a, size_t pos, size_t len) {
    HeapItem t = a[pos];
    while (true) {
        size_t l = heap_left(pos);
        size_t r = heap_right(pos);
        size_t smallest = pos;

        if (l < len && a[l].val < a[smallest].val) {
            smallest = l;
        }
        if (r < len && a[r].val < a[smallest].val) {
            smallest = r;
        }
        if (smallest == pos) {
            break;
        }

        a[pos] = a[smallest];
        if (a[pos].ref) {
            *a[pos].ref = pos;
        }
        pos = smallest;
    }
    a[pos] = t;
    if (a[pos].ref) {
        *a[pos].ref = pos;
    }
}

void heap_update(HeapItem *a, size_t pos, size_t len) {
    if (pos > 0 && a[heap_parent(pos)].val > a[pos].val) {
        heap_up(a, pos);
    } else {
        heap_down(a, pos, len);
    }
}

void heap_upsert(std::vector<HeapItem> &a, size_t pos, HeapItem t) {
    if (pos < a.size()) {
        a[pos] = t; // Mise à jour d'un élément existant
    } else {
        pos = a.size();
        a.push_back(t); // Ajout d'un nouvel élément
    }
    heap_update(a.data(), pos, a.size());
}

void heap_delete(std::vector<HeapItem> &a, size_t pos) {
    if (pos >= a.size()) return;
    a[pos] = a.back();
    a.pop_back();
    if (pos < a.size()) {
        heap_update(a.data(), pos, a.size());
    }
}

