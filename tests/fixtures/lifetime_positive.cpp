#include <string>
#include <string_view>
#include <utility>
std::string make_name() { return "Paceforge"; }
void process(std::string_view name) { (void)name.size(); }
int main() { auto name = make_name(); process(name); }
