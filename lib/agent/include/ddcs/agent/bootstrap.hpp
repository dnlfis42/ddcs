#pragma once

#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/domain/device.hpp"
#include "ddcs/common/uuid.hpp"

#include <memory>
#include <random>

namespace ddcs::agent::bootstrap {

// 설정 우선순위: 지원하는 환경변수 > 파일 > 기본값.
// 로그 sink는 호출자가 먼저 지정한다. 전역 로그 수준을 변경하며 파싱 오류는 예외로 전달한다.
Agent::Config load_agent_config();

// 보고 주기와 DDCS_SIM_* 설정으로 장치를 구성한다.
// 전달받은 rng를 소비해 장치별 초기 상태·변화율·난수 시드를 정한다.
std::unique_ptr<domain::Device>
make_simulated_device(common::Uuid uuid, Agent::Config const& cfg, std::mt19937_64& rng);

} // namespace ddcs::agent::bootstrap
