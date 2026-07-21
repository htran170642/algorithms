#pragma once

// An AVL tree: a self-balancing binary search tree. After every insert, if any
// node's two subtrees differ in height by more than 1, a rotation restores
// balance in O(1). Height stays ~log(n), so search/insert stay O(log n) even
// for sorted input — where a plain BST degenerates into a linked list.
//
// This is the machinery behind std::map / std::set (which use red-black trees,
// same idea, different balance rule). Thrown away after this week.

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace cr {

template <typename Key, typename Value>
class AvlTree {
    struct Node {
        Key   key;
        Value value;
        int   height = 1;                     // height of this subtree
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        Node(Key k, Value v) : key(std::move(k)), value(std::move(v)) {}
    };

public:
    void insert(const Key& k, const Value& v) { root_ = insert(std::move(root_), k, v); }

    [[nodiscard]] std::optional<Value> find(const Key& k) const {
        Node* n = root_.get();
        while (n) {
            if (k < n->key)      n = n->left.get();
            else if (n->key < k) n = n->right.get();
            else                 return n->value;
        }
        return std::nullopt;
    }

    // in-order traversal -> keys come out SORTED. This is what hash can't do.
    [[nodiscard]] std::vector<Key> sortedKeys() const {
        std::vector<Key> out;
        inorder(root_.get(), out);
        return out;
    }

    [[nodiscard]] int height() const { return height(root_.get()); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    static int height(const Node* n) { return n ? n->height : 0; }

    static int balanceFactor(const Node* n) {
        return n ? height(n->left.get()) - height(n->right.get()) : 0;
    }

    static void updateHeight(Node* n) {
        n->height = 1 + std::max(height(n->left.get()), height(n->right.get()));
    }

    // Right rotation: the left child becomes the new root of this subtree.
    //       y             x
    //      / \.          / \.
    //     x   C   -->   A   y
    //    / \.              / \.
    //   A   B             B   C
    static std::unique_ptr<Node> rotateRight(std::unique_ptr<Node> y) {
        std::unique_ptr<Node> x = std::move(y->left);
        y->left  = std::move(x->right);       // B moves under y
        updateHeight(y.get());
        x->right = std::move(y);              // y becomes x's right child
        updateHeight(x.get());
        return x;
    }

    static std::unique_ptr<Node> rotateLeft(std::unique_ptr<Node> x) {
        std::unique_ptr<Node> y = std::move(x->right);
        x->right = std::move(y->left);
        updateHeight(x.get());
        y->left  = std::move(x);
        updateHeight(y.get());
        return y;
    }

    std::unique_ptr<Node> insert(std::unique_ptr<Node> node, const Key& k, const Value& v) {
        if (!node) {
            ++size_;
            return std::make_unique<Node>(k, v);
        }
        if (k < node->key)      node->left  = insert(std::move(node->left), k, v);
        else if (node->key < k) node->right = insert(std::move(node->right), k, v);
        else                    { node->value = v; return node; }   // key exists

        updateHeight(node.get());
        return rebalance(std::move(node), k);
    }

    // The four cases, chosen by which side is heavy and where the new key went.
    std::unique_ptr<Node> rebalance(std::unique_ptr<Node> node, const Key& k) {
        int bf = balanceFactor(node.get());

        if (bf > 1 && k < node->left->key)                     // LL
            return rotateRight(std::move(node));
        if (bf < -1 && node->right->key < k)                   // RR
            return rotateLeft(std::move(node));
        if (bf > 1 && node->left->key < k) {                   // LR
            node->left = rotateLeft(std::move(node->left));
            return rotateRight(std::move(node));
        }
        if (bf < -1 && k < node->right->key) {                 // RL
            node->right = rotateRight(std::move(node->right));
            return rotateLeft(std::move(node));
        }
        return node;                                            // already balanced
    }

    static void inorder(const Node* n, std::vector<Key>& out) {
        if (!n) return;
        inorder(n->left.get(), out);          // trái trước
        out.push_back(n->key);                // rồi node
        inorder(n->right.get(), out);         // rồi phải  → thứ tự tăng dần
    }

    std::unique_ptr<Node> root_;
    std::size_t           size_ = 0;
};

}  // namespace cr
