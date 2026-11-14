#include <string>
#include <string_view>
// Deliberately unsafe example; compile-time warnings may depend on compiler version.
std::string_view bad_name() {
    std::string name = "Paceforge";
    return name; // dangling view
}
int main() { return 0; } // intentionally never executes UB
