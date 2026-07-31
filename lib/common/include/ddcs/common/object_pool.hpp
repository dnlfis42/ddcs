#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace ddcs::common {

namespace detail::object_pool {

template <typename T>
concept resettable = requires(T& t) {
    { t.reset() } noexcept -> std::same_as<void>;
};

template <typename T, typename... Args>
concept pool_constructible_from = std::constructible_from<T, std::decay_t<Args> const&...> &&
                                  (std::copy_constructible<std::decay_t<Args>> && ...);

} // namespace detail::object_pool

// T 객체를 미리 만들어두고 재사용하는 오브젝트 풀.
// acquire()가 돌려주는 Handle이 소멸하면 객체는 소멸 대신 reset() 후 풀로 반환된다.
template <detail::object_pool::resettable T>
class ObjectPool {
private:
    struct Slot {
        Slot* next_available = nullptr;
        alignas(T) std::byte storage[sizeof(T)];

        T* object_ptr() noexcept {
            return std::launder(reinterpret_cast<T*>(storage));
        }
    };

    using Chunk = std::unique_ptr<Slot[]>;

public:
    class Deleter {
    public:
        Deleter() noexcept = default;

        void operator()(T* object) const noexcept {
            if (pool_ != nullptr) {
                assert(slot_ != nullptr);
                assert(object == slot_->object_ptr());
                (void)object;

                pool_->release(*slot_);
            }
        }

    private:
        friend class ObjectPool;

        Deleter(ObjectPool& pool, Slot& slot) noexcept
            : pool_(&pool),
              slot_(&slot) {}

        ObjectPool* pool_ = nullptr;
        Slot* slot_ = nullptr;
    };

    using Handle = std::unique_ptr<T, Deleter>;

    // args를 복사해 보관하고 이후 모든 슬롯의 T를 그 인자로 생성한다.
    // 첫 chunk는 지연 생성되며, 첫 acquire()/reserve() 시점에 채워진다.
    template <std::size_t ChunkSize = 64, typename... Args>
        requires(ChunkSize > 0) && detail::object_pool::pool_constructible_from<T, Args...>
    [[nodiscard]] static ObjectPool create(Args&&... args) {
        return ObjectPool{ChunkSize, [... captured_args = std::forward<Args>(args)](void* storage) {
                              std::construct_at(static_cast<T*>(storage), captured_args...);
                          }};
    }

    ~ObjectPool() {
        ensure_all_objects_released();
        destroy_all_objects();
    }

    ObjectPool(ObjectPool const&) = delete;
    ObjectPool& operator=(ObjectPool const&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    [[nodiscard]] std::size_t chunk_size() const noexcept {
        return chunk_size_;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] std::size_t available_count() const noexcept {
        return available_count_;
    }

    [[nodiscard]] std::size_t acquired_count() const noexcept {
        return capacity_ - available_count_;
    }

    void reserve(std::size_t min_capacity) {
        std::size_t chunk_count =
            min_capacity / chunk_size_ + (min_capacity % chunk_size_ == 0 ? 0 : 1);

        while (chunks_.size() < chunk_count) {
            add_chunk();
        }
    }

    // 객체 하나를 빌려 Handle로 돌려준다. 가용 슬롯이 없으면 chunk를 하나 더 늘려서라도 성공한다.
    // CAUTION: 발급한 Handle보다 풀이 먼저 소멸하면 std::terminate된다.
    [[nodiscard]] Handle acquire() {
        if (available_head_ == nullptr) [[unlikely]] {
            add_chunk();
        }

        Slot& slot = *available_head_;
        available_head_ = slot.next_available;
        slot.next_available = nullptr;

        --available_count_;

        return Handle{slot.object_ptr(), Deleter{*this, slot}};
    }

private:
    using ObjectConstructor = std::function<void(void*)>;

    explicit ObjectPool(std::size_t chunk_size, ObjectConstructor construct_object)
        : chunk_size_(chunk_size),
          construct_object_(std::move(construct_object)) {}

    void add_chunk() {
        Chunk chunk = std::make_unique<Slot[]>(chunk_size_);

        // PERF: 슬롯 객체는 chunk 생성 시 한 번만 만들고, 반환 시에는 reset()으로 재사용한다.
        std::size_t constructed_count = 0;

        try {
            for (; constructed_count < chunk_size_; ++constructed_count) {
                construct_object_(chunk[constructed_count].storage);
            }

            chunks_.push_back(std::move(chunk));
        } catch (...) {
            // 생성 중 예외가 나면 이미 생성된 슬롯만 직접 되돌린다.
            destroy_objects(chunk.get(), constructed_count);
            throw;
        }

        Slot* const slots = chunks_.back().get();

        for (std::size_t i = 0; i + 1 < chunk_size_; ++i) {
            slots[i].next_available = &slots[i + 1];
        }

        slots[chunk_size_ - 1].next_available = available_head_;
        available_head_ = &slots[0];

        capacity_ += chunk_size_;
        available_count_ += chunk_size_;
    }

    void release(Slot& slot) noexcept {
        slot.object_ptr()->reset();

        slot.next_available = available_head_;
        available_head_ = &slot;

        ++available_count_;
    }

    static void destroy_objects(Slot* slots, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            slots[i].object_ptr()->~T();
        }
    }

    void destroy_all_objects() noexcept {
        for (auto& chunk : chunks_) {
            destroy_objects(chunk.get(), chunk_size_);
        }
    }

    void ensure_all_objects_released() const noexcept {
        if (available_count_ != capacity_) {
            assert(false && "ObjectPool destroyed while handles are still alive");
            std::terminate();
        }
    }

    const std::size_t chunk_size_;
    ObjectConstructor construct_object_;
    std::vector<Chunk> chunks_;
    Slot* available_head_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t available_count_ = 0;
};

template <detail::object_pool::resettable T>
using PoolHandle = typename ObjectPool<T>::Handle;

} // namespace ddcs::common
