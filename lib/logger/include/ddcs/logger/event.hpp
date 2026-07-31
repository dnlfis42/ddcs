#pragma once

#include "ddcs/logger/log.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

// event 이름과 레벨, 필드 이름과 순서를 한 곳에서 정한다. 호출부는 값만 넘기므로 셋 중
// 어느 것도 자리마다 어긋날 수 없고, 이 파일이 곧 event 목록이 된다.
//
// 함수가 아니라 매크로인 이유는 LOG_*와 같은 두 성질을 지키기 위해서다. disabled 레벨에서
// 인자를 평가하지 않고, file/line이 호출 지점을 가리킨다.
//
// 절은 event 이름의 첫 토큰으로 나누고, 절 안에서는 event 이름 순으로 적는다. 누가 출력하는지는
// 주석이 말한다.

namespace ddcs::logger::detail {

// 운영자가 쓴 값은 길이에 상한이 없으므로 로그 한 줄이 그만큼 커지지 않게 자른다.
// 파일 경로는 PATH_MAX로 묶여 있고 잘리면 고칠 자리를 못 찾으므로 자르지 않는다.
constexpr std::size_t event_value_clip = 120;

[[nodiscard]] constexpr std::string_view clip(std::string_view value) noexcept {
    return value.substr(0, std::min(value.size(), event_value_clip));
}

} // namespace ddcs::logger::detail

// --- command (Controller) ---
//
// 한 command_id의 생애는 dispatch 또는 supersede로 시작해 complete 또는 fail로 끝난다.
// 그 사이의 ack/reject/timeout/retry는 시도 단위라 한 id에 여러 번 나올 수 있다.
// 상관 키는 wire 맥락 그대로 (device, command_id) 순서로 싣는다.

// device가 명령을 받았다고 알렸다. apply 전에 오므로 아직 수행 결과는 아니다.
#define LOG_COMMAND_ACK(_device, _command_id, _attempts)                                           \
    LOG_INFO(                                                                                      \
        "command.ack", ::ddcs::logger::kv("device", _device),                                      \
        ::ddcs::logger::kv("command_id", _command_id), ::ddcs::logger::kv("attempts", _attempts)   \
    )

// 명령이 성공으로 종결됐다. rtt_ms는 최초 dispatch부터의 왕복 시간이다.
#define LOG_COMMAND_COMPLETE(_device, _command_id, _rtt_ms)                                        \
    LOG_INFO(                                                                                      \
        "command.complete", ::ddcs::logger::kv("device", _device),                                 \
        ::ddcs::logger::kv("command_id", _command_id), ::ddcs::logger::kv("rtt_ms", _rtt_ms)       \
    )

// 새 명령을 보내고 미결로 등록했다.
#define LOG_COMMAND_DISPATCH(_device, _command_id)                                                 \
    LOG_INFO(                                                                                      \
        "command.dispatch", ::ddcs::logger::kv("device", _device),                                 \
        ::ddcs::logger::kv("command_id", _command_id)                                              \
    )

// 첫 송신부터 실패해 미결로 등록조차 못 했다. 재시도 없이 버려진 명령이다.
#define LOG_COMMAND_DISPATCH_FAIL(_device, _command_id, _reason)                                   \
    LOG_WARN(                                                                                      \
        "command.dispatch.fail", ::ddcs::logger::kv("device", _device),                            \
        ::ddcs::logger::kv("command_id", _command_id), ::ddcs::logger::kv("reason", _reason)       \
    )

// 명령을 포기했다. 이 계열의 알람 줄이다. reason이 왜 접었는지의 유일한 출처다.
#define LOG_COMMAND_FAIL(_device, _command_id, _attempts, _reason)                                 \
    LOG_WARN(                                                                                      \
        "command.fail", ::ddcs::logger::kv("device", _device),                                     \
        ::ddcs::logger::kv("command_id", _command_id), ::ddcs::logger::kv("attempts", _attempts),  \
        ::ddcs::logger::kv("reason", _reason)                                                      \
    )

// device가 이번 시도를 거부했다. code는 검증 전 wire byte라 정수로 적는다.
// codec이 어휘 밖 값을 통과시키므로 이름으로 옮기면 정작 알아야 할 값이 빈 문자열이 된다.
#define LOG_COMMAND_REJECT(_device, _command_id, _code)                                            \
    LOG_WARN(                                                                                      \
        "command.reject", ::ddcs::logger::kv("device", _device),                                   \
        ::ddcs::logger::kv("command_id", _command_id), ::ddcs::logger::kv("code", _code)           \
    )

// backoff가 지나 같은 command_id를 다시 보냈다.
#define LOG_COMMAND_RETRY(_device, _command_id, _attempts)                                         \
    LOG_INFO(                                                                                      \
        "command.retry", ::ddcs::logger::kv("device", _device),                                    \
        ::ddcs::logger::kv("command_id", _command_id), ::ddcs::logger::kv("attempts", _attempts)   \
    )

// 이미 닫혔거나 대체된 명령에 device가 뒤늦게 답했다. 정상 부산물이라 DEBUG다.
#define LOG_COMMAND_STALE_RESPONSE(_device, _command_id)                                           \
    LOG_DEBUG(                                                                                     \
        "command.stale_response", ::ddcs::logger::kv("device", _device),                           \
        ::ddcs::logger::kv("command_id", _command_id)                                              \
    )

// 같은 계열의 새 명령이 들어와 이 미결을 밀어냈다. command_id는 밀려난 쪽이다.
#define LOG_COMMAND_SUPERSEDE(_device, _command_id)                                                \
    LOG_INFO(                                                                                      \
        "command.supersede", ::ddcs::logger::kv("device", _device),                                \
        ::ddcs::logger::kv("command_id", _command_id)                                              \
    )

// 이번 시도의 응답 시한이 지났다. 시도 단위라 재시도가 남았으면 뒤에 retry가 따른다.
#define LOG_COMMAND_TIMEOUT(_device, _command_id, _attempts)                                       \
    LOG_WARN(                                                                                      \
        "command.timeout", ::ddcs::logger::kv("device", _device),                                  \
        ::ddcs::logger::kv("command_id", _command_id), ::ddcs::logger::kv("attempts", _attempts)   \
    )

// --- config ---

// 어느 파일을 읽기로 했는지. key는 그 경로를 정하는 env 변수 이름이다.
#define LOG_CONFIG_PATH(_key, _path)                                                               \
    LOG_INFO("config.path", ::ddcs::logger::kv("key", _key), ::ddcs::logger::kv("path", _path))

// 설정 파일이 없어 전부 기본값으로 동작한다.
#define LOG_CONFIG_PATH_ABSENT(_path)                                                              \
    LOG_WARN("config.path.absent", ::ddcs::logger::kv("path", _path))

// 설정 값이 규격을 벗어나 기본값을 쓴다.
// source는 "file" 또는 "env"이고, key는 운영자가 쓴 자리다. 설정 파일이면 점 표기 키, env면 변수
// 이름, 파일 하나가 통째로 값인 신원 파일이면 그 경로다.
#define LOG_CONFIG_VALUE_INVALID(_source, _key, _expected, _actual)                                \
    LOG_WARN(                                                                                      \
        "config.value.invalid", ::ddcs::logger::kv("source", _source),                             \
        ::ddcs::logger::kv("key", _key), ::ddcs::logger::kv("expected", _expected),                \
        ::ddcs::logger::kv("actual", ::ddcs::logger::detail::clip(_actual))                        \
    )

// --- device ---

// (Controller) 정책에 없는 group으로 등록했다. 등록은 허용되고 정책 지배만 받지 않는다.
#define LOG_DEVICE_GROUP_UNKNOWN(_device, _group)                                                  \
    LOG_WARN(                                                                                      \
        "device.group.unknown", ::ddcs::logger::kv("device", _device),                             \
        ::ddcs::logger::kv("group", _group)                                                        \
    )

// (Agent) 이 프로세스의 신원. source는 어디서 왔는지다("env", "file", "generated").
#define LOG_DEVICE_ID(_device, _source)                                                            \
    LOG_INFO(                                                                                      \
        "device.id", ::ddcs::logger::kv("device", _device), ::ddcs::logger::kv("source", _source)  \
    )

// (Agent) 신원을 파일에 기록하지 못해 재시작마다 DeviceId가 바뀐다.
// 어느 파일인지는 바로 위 config.path가 말한다.
#define LOG_DEVICE_ID_NOT_PERSISTED(_device)                                                       \
    LOG_WARN("device.id.not_persisted", ::ddcs::logger::kv("device", _device))

// (Controller) 등록되지 않은 DeviceId로 보고가 왔다.
#define LOG_DEVICE_ID_UNKNOWN(_device)                                                             \
    LOG_WARN("device.id.unknown", ::ddcs::logger::kv("device", _device))

// (Agent) 주기적으로 보고한 자기 상태. mode는 Agent가 들고 있는 어휘라 이름으로 적는다.
#define LOG_DEVICE_STATUS(_mode, _load, _temp)                                                     \
    LOG_DEBUG(                                                                                     \
        "device.status", ::ddcs::logger::kv("mode", _mode), ::ddcs::logger::kv("load", _load),     \
        ::ddcs::logger::kv("temp", _temp)                                                          \
    )

// (Controller) 보고된 load/temp가 유한하지 않아 버리고 직전 Shadow를 보존한다.
#define LOG_DEVICE_STATUS_NON_FINITE(_device, _load, _temp)                                        \
    LOG_WARN(                                                                                      \
        "device.status.non_finite", ::ddcs::logger::kv("device", _device),                         \
        ::ddcs::logger::kv("load", _load), ::ddcs::logger::kv("temp", _temp)                       \
    )

// (Controller) Shadow를 갱신했다. mode는 검증 전 wire byte다(Shadow에는 어휘로 해석해 넣는다).
#define LOG_DEVICE_STATUS_UPDATE(_device, _mode, _load, _temp)                                     \
    LOG_DEBUG(                                                                                     \
        "device.status.update", ::ddcs::logger::kv("device", _device),                             \
        ::ddcs::logger::kv("mode", _mode), ::ddcs::logger::kv("load", _load),                      \
        ::ddcs::logger::kv("temp", _temp)                                                          \
    )

// --- message ---

// 수신 바이트를 message로 읽지 못했다. type은 검증 전 wire byte라 정수로 적는다.
#define LOG_MESSAGE_DECODE_FAIL(_type)                                                             \
    LOG_WARN("message.decode.fail", ::ddcs::logger::kv("type", _type))

// 송신 메시지를 버퍼에 담지 못했다. 버퍼는 최대 메시지보다 크게 잡으므로 설계상 불가능하다.
// type은 우리가 만들려던 message라 어휘로 적는다.
#define LOG_MESSAGE_ENCODE_FAIL(_type)                                                             \
    LOG_ERROR("message.encode.fail", ::ddcs::logger::kv("type", _type))

// (Agent) 그 state에서 오지 않아야 하는 message가 왔다. decode는 성공했으므로 type은 어휘다.
// Controller는 같은 상황을 DisconnectReason::unexpected_message로 끊어 disconnect가 알린다.
#define LOG_MESSAGE_UNEXPECTED(_type, _state)                                                      \
    LOG_WARN(                                                                                      \
        "message.unexpected", ::ddcs::logger::kv("type", _type),                                   \
        ::ddcs::logger::kv("state", _state)                                                        \
    )

// --- policy (Controller) ---

// 정책을 읽어 적용했다. trigger는 "boot" 또는 "reload"다.
#define LOG_POLICY_LOAD(_path, _groups, _trigger)                                                  \
    LOG_INFO(                                                                                      \
        "policy.load", ::ddcs::logger::kv("path", _path), ::ddcs::logger::kv("groups", _groups),   \
        ::ddcs::logger::kv("trigger", _trigger)                                                    \
    )

// 파일에 policy 절이 없다. 부팅이면 빈 정책으로 가고, 리로드면 옛 정책이 그대로 남는다.
#define LOG_POLICY_LOAD_ABSENT(_path, _trigger)                                                    \
    LOG_WARN(                                                                                      \
        "policy.load.absent", ::ddcs::logger::kv("path", _path),                                   \
        ::ddcs::logger::kv("trigger", _trigger)                                                    \
    )

// 정책을 읽지 못했다. reason은 "open", "parse", "invalid" 중 하나다.
#define LOG_POLICY_LOAD_FAIL(_path, _reason, _trigger)                                             \
    LOG_WARN(                                                                                      \
        "policy.load.fail", ::ddcs::logger::kv("path", _path),                                     \
        ::ddcs::logger::kv("reason", _reason), ::ddcs::logger::kv("trigger", _trigger)             \
    )

// group의 부하 국면이 바뀌었다. 전이할 때만 나오므로 load는 전이 시점의 평균이다.
#define LOG_POLICY_REGIME_UPDATE(_group, _regime, _load)                                           \
    LOG_INFO(                                                                                      \
        "policy.regime.update", ::ddcs::logger::kv("group", _group),                               \
        ::ddcs::logger::kv("regime", _regime), ::ddcs::logger::kv("load", _load)                   \
    )

// device의 과열 latch가 바뀌었다. hot으로 들어갈 때와 cool로 풀릴 때 한 번씩 나온다.
#define LOG_POLICY_THERMAL_UPDATE(_device, _thermal, _temp)                                        \
    LOG_INFO(                                                                                      \
        "policy.thermal.update", ::ddcs::logger::kv("device", _device),                            \
        ::ddcs::logger::kv("thermal", _thermal), ::ddcs::logger::kv("temp", _temp)                 \
    )

// --- prometheus (Controller) ---
//
// 스크레이프 연결의 소켓 실패는 transport.* 이름을 그대로 쓴다. 같은 사실이고 어느 리스너인지는
// file/line이 말한다. init 실패도 이벤트가 없다. Acceptor와 같이 io::SysResult로 올려보내고
// 부팅을 세우는 쪽이 한 번만 알린다.

// 메트릭 엔드포인트를 열었다.
#define LOG_PROMETHEUS_LISTEN(_port)                                                               \
    LOG_INFO("prometheus.listen", ::ddcs::logger::kv("port", _port))

// 메트릭 리스닝 소켓이 죽었다. 이후 스크레이프는 전부 실패한다.
#define LOG_PROMETHEUS_LISTEN_FAIL(_events)                                                        \
    LOG_ERROR("prometheus.listen.fail", ::ddcs::logger::kv("events", _events))

// --- session ---

// (Agent) 명령을 수행하고 결과를 보냈다.
#define LOG_SESSION_COMMAND_APPLY(_command_id, _ok, _reason)                                       \
    LOG_INFO(                                                                                      \
        "session.command.apply", ::ddcs::logger::kv("command_id", _command_id),                    \
        ::ddcs::logger::kv("ok", _ok), ::ddcs::logger::kv("reason", _reason)                       \
    )

// (Agent) 직전과 같은 command_id라 apply 없이 이전 응답을 재송신한다.
#define LOG_SESSION_COMMAND_DEDUP(_command_id)                                                     \
    LOG_DEBUG("session.command.dedup", ::ddcs::logger::kv("command_id", _command_id))

// (Controller) ack를 받아 세션이 active가 되었다.
#define LOG_SESSION_CONNECTION_ACTIVE(_conn, _device)                                              \
    LOG_INFO(                                                                                      \
        "session.connection.active", ::ddcs::logger::kv("conn", _conn),                            \
        ::ddcs::logger::kv("device", _device)                                                      \
    )

// (Controller) 연결이 올라와 세션을 열었다.
#define LOG_SESSION_CONNECTION_CONNECT(_conn)                                                      \
    LOG_INFO("session.connection.connect", ::ddcs::logger::kv("conn", _conn))

// (Controller) 세션이 끝났다. reason이 왜 끝났는지의 유일한 출처다(시한 초과, kick, 위반 포함).
#define LOG_SESSION_CONNECTION_DISCONNECT(_conn, _reason)                                          \
    LOG_INFO(                                                                                      \
        "session.connection.disconnect", ::ddcs::logger::kv("conn", _conn),                        \
        ::ddcs::logger::kv("reason", _reason)                                                      \
    )

// (Controller) 이미 세션이 있는 conn을 또 올렸다.
// 아래 unknown과 함께, Server의 연결 맵과 SessionService의 세션 맵이 어긋났을 때만 나온다.
// 정상 배선에서는 닿지 않는 가드다.
#define LOG_SESSION_CONNECTION_DUPLICATE(_conn)                                                    \
    LOG_WARN("session.connection.duplicate", ::ddcs::logger::kv("conn", _conn))

// (Agent) keepalive를 보냈다.
#define LOG_SESSION_CONNECTION_HEARTBEAT() LOG_DEBUG("session.connection.heartbeat")

// 아래 넷은 등록 3-way다. Agent가 request를 보내고 Controller가 accept 또는 reject로 판정한다.
// Agent가 판정을 받아 세션을 여는 것이 success다. 거절과 시한 초과는 이벤트를 따로 내지 않는다.
// 뒤따르는 transport.disconnect의 reason이 말한다.

// (Controller) device를 바인딩하고 판정을 보냈다(ack 대기).
#define LOG_SESSION_CONNECTION_REGISTER_ACCEPT(_conn, _device)                                     \
    LOG_INFO(                                                                                      \
        "session.connection.register.accept", ::ddcs::logger::kv("conn", _conn),                   \
        ::ddcs::logger::kv("device", _device)                                                      \
    )

// (Controller) 등록을 거부했다.
#define LOG_SESSION_CONNECTION_REGISTER_REJECT(_conn, _reason)                                     \
    LOG_WARN(                                                                                      \
        "session.connection.register.reject", ::ddcs::logger::kv("conn", _conn),                   \
        ::ddcs::logger::kv("reason", _reason)                                                      \
    )

// (Agent) 연결이 올라와 등록을 요청했다.
#define LOG_SESSION_CONNECTION_REGISTER_REQUEST() LOG_DEBUG("session.connection.register.request")

// (Agent) 수락을 받았다.
#define LOG_SESSION_CONNECTION_REGISTER_SUCCESS(_device)                                           \
    LOG_INFO("session.connection.register.success", ::ddcs::logger::kv("device", _device))

// (Controller) 세션이 없는 conn으로 메시지가 왔다.
#define LOG_SESSION_CONNECTION_UNKNOWN(_conn)                                                      \
    LOG_WARN("session.connection.unknown", ::ddcs::logger::kv("conn", _conn))

// --- transport (Controller, Agent) ---
//
// 두 프로세스가 같은 이름을 쓴다. Controller는 연결이 여럿이라 conn을 달고 Agent는 달지 않는다.
// 어느 쪽 로그인지는 file/line이 말한다. 스크레이프 소켓의 실패도 여기를 쓴다.
//
// 그래서 몇몇 event는 매크로가 둘이다. _CONN이 붙은 쪽이 conn을 싣는 Controller용이다.
// event 이름은 하나이고, 한 프로세스의 로그 안에서는 필드 집합이 언제나 같다.

// (Controller) accept 실패. fd 고갈은 아래 fd_exhausted가 따로 말한다.
#define LOG_TRANSPORT_ACCEPT_FAIL(_errno)                                                          \
    LOG_WARN("transport.accept.fail", ::ddcs::logger::kv("errno", _errno))

// (Controller) fd가 말라 새 연결을 거절하기 시작했다. 회복할 때까지 한 번만 나온다.
#define LOG_TRANSPORT_ACCEPT_FD_EXHAUSTED(_errno)                                                  \
    LOG_WARN("transport.accept.fd_exhausted", ::ddcs::logger::kv("errno", _errno))

// (Controller) fd 여유가 돌아왔다. rejected는 고갈 동안 거절한 연결 수다.
#define LOG_TRANSPORT_ACCEPT_FD_RECOVER(_rejected)                                                 \
    LOG_INFO("transport.accept.fd_recover", ::ddcs::logger::kv("rejected", _rejected))

// (Controller) 거절용 예비 fd를 되찾지 못했다. 이후 fd 고갈에서 연결을 정중히 끊지 못한다.
#define LOG_TRANSPORT_ACCEPT_SPARE_FD_FAIL(_errno)                                                 \
    LOG_ERROR("transport.accept.spare_fd.fail", ::ddcs::logger::kv("errno", _errno))

// (Agent) 연결을 시도한다.
#define LOG_TRANSPORT_CONNECT(_host, _port)                                                        \
    LOG_DEBUG(                                                                                     \
        "transport.connect", ::ddcs::logger::kv("host", _host), ::ddcs::logger::kv("port", _port)  \
    )

// (Agent) 연결이 서지 못했다. connect() 즉시 실패와 SO_ERROR로 뒤늦게 안 실패가 같이 온다.
#define LOG_TRANSPORT_CONNECT_FAIL(_errno)                                                         \
    LOG_WARN("transport.connect.fail", ::ddcs::logger::kv("errno", _errno))

// (Agent) TCP 연결이 섰다. 등록은 아직이다.
#define LOG_TRANSPORT_CONNECT_SUCCESS(_host, _port)                                                \
    LOG_INFO(                                                                                      \
        "transport.connect.success", ::ddcs::logger::kv("host", _host),                            \
        ::ddcs::logger::kv("port", _port)                                                          \
    )

// (Controller) 이미 쓰는 conn id를 또 발급했다. issue_id가 막으므로 정상 배선에서는 닿지 않는다.
#define LOG_TRANSPORT_CONNECTION_DUPLICATE(_conn)                                                  \
    LOG_WARN("transport.connection.duplicate", ::ddcs::logger::kv("conn", _conn))

// (Controller) app 통지 중 예외가 샜다. event는 "connect", "message", "disconnect" 중 하나다.
#define LOG_TRANSPORT_CONNECTION_NOTIFY_FAIL(_conn, _event)                                        \
    LOG_ERROR(                                                                                     \
        "transport.connection.notify.fail", ::ddcs::logger::kv("conn", _conn),                     \
        ::ddcs::logger::kv("event", _event)                                                        \
    )

// (Controller) 갓 받은 연결을 reactor에 못 올려 그대로 버렸다.
#define LOG_TRANSPORT_CONNECTION_REGISTER_FAIL(_conn, _errno)                                      \
    LOG_WARN(                                                                                      \
        "transport.connection.register.fail", ::ddcs::logger::kv("conn", _conn),                   \
        ::ddcs::logger::kv("errno", _errno)                                                        \
    )

// (Controller) 연결을 조립하다 예외가 나서 그 연결 하나를 포기했다.
#define LOG_TRANSPORT_CONNECTION_SETUP_FAIL() LOG_ERROR("transport.connection.setup.fail")

// (Agent) 연결이 끊겼다. reason이 왜 끊겼는지의 유일한 출처다.
#define LOG_TRANSPORT_DISCONNECT(_reason)                                                          \
    LOG_INFO("transport.disconnect", ::ddcs::logger::kv("reason", _reason))

// 우리 rx ring이 어긋났다. 상대 탓이 아니라 우리 쪽 손상이라 위와 레벨이 갈린다.
#define LOG_TRANSPORT_FRAME_DECODE_CORRUPT() LOG_ERROR("transport.frame.decode.corrupt")
#define LOG_TRANSPORT_FRAME_DECODE_CORRUPT_CONN(_conn)                                             \
    LOG_ERROR("transport.frame.decode.corrupt", ::ddcs::logger::kv("conn", _conn))

// 상대가 보낸 바이트가 frame이 아니다. reason은 "bad_magic" 또는 "too_long"이다.
#define LOG_TRANSPORT_FRAME_DECODE_FAIL(_reason)                                                   \
    LOG_WARN("transport.frame.decode.fail", ::ddcs::logger::kv("reason", _reason))
#define LOG_TRANSPORT_FRAME_DECODE_FAIL_CONN(_conn, _reason)                                       \
    LOG_WARN(                                                                                      \
        "transport.frame.decode.fail", ::ddcs::logger::kv("conn", _conn),                          \
        ::ddcs::logger::kv("reason", _reason)                                                      \
    )

// 송신 frame을 만들지 못해 버렸다. 버퍼는 최대 frame보다 크게 잡으므로 설계상 불가능하다.
#define LOG_TRANSPORT_FRAME_ENCODE_FAIL(_size)                                                     \
    LOG_ERROR("transport.frame.encode.fail", ::ddcs::logger::kv("size", _size))
#define LOG_TRANSPORT_FRAME_ENCODE_FAIL_CONN(_conn, _size)                                         \
    LOG_ERROR(                                                                                     \
        "transport.frame.encode.fail", ::ddcs::logger::kv("conn", _conn),                          \
        ::ddcs::logger::kv("size", _size)                                                          \
    )

// (Agent) controller 주소를 풀지 못했다. 풀릴 때까지 재연결마다 반복되므로 한 번만 알린다.
#define LOG_TRANSPORT_HOST_RESOLVE_FAIL(_host, _eai)                                               \
    LOG_ERROR(                                                                                     \
        "transport.host.resolve.fail", ::ddcs::logger::kv("host", _host),                          \
        ::ddcs::logger::kv("eai", _eai)                                                            \
    )

// (Agent) 주소가 다시 풀렸다. attempts는 못 푸는 동안 헛돈 재연결 횟수다.
#define LOG_TRANSPORT_HOST_RESOLVE_RECOVER(_host, _attempts)                                       \
    LOG_INFO(                                                                                      \
        "transport.host.resolve.recover", ::ddcs::logger::kv("host", _host),                       \
        ::ddcs::logger::kv("attempts", _attempts)                                                  \
    )

// (Controller) 연결을 받는 소켓을 열었다.
#define LOG_TRANSPORT_LISTEN(_port) LOG_INFO("transport.listen", ::ddcs::logger::kv("port", _port))

// (Controller) 리스닝 소켓이 죽었다. 이후 새 연결을 받지 못한다.
#define LOG_TRANSPORT_LISTEN_FAIL(_events)                                                         \
    LOG_ERROR("transport.listen.fail", ::ddcs::logger::kv("events", _events))

// reactor에 채널을 올리지 못했다.
#define LOG_TRANSPORT_REACTOR_ADD_FAIL(_errno)                                                     \
    LOG_WARN("transport.reactor.add.fail", ::ddcs::logger::kv("errno", _errno))

// reactor의 관심 이벤트를 바꾸지 못했다.
#define LOG_TRANSPORT_REACTOR_MODIFY_FAIL(_errno)                                                  \
    LOG_WARN("transport.reactor.modify.fail", ::ddcs::logger::kv("errno", _errno))
#define LOG_TRANSPORT_REACTOR_MODIFY_FAIL_CONN(_conn, _errno)                                      \
    LOG_WARN(                                                                                      \
        "transport.reactor.modify.fail", ::ddcs::logger::kv("conn", _conn),                        \
        ::ddcs::logger::kv("errno", _errno)                                                        \
    )

// 수신 syscall이 실패했다.
#define LOG_TRANSPORT_RECEIVE_FAIL(_errno)                                                         \
    LOG_WARN("transport.receive.fail", ::ddcs::logger::kv("errno", _errno))
#define LOG_TRANSPORT_RECEIVE_FAIL_CONN(_conn, _errno)                                             \
    LOG_WARN(                                                                                      \
        "transport.receive.fail", ::ddcs::logger::kv("conn", _conn),                               \
        ::ddcs::logger::kv("errno", _errno)                                                        \
    )

// (Agent) backoff 후 다시 붙기로 예약했다.
#define LOG_TRANSPORT_RECONNECT_SCHEDULE(_delay_ms)                                                \
    LOG_DEBUG("transport.reconnect.schedule", ::ddcs::logger::kv("delay_ms", _delay_ms))

// 설정한 rx 버퍼 크기가 frame 계약에 맞지 않아 키웠다.
#define LOG_TRANSPORT_RX_BUFFER_ADJUST(_requested, _effective)                                     \
    LOG_WARN(                                                                                      \
        "transport.rx_buffer.adjust", ::ddcs::logger::kv("requested", _requested),                 \
        ::ddcs::logger::kv("effective", _effective)                                                \
    )

// 송신 syscall이 실패했다.
#define LOG_TRANSPORT_SEND_FAIL(_errno)                                                            \
    LOG_WARN("transport.send.fail", ::ddcs::logger::kv("errno", _errno))
#define LOG_TRANSPORT_SEND_FAIL_CONN(_conn, _errno)                                                \
    LOG_WARN(                                                                                      \
        "transport.send.fail", ::ddcs::logger::kv("conn", _conn),                                  \
        ::ddcs::logger::kv("errno", _errno)                                                        \
    )

// (Agent) 연결 상태 기계가 못 가는 전이를 요구받았다. 정상 배선에서는 닿지 않는다.
#define LOG_TRANSPORT_TRANSITION_INVALID(_from, _to)                                               \
    LOG_ERROR(                                                                                     \
        "transport.transition.invalid", ::ddcs::logger::kv("from", _from),                         \
        ::ddcs::logger::kv("to", _to)                                                              \
    )
