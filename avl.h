#pragma once

#include <stdint.h>
#include <stddef.h>

struct AVLNode {
    AVLNode *parent = nullptr;
    AVLNode *left = nullptr;
    AVLNode *right = nullptr;
    uint32_t height = 0;
};

inline void avl_init(AVLNode *node) {
    node->parent = node->left = node->right = nullptr;
    node->height = 1;
}

AVLNode *avl_fix(AVLNode *node);
AVLNode *avl_del(AVLNode *node);
AVLNode *avl_offset(AVLNode *node, int64_t offset);
