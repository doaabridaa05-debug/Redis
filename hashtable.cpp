#include "hashtable.h"
#include <assert.h>
#include <stdlib.h>

// Allouer une table avec 2^power buckets
static void h_init(HTab *httab, size_t n) {
    assert(n > 0 && ((n & (n - 1)) == 0)); // Doit être une puissance de 2
    httab->tab = (HNode **)calloc(n, sizeof(HNode *));
    httab->mask = n - 1;
    httab->size = 0;
}

// Insérer un nœud dans une table spécifique
static void h_insert(HTab *httab, HNode *node) {
    size_t pos = node->hcode & httab->mask;
    node->next = httab->tab[pos];
    httab->tab[pos] = node;
    httab->size++;
}

// Rechercher un pointeur vers un nœud (gestion des collisions par chaînage)
static HNode **h_lookup(HTab *httab, HNode *key, bool (*eq)(HNode *, HNode *)) {
    if (!httab->tab) {
        return nullptr;
    }
    size_t pos = key->hcode & httab->mask;
    HNode **from = &httab->tab[pos];
    while (*from) {
        if ((*from)->hcode == key->hcode && eq(*from, key)) {
            return from; // <-- RETOURNER L'ADRESSE DU POINTEUR ICI
        }
        from = &(*from)->next;
    }
    return nullptr;
}

// Retirer un nœud d'une table
static HNode *h_detach(HTab *httab, HNode **from) {
    HNode *node = *from;
    *from = node->next;
    httab->size--;
    return node;
}

// Avancer le redimensionnement progressif (rehashing) de quelques buckets
static void hm_help_rehashing(HMap *hmap) {
    if (!hmap->ht2.tab) {
        return;
    }

    size_t n_work = 0;
    while (n_work < 128 && hmap->ht2.size > 0) {
        // Trouver un bucket non vide dans ht2
        HNode **from = &hmap->ht2.tab[hmap->resizing_pos];
        if (!*from) {
            hmap->resizing_pos++;
            continue;
        }

        // Déplacer le nœud vers ht1
        HNode *node = h_detach(&hmap->ht2, from);
        h_insert(&hmap->ht1, node);
        n_work++;
    }

    // Si ht2 est vide, on libère l'ancienne table
    if (hmap->ht2.size == 0) {
        free(hmap->ht2.tab);
        hmap->ht2 = {};
    }
}

// Déclencher un redimensionnement si la table est trop pleine
static void hm_trigger_rehashing(HMap *hmap) {
    assert(hmap->ht2.tab == nullptr);
    hmap->ht2 = hmap->ht1;
    h_init(&hmap->ht1, (hmap->ht1.mask + 1) * 2);
    hmap->resizing_pos = 0;
}

// Recherche globale (vérifie ht1 et ht2)
HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, eq);
    if (!from) {
        from = h_lookup(&hmap->ht2, key, eq);
    }
    return from ? *from : nullptr;
}

// Insertion globale
void hm_insert(HMap *hmap, HNode *node) {
    if (!hmap->ht1.tab) {
        h_init(&hmap->ht1, 4); // Taille initiale : 4 buckets
    }
    h_insert(&hmap->ht1, node);
    hm_help_rehashing(hmap);

    // Si le ratio de charge dépasse 3, on déclenche un rehashing
    size_t threshold = (hmap->ht1.mask + 1) * 3;
    if (!hmap->ht2.tab && hmap->ht1.size >= threshold) {
        hm_trigger_rehashing(hmap);
    }
}

// Suppression globale
HNode *hm_pop(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *)) {
    hm_help_rehashing(hmap);
    HNode **from = h_lookup(&hmap->ht1, key, eq);
    if (from) {
        return h_detach(&hmap->ht1, from);
    }
    from = h_lookup(&hmap->ht2, key, eq);
    if (from) {
        return h_detach(&hmap->ht2, from);
    }
    return nullptr;
}

size_t hm_size(HMap *hmap) {
    return hmap->ht1.size + hmap->ht2.size;
}

void hm_destroy(HMap *hmap) {
    free(hmap->ht1.tab);
    free(hmap->ht2.tab);
    *hmap = {};
}

