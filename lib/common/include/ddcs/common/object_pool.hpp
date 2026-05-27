#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include <cstddef>

namespace ddcs::common {

// release 시 논리적 리셋을 위해 요구하는 계약
template <typename T>
concept resettable = requires(T& t) {
    { t.reset() } noexcept -> std::same_as<void>;
};

template <resettable T>
class ObjectPool {
private:
    struct Node {
        alignas(T) std::byte storage[sizeof(T)];
        Node* next;

        T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
    };

public:
    class Deleter {
    public:
        Deleter() noexcept = default;
        Deleter(ObjectPool& pool, Node& node) noexcept : pool_{&pool}, node_{&node} {}

        void operator()(T*) const noexcept {
            if (pool_ != nullptr) {
                pool_->release(*node_);
            }
        }

    private:
        ObjectPool* pool_;
        Node* node_;
    };

    using Handle = std::unique_ptr<T, Deleter>;
    using Factory = std::function<void(void* /* placement-new 대상 주소 */)>;

public:
    explicit ObjectPool(Factory factory, std::size_t initial_capacity = 0, std::size_t chunk_size = 64)
        : factory_{std::move(factory)}, head_free_{nullptr}, chunk_size_{chunk_size == 0 ? 1 : chunk_size},
          capacity_{0}, available_{0} {
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
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t available() const noexcept { return available_; }
    std::size_t in_use() const noexcept { return capacity_ - available_; }
    std::size_t chunk_size() const noexcept { return chunk_size_; }

public:
    [[nodiscard]]
    Handle acquire() {
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
        // 소멸이 아니라 논리적 리셋. 내부 동적 할당(capacity)은 그대로 유지된다.
        node.ptr()->reset();
        node.next = head_free_;
        head_free_ = &node;
        ++available_;
    }

    void grow() {
        auto chunk = std::make_unique<Node[]>(chunk_size_);

        // 슬롯마다 단 한 번 객체 생성. 이 시점에 T 내부 동적 할당이 발생한다.
        std::size_t constructed = 0;
        try {
            for (; constructed < chunk_size_; ++constructed) {
                factory_(chunk[constructed].storage);
            }
        } catch (...) {
            // 부분 생성 정리: 이미 만들어진 0..constructed-1 을 되돌린다.
            for (std::size_t i = 0; i < constructed; ++i) {
                chunk[i].ptr()->~T();
            }
            throw;
        }

        // free-list 연결.
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
    Factory factory_;
    std::vector<std::unique_ptr<Node[]>> chunks_;
    Node* head_free_;
    std::size_t chunk_size_;
    std::size_t capacity_;
    std::size_t available_;
};

template <typename T>
using PoolHandle = typename ObjectPool<T>::Handle;

// 호출 지점을 한 줄로 만드는 헬퍼. T 의 ctor 인자들을 묶어서 받는다.
// default ctor 유무, 인자 개수에 무관하게 균일하게 흡수한다.
// auto p = make_pool<LinearBuffer>(/*initial*/256, /*chunk*/64, /*T ctor args*/4096);
template <typename T, typename... Args>
[[nodiscard]]
ObjectPool<T> make_pool(std::size_t initial_capacity, std::size_t chunk_size, Args&&... args) {
    return ObjectPool<T>([args...](void* p) { new (p) T(args...); }, initial_capacity, chunk_size);
}

} // namespace ddcs::common
