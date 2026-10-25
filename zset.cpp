#include "zset.h"
#include <cstring>
#include <cstdlib>
#include <cassert>

// Macro container_of déjà utilisée dans server.cpp
#define container_of(ptr, type, member) ({ \
    const typeof( ((type *)0)->member ) *__mptr = (ptr); \
    (type *)( (char *)__mptr - offsetof(type, member) ); \
})

// Fonction de hachage interne
static uint64_t str_hash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ data[i]) * 0x01000193;
    }
    return h;
}

struct HKey {
    HNode node;
    const char *name = nullptr;
    size_t len = 0;
};

static bool hcmp(HNode *node, HNode *key) {
    ZNode *znode = container_of(node, ZNode, hmap);
    HKey *hkey = container_of(key, HKey, node);
    if (znode->len != hkey->len) return false;
    return 0 == memcmp(znode->name, hkey->name, znode->len);
}

ZNode *zset_lookup(ZSet *zset, const char *name, size_t len) {
    if (!zset->root) return nullptr;
    HKey key;
    key.node.hcode = str_hash((const uint8_t *)name, len);
    key.name = name;
    key.len = len;
    HNode *found = hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? container_of(found, ZNode, hmap) : nullptr;
}

// Comparaison lexicographique et par score (score en premier, puis nom)
static bool zless(AVLNode *lhs, double score, const char *name, size_t len) {
    ZNode *zl = container_of(lhs, ZNode, tree);
    if (zl->score != score) {
        return zl->score < score;
    }
    int rv = memcmp(zl->name, name, std::min(zl->len, len));
    return (rv != 0) ? (rv < 0) : (zl->len < len);
}

static bool zless_node(AVLNode *lhs, AVLNode *rhs) {
    ZNode *zr = container_of(rhs, ZNode, tree);
    return zless(lhs, zr->score, zr->name, zr->len);
}

static ZNode *znode_new(const char *name, size_t len, double score) {
    ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
    avl_init(&node->tree);
    node->hmap.next = nullptr;
    node->hmap.hcode = str_hash((const uint8_t *)name, len);
    node->score = score;
    node->len = len;
    memcpy(node->name, name, len);
    return node;
}

static void tree_insert(ZSet *zset, ZNode *node) {
    AVLNode *parent = nullptr;
    AVLNode **from = &zset->root;
    while (*from) {
        parent = *from;
        from = zless_node(&node->tree, parent) ? &parent->left : &parent->right;
    }
    *from = &node->tree;
    node->tree.parent = parent;
    zset->root = avl_fix(&node->tree);
}

bool zset_insert(ZSet *zset, const char *name, size_t len, double score) {
    ZNode *node = zset_lookup(zset, name, len);
    if (node) {
        // Mise à jour du score si la clé existe déjà
        node->score = score;
        zset->root = avl_del(&node->tree);
        avl_init(&node->tree);
        tree_insert(zset, node);
        return false;
    }
    node = znode_new(name, len, score);
    hm_insert(&zset->hmap, &node->hmap);
    tree_insert(zset, node);
    return true;
}

bool zset_delete(ZSet *zset, const char *name, size_t len, ZNode **out) {
    HKey key;
    key.node.hcode = str_hash((const uint8_t *)name, len);
    key.name = name;
    key.len = len;
    
    // Correction : Utiliser hm_pop au lieu de hm_delete
    HNode *found = hm_pop(&zset->hmap, &key.node, &hcmp);
    if (!found) {
        return false;
    }

    ZNode *node = container_of(found, ZNode, hmap);
    zset->root = avl_del(&node->tree);
    *out = node;
    return true;
}

void zset_clear(ZSet *zset) {
    // Nettoyage simplifié pour la structure
    hm_destroy(&zset->hmap);
    zset->root = nullptr;
}
