#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ddcs::json {

void append_null(std::string& out);
void append_bool(std::string& out, bool value);
void append_number(std::string& out, std::int64_t value);
void append_number(std::string& out, std::uint64_t value);
// inf/NaN은 JSON에 표현이 없어 null로 쓴다.
void append_number(std::string& out, double value);
void append_string_literal(std::string& out, std::string_view value);

} // namespace ddcs::json
