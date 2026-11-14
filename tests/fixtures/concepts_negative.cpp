#include <array>
#include <iterator>
#include <ranges>

// Deliberately expected compile failure: std::array does not support insert.
int main() {
    std::array<int, 3> a{1, 2, 3};
    auto it = std::inserter(a, std::ranges::end(a));
    *it = 4;  // instantiates output iterator operation body -> compilation must fail
}
