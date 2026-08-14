#pragma once

#include <cstdint>

namespace ddcs::ctrl::app::transport::port {

// 전송 계층 자원 계기의 scrape 시점 스냅샷 (메트릭 노출용)
struct TransportStats {
    std::uint64_t tx_queued_messages{};       // 전 연결 송신 큐 대기 메시지 합
    std::uint64_t connection_pool_capacity{}; // Connection 풀 슬롯 수
    std::uint64_t connection_pool_acquired{};
    std::uint64_t message_pool_capacity{}; // 수신 메시지 버퍼 풀 슬롯 수
    std::uint64_t message_pool_acquired{};
};

// 전송 계층 계기 조회 포트. 상한 없는 송신 큐(느린 소비자)와 풀 성장(누수/폭주)을 감시한다.
class TransportStatsSource {
public:
    virtual ~TransportStatsSource() = default;

    [[nodiscard]] virtual TransportStats transport_stats() const = 0;
};

} // namespace ddcs::ctrl::app::transport::port
