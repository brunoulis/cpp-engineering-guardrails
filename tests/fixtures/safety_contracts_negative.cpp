// Review/syntax-only fixtures. NEVER execute these functions in normal tests.
// Well-formed C++17 is not evidence of safety; warnings are compiler-dependent.
#include <cstddef>
#include <vector>

namespace safety_negative {
void normalize(std::vector<double>&); // external implementation is UNKNOWN

double unknown_call(std::vector<double>& values) {
    double& first = values.front(); // separate nonempty precondition
    normalize(values);             // no evidence that the borrow survives
    return first;                  // invalidation remains UNVERIFIED
}

double unchecked_empty(std::vector<double>& values) {
    return values.front(); // defect if empty input is permitted/reachable
}

double destroyed_element() {
    std::vector<double> values{1.0};
    double& first = values.front();
    values.clear();
    return first; // object lifetime ended even though capacity may be unchanged
}

double replaced_storage() {
    std::vector<double> values{1.0};
    double& first = values.front();
    {
        std::vector<double> replacement{2.0, 3.0};
        values.swap(replacement);
    } // destroys and frees original storage, independently of growth strategy
    return first;
}

int partial_initialization() {
    int output[2];
    output[0] = 7;
    return output[0] + output[1]; // second object was not initialized
}

void retain(const int*); // retention/lifetime contract unknown
void escaped_stack() {
    int local = 4;
    retain(&local); // hazard if retained and read after this function returns
}
} // namespace safety_negative
