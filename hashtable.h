#pragma once

#include <stdint.h>
#include <stddef.h>

// Nœud de hachage intrusif
struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0;
};

// Une table de hachage (un tableau de buckets)
struct HTab {
    HNode **tab = nullptr;
    size_t mask = 0; // Toujours égal à (puissance de 2) - 1 (utilisé à la place du modulo)
    size_t size = 0; // Nombre d'éléments
};

// La table de hachage principale (gère le double hachage pour le redimensionnement progressif)
struct HMap {
    HTab ht1;
    HTab ht2;
    size_t resizing_pos = 0; // Index de progression du rehashing
};

// API publique de la table de hachage
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void hm_insert(HMap *hmap, HNode *node);
HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
size_t hm_size(HMap *hmap);
void hm_destroy(HMap *hmap);
