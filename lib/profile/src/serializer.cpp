#include "ddcs/profile/serializer.hpp"

#include "ddcs/json/writer.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ddcs::profile {

namespace {

void append_key(std::string& out, std::string_view key) {
    ddcs::json::append_string_literal(out, key);
    out += ':';
}

void append_uint64(std::string& out, std::uint64_t value) {
    ddcs::json::append_number(out, value);
}

void append_sample(std::string& out, TickSample const& sample) {
    out += '{';

    append_key(out, "tick_id");
    append_uint64(out, sample.tick_id);
    out += ',';

    append_key(out, "started_ns");
    append_uint64(out, sample.started_ns);
    out += ',';

    append_key(out, "command_sweep_ended_ns");
    if (command_sweep_completed(sample.outcome)) {
        append_uint64(out, sample.command_sweep_ended_ns);
    } else {
        ddcs::json::append_null(out);
    }
    out += ',';

    append_key(out, "session_sweep_ended_ns");
    if (session_sweep_completed(sample.outcome)) {
        append_uint64(out, sample.session_sweep_ended_ns);
    } else {
        ddcs::json::append_null(out);
    }
    out += ',';

    append_key(out, "policy_evaluate_ended_ns");
    if (policy_evaluate_completed(sample.outcome)) {
        append_uint64(out, sample.policy_evaluate_ended_ns);
    } else {
        ddcs::json::append_null(out);
    }
    out += ',';

    append_key(out, "finished_ns");
    append_uint64(out, sample.finished_ns);
    out += ',';

    append_key(out, "outcome");
    ddcs::json::append_string_literal(out, to_string(sample.outcome));

    out += '}';
}

} // namespace

std::optional<std::string>
serialize_recording(RecordingView const& recording, RunMetadata const& metadata) {
    if (metadata.run_id.empty()) {
        return std::nullopt;
    }
    if (metadata.recording_origin_utc && metadata.recording_origin_utc->before_unix_ns >
                                             metadata.recording_origin_utc->after_unix_ns) {
        return std::nullopt;
    }

    for (TickSample const& sample : recording.samples()) {
        if (!is_valid_tick_sample(sample)) {
            return std::nullopt;
        }
    }

    std::string out;
    out += '{';

    append_key(out, "schema_name");
    ddcs::json::append_string_literal(out, tick_profile_schema_name);
    out += ',';

    append_key(out, "schema_version");
    append_uint64(out, tick_profile_schema_version);
    out += ',';

    append_key(out, "time_unit");
    ddcs::json::append_string_literal(out, tick_profile_time_unit);
    out += ',';

    append_key(out, "run_id");
    ddcs::json::append_string_literal(out, metadata.run_id);
    out += ',';

    append_key(out, "recording_origin_utc");
    if (metadata.recording_origin_utc) {
        out += '{';
        append_key(out, "before_unix_ns");
        append_uint64(out, metadata.recording_origin_utc->before_unix_ns);
        out += ',';
        append_key(out, "after_unix_ns");
        append_uint64(out, metadata.recording_origin_utc->after_unix_ns);
        out += '}';
    } else {
        ddcs::json::append_null(out);
    }
    out += ',';

    append_key(out, "recording");
    out += '{';
    append_key(out, "capacity");
    append_uint64(out, recording.capacity());
    out += ',';
    append_key(out, "storage_bytes");
    append_uint64(out, recording.storage_bytes());
    out += ',';
    append_key(out, "captured");
    append_uint64(out, recording.samples().size());
    out += ',';
    append_key(out, "dropped");
    append_uint64(out, recording.dropped());
    out += '}';
    out += ',';

    append_key(out, "samples");
    out += '[';
    bool first = true;
    for (TickSample const& sample : recording.samples()) {
        if (!first) {
            out += ',';
        }
        append_sample(out, sample);
        first = false;
    }
    out += ']';

    out += '}';
    return out;
}

} // namespace ddcs::profile
