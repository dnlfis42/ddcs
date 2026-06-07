#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace ddcs::common {

template <typename T>
concept pool_resettable = requires(T& t) {
    { t.reset() } noexcept -> std::same_as<void>;
};

// 객체 주소를 유지한 채 슬롯을 재사용한다. Handle 반환 시 객체는 파괴하지 않고 reset()으로 초기화한다.
template <pool_resettable T>
class ObjectPool {
private:
    struct Node {
        Node* next{nullptr};
        alignas(T) std::byte storage[sizeof(T)];

        T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    };

public:
    class Deleter {
    public:
        Deleter() noexcept = default;
        Deleter(ObjectPool& pool, Node& node) noexcept : pool_{&pool}, node_{&node} {}

    public:
        void operator()(T*) const noexcept {
            if (pool_ != nullptr) {
                pool_->release(*node_);
            }
        }

    private:
        ObjectPool* pool_{nullptr};
        Node* node_{nullptr};
    };

    using Constructor = std::function<void(void*)>;
    using Handle = std::unique_ptr<T, Deleter>;

public:
    explicit ObjectPool(Constructor constructor, std::size_t initial_capacity = 0, std::size_t chunk_size = 64)
        : constructor_{std::move(constructor)}, chunk_size_{chunk_size == 0 ? 1 : chunk_size} {
        std::size_t const initial_chunks = (initial_capacity + chunk_size_ - 1) / chunk_size_;
        for (std::size_t i = 0; i < initial_chunks; ++i) {
            grow();
        }
    }
    ~ObjectPool() {
        for (auto& chunk : chunks_) {
            for (std::size_t i = 0; i < chunk_size_; ++i) {
                chunk[i].ptr()->~T();
            }
        }
    }

    ObjectPool(ObjectPool const&) = delete;
    ObjectPool& operator=(ObjectPool const&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

public:
    std::size_t chunk_size() const noexcept { return chunk_size_; }
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t available() const noexcept { return available_; }
    std::size_t in_use() const noexcept { return capacity_ - available_; }

public:
    [[nodiscard]] Handle acquire() {
        if (head_free_ == nullptr) [[unlikely]] {
            grow();
        }
        Node& node = *head_free_;
        head_free_ = node.next;
        --available_;
        return Handle{node.ptr(), Deleter{*this, node}};
    }

private:
    void release(Node& node) noexcept {
        node.ptr()->reset();
        node.next = head_free_;
        head_free_ = &node;
        ++available_;
    }

    void grow() {
        auto chunk = std::make_unique<Node[]>(chunk_size_);

        // PERF: 슬롯 객체는 chunk 생성 시 한 번만 만들고, 반환 시에는 reset()으로 재사용한다.
        std::size_t constructed = 0;
        try {
            for (; constructed < chunk_size_; ++constructed) {
                constructor_(chunk[constructed].storage);
            }
        } catch (...) {
            // 생성 중 예외가 나면 이미 생성된 슬롯만 직접 되돌린다.
            for (std::size_t i = 0; i < constructed; ++i) {
                chunk[i].ptr()->~T();
            }
            throw;
        }

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
    Constructor constructor_;
    std::vector<std::unique_ptr<Node[]>> chunks_;
    Node* head_free_{nullptr};
    std::size_t chunk_size_;
    std::size_t capacity_{0};
    std::size_t available_{0};
};

template <typename T>
using PoolHandle = typename ObjectPool<T>::Handle;

template <typename T, typename... Args>
[[nodiscard]] ObjectPool<T> make_pool(std::size_t initial_capacity, std::size_t chunk_size, Args&&... args) {
    return ObjectPool<T>([args...](void* p) { new (p) T(args...); }, initial_capacity, chunk_size);
}

} // namespace ddcs::common
