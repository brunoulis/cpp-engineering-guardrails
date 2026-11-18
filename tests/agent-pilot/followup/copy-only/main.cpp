#include <fmt/ranges.h>
#include <cstddef>
#include <iterator>
#include <type_traits>

struct Cursor {
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  using pointer = const int*;
  using reference = const int&;
  using iterator_category = std::input_iterator_tag;

  pointer pos;
  explicit Cursor(pointer p) : pos(p) {}
  Cursor(const Cursor&) = default;
  Cursor(Cursor&&) = delete;
  Cursor& operator=(const Cursor&) = default;
  reference operator*() const { return *pos; }
  Cursor& operator++() { ++pos; return *this; }
  Cursor operator++(int) { const auto old = *this; ++*this; return old; }
  bool operator==(const Cursor&) const = default;
};

static_assert(std::is_copy_constructible_v<Cursor>);
static_assert(!std::is_move_constructible_v<Cursor>);

int main() {
  const int values[]{3, 7};
  const Cursor first(values), last(values + 2);
  const auto view = fmt::join(first, last, "/");
  const auto args = fmt::make_format_args(view);
  return fmt::vformat("{}", args) == "3/7" && fmt::vformat("{}", args) == "3/7" ? 0 : 1;
}
