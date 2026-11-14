#include <array>
#include <concepts>
#include <iterator>
#include <ranges>
#include <vector>

template<class C>
concept InsertableAtEnd = std::ranges::range<C> &&
    requires(C& c, std::ranges::range_value_t<C> v) {
        c.insert(std::ranges::end(c), v);
    };

template<InsertableAtEnd C>
void append_one(C& c, std::ranges::range_value_t<C> v) {
    c.insert(std::ranges::end(c), v);
}

static_assert(InsertableAtEnd<std::vector<int>>);
static_assert(!InsertableAtEnd<std::array<int, 3>>);

int main() { std::vector<int> v{1, 2}; append_one(v, 3); return v.size() != 3; }
