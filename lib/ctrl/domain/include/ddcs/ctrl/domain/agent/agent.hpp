#pragma once

#include "ddcs/ctrl/domain/agent/agent_id.hpp"
#include "ddcs/ctrl/domain/agent/agent_uuid.hpp"
#include "ddcs/device/mode.hpp"

#include <string>

namespace ddcs::ctrl::domain::agent {

// Agent 식별 레코드. AgentRegistry 가 소유.
// binding(현재 어느 connection 에 연결돼 있는지)은 app::session 책임 - 여기엔 없음.
// id/uuid = 영속 identity. group/version = 등록 시 선언. mode/load/temp = 최근 Status 텔레메트리.
struct Agent {
    AgentId id;
    AgentUuid uuid;
    std::string group{};   // 논리 그룹(정책 타깃팅). 빈 문자열 = 미지정
    std::string version{}; // agent 앱/빌드 버전
    device::Mode mode{};   // 현재 모드(기본 safe). 정책 출력+피드백
    double load{};         // 부하 가상지표 0~100. 정책 입력 신호
    double temp{};         // 온도 C
    // 가변 필드엔 기본 멤버 초기자({}) - designated init 누락 시 -Wmissing-field-initializers 회피.
};

} // namespace ddcs::ctrl::domain::agent
