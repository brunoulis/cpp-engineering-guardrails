// C++17 executable examples, not a semantic analyzer or memory-safety proof.
// Explicit checks stay active in Release builds. No undefined behavior is run.
#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Visible body: writes elements, never changes structure or retains an alias.
void normalize_in_place(std::vector<double>& values) noexcept {
    for (double& value : values) value *= 2.0;
}

std::optional<double> first_with_known_effects(std::vector<double>& values) {
    if (values.empty()) return std::nullopt;
    double& first = values.front();
    normalize_in_place(values);
    return first;
}

// A different, explicit contract: select the first element AFTER the callback.
// Not an automatic behavior-preserving fix for a borrow taken before the call.
std::optional<double> first_after_callback(
    std::vector<double>& values, void (*callback)(std::vector<double>&)) {
    callback(values); // test callers supply a non-null synchronous callback
    if (values.empty()) return std::nullopt;
    return values.front();
}

void replace_storage(std::vector<double>& values) {
    std::vector<double> replacement(values.size() + 1, 7.0);
    values.swap(replacement);
} // old elements are destroyed here; no capacity-growth assumption is needed

void clear_values(std::vector<double>& values) { values.clear(); }

struct FillResult { bool success; std::size_t written; };
using Fill = FillResult (*)(char*, std::size_t);

std::optional<std::string> read_initialized_prefix(Fill fill) {
    std::array<char, 8> buffer; // only the reported prefix will be initialized
    const FillResult result = fill(buffer.data(), buffer.size());
    if (!result.success || result.written > buffer.size()) return std::nullopt;
    return std::string(buffer.data(), result.written);
}

FillResult partial_fill(char* out, std::size_t capacity) {
    if (capacity < 2) return {false, 0};
    out[0] = 'o'; out[1] = 'k';
    return {true, 2};
}

FillResult empty_fill(char*, std::size_t) { return {true, 0}; }

FillResult full_fill(char* out, std::size_t capacity) {
    for (std::size_t i = 0; i < capacity; ++i) out[i] = 'x';
    return {true, capacity};
}

FillResult failed_fill(char* out, std::size_t capacity) {
    if (capacity != 0) out[0] = '!';
    return {false, capacity != 0 ? 1u : 0u};
}

// Broken length metadata, without an out-of-bounds write. Consumer must reject.
FillResult impossible_length(char*, std::size_t capacity) {
    return {true, capacity + 1};
}
} // namespace

int main() {
    try {
        std::vector<double> values{3.0, 4.0};
        double* alias = &values.front();
        require(first_with_known_effects(values) == 6.0, "in-place result");
        require(&values.front() == alias && *alias == 6.0, "borrow preserved");
        std::vector<double> empty;
        require(!first_with_known_effects(empty), "empty input has no first");
        require(first_after_callback(values, replace_storage) == 7.0,
                "read after storage replacement");
        // alias now dangles and is never used again.
        require(!first_after_callback(values, clear_values), "check after clear");
        require(first_after_callback(empty, replace_storage) == 7.0,
                "callback can populate empty input");
        require(read_initialized_prefix(partial_fill) == "ok", "partial prefix");
        require(read_initialized_prefix(empty_fill) == "", "zero written bytes");
        require(read_initialized_prefix(full_fill) == std::string(8, 'x'),
                "full capacity initialized");
        require(!read_initialized_prefix(failed_fill), "discard failed output");
        require(!read_initialized_prefix(impossible_length), "reject bad length");
        std::cout << "Safety contract examples PASS: in-place/replaced/empty, "
                     "partial/full/failed output; no semantic proof claimed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
