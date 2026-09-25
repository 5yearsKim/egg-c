#pragma once
#include <cstdint>
#include <limits>

namespace eggc {
using Id = std::uint32_t;
constexpr Id invalid_id = std::numeric_limits<Id>::max();
}
