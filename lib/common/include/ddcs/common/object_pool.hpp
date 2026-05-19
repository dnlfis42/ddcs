#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <cstddef>

namespace ddcs::common {

template <typename T>
    requires std::is_default_constructible_v<T>
class ObjectPool {
private:
    struct Node {
        T data;
        Node* next;
    };

public:
    class Deleter {
    public:
        Deleter(ObjectPool& pool, Node& node) noexcept : pool_{pool}, node_{node} {}

        void operator()(T*) const noexcept { pool_.get().release(node_); }

    private:
        std::reference_wrapper<ObjectPool> pool_;
        std::reference_wrapper<Node> node_;
    };

    using Handle = std::unique_ptr<T, Deleter>;

public:
    explicit ObjectPool(std::size_t initial_capacity = 0, std::size_t chunk_size = 64)
        : head_free_{nullptr}, chunk_size_{chunk_size == 0 ? 1 : chunk_size}, capacity_{0},
          available_{0} {
        std::size_t const initial_chunks = (initial_capacity + chunk_size_ - 1) / chunk_size_;
        for (std::size_t i = 0; i < initial_chunks; ++i) {
            grow();
        }
    }

    ObjectPool(ObjectPool const&) = delete;
    ObjectPool& operator=(ObjectPool const&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t available() const noexcept { return available_; }
    std::size_t in_use() const noexcept { return capacity_ - available_; }
    std::size_t chunk_size() const noexcept { return chunk_size_; }

public:
    [[nodiscard]] Handle acquire() {
        if (head_free_ == nullptr) {
            grow();
        }
        Node& node = *head_free_;
        head_free_ = node.next;
        --available_;
        return Handle{&node.data, Deleter{*this, node}};
    }

private:
    void release(Node& node) noexcept {
        node.next = head_free_;
        head_free_ = &node;
        ++available_;
    }

    void grow() {
        auto chunk = std::make_unique<Node[]>(chunk_size_);

        for (std::size_t i = 0; i + 1 < chunk_size_; ++i) {
            chunk[i].next = &chunk[i + 1];
        }
        chunk[chunk_size_ - 1].next = head_free_;

        head_free_ = &chunk[0];
        capacity_ += chunk_size_;
        available_ += chunk_size_;

        chunks_.push_back(std::move(chunk));
    }

private:
    std::vector<std::unique_ptr<Node[]>> chunks_;
    Node* head_free_;
    std::size_t chunk_size_;
    std::size_t capacity_;
    std::size_t available_;
};

template <typename T>
using PoolHandle = typename ObjectPool<T>::Handle;

} // namespace ddcs::common
