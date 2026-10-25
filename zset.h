#pragma once

#include "avl.h"
#include "hashtable.h"
#include <string>

struct ZSet {
    AVLNode *root = nullptr;
    HMap hmap;
};

struct ZNode {
    AVLNode tree;
    HNode hmap;
    double score = 0;
    size_t len = 0;
    char name[0]; // Tableau flexible pour stocker la chaîne directement
};

ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);
bool zset_insert(ZSet *zset, const char *name, size_t len, double score);
bool zset_delete(ZSet *zset, const char *name, size_t len, ZNode **out);
void zset_clear(ZSet *zset);
