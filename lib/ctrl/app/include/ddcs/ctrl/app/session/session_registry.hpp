#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/port/active_devices.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <cassert>
#include <cstddef>
#include <functional>
#include <unordered_map>

namespace ddcs::ctrl::app::session {

namespace port = ddcs::ctrl::app::transport::port;

// Session 집합의 단일 진실.
// conn이 1차 키, device 역색인이 "device당 바인딩 연결 최대 1" 불변식을 봉인한다.
// CAUTION: outbound.disconnect는 동기로 on_disconnected와 erase를 되부른다.
// 순회 중 disconnect 금지(희생자 수집 후 순회 밖에서), erase 가능 경로 뒤에는 find 재조회.
class SessionRegistry : public device::port::ActiveDevices {
public:
    [[nodiscard]] std::size_t size() const noexcept {
        return sessions_.size();
    }

    [[nodiscard]] Session* find(port::ConnectionId conn) {
        auto it = sessions_.find(conn);
        return it == sessions_.end() ? nullptr : &it->second;
    }

    // 역색인 경유. 바인딩된(confirming/active) 연결만 닿는다. (kick-old가 옛 연결을 찾는 경로)
    [[nodiscard]] Session* find(domain::DeviceId device) {
        auto it = device_index_.find(device);
        return it == device_index_.end() ? nullptr : find(it->second);
    }

    // monitor sweep용 순회.
    // CAUTION: 순회 중 등록/제거 금지
    template <typename F>
    void for_each(F&& fn) const {
        for (auto const& entry : sessions_) {
            fn(entry.second);
        }
    }

    // 정책과 메트릭 집계용 active device 열거 (device::port::ActiveDevices 구현)
    void for_each_active(std::function<void(domain::DeviceId)> const& fn) override {
        for_each([&](Session const& session) {
            if (session.active()) {
                fn(session.device());
            }
        });
    }

    // on_connected: handshaking Session 생성:
    // - conn 중복이면 false (infra가 유일성을 보장하므로 버그 신호)
    [[nodiscard]] bool add(port::ConnectionId conn, common::Clock::time_point now) {
        return sessions_.try_emplace(conn, conn, now).second;
    }

    // 등록 확정시 handshaking에서 confirming으로 전이 + 역색인 등재
    // (active 전이는 Session::confirm 몫)
    // - 실패(상태 불변): conn 없음 / device 이미 점유 / 비handshaking / nil device
    // - 점유 device는 호출자가 먼저 kick으로 비워야 한다.
    [[nodiscard]] bool
    bind(port::ConnectionId conn, domain::DeviceId device, common::Clock::time_point now) {
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            return false;
        }
        auto const [index_it, inserted] = device_index_.emplace(device, conn);
        if (!inserted) {
            return false;
        }
        if (!it->second.bind(device, now)) {
            device_index_.erase(index_it); // 역색인 선점 롤백 (비handshaking / nil device)
            return false;
        }
        return true;
    }

    // on_disconnected: Session 제거. 바인딩됐다면(confirming/active) 역색인도 함께 지운다.
    // 다른 경로로는 역색인이 줄지 않는다.
    bool erase(port::ConnectionId conn) {
        auto it = sessions_.find(conn);
        if (it == sessions_.end()) {
            return false;
        }
        if (it->second.device().valid()) { // device 보유면 바인딩된 상태
            assert(device_index_.contains(it->second.device()));
            device_index_.erase(it->second.device());
        }
        sessions_.erase(it);
        return true;
    }

private:
    std::unordered_map<port::ConnectionId, Session> sessions_;
    std::unordered_map<domain::DeviceId, port::ConnectionId>
        device_index_; // confirming/active 바인딩
};

} // namespace ddcs::ctrl::app::session
