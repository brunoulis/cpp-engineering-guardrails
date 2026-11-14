# Templates and C++20 concepts: expression validity ≠ body correctness

## Failure mode
A `requires` expression can confirm a call is well formed at the signature/substitution level without instantiating a called function's body. Example: `std::inserter(array, end(array))` may appear constructible for a `std::array` although writing via its output iterator requires `insert`, which `std::array` does not implement.

```cpp
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
static_assert(InsertableAtEnd<std::vector<int>>);
static_assert(!InsertableAtEnd<std::array<int, 4>>);
```

**This checks an expression, not all semantic preconditions.** An algorithm might require additional properties, iterator stability, reserve/capacity, mutable access, copy/move requirements, exception guarantees or `sized_range`.

## Guardrails
- Specify semantic *operation* the generic algorithm needs, not a proxy wrapper whose construction looks valid.
- Test at least two positive types and negative categories (non-growable, const, move-only, proxy references, empty and unsized ranges as applicable).
- Compile and execute the actual body of the algorithm with concrete instantiations; negative `static_assert`s alone are insufficient.
- Check constraints don't silently select a different overload; check overload resolution against imperfect-forwarding traps.
- Avoid replacing comprehensible overloads with complicated concepts when the supported type set is small.
- Do not use unchecked `std::memcpy` or C-style casts to gain genericity.

## Compiler matrix
Use project-supported C++ standard and compilers; GCC, Clang and MSVC library implementations may differ. Mark missing versions UNVERIFIED. Use `tests/fixtures/concepts_positive.cpp` and `concepts_negative.cpp` as a small exemplar, not as coverage of the user's own project.
