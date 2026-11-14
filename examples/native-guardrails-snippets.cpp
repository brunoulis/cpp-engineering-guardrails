// Examples are intentionally small and illustrative; do not prescribe architecture.
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// From Rust/newtypes: prevent mixing IDs with raw counts when bugs are likely.
struct SessionId { explicit SessionId(std::uint64_t n) : value(n) {} std::uint64_t value; };

// From Flutter/state owner: owner returns an owning result, not a dangling view.
std::string session_title(SessionId id) { return "Session " + std::to_string(id.value); }
void show_title(std::string_view borrowed) { (void)borrowed; } // only within owner lifetime

// C++20: bounded non-owning byte span; callers must keep owner alive.
std::optional<std::uint32_t> header_id(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 4) return std::nullopt;
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
         | (std::uint32_t(bytes[2]) << 8) | bytes[3];
}

int main() {
    const auto name = session_title(SessionId{42});
    show_title(name);
    const std::vector<std::uint8_t> bytes{0, 0, 0, 7};
    return header_id(bytes).value_or(0) == 7 ? 0 : 1;
}
