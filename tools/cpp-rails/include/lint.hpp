#pragma once
#include "cpp_rails.hpp"
namespace cpprails {
void run_lint(const Options&,const fs::path&,const std::vector<fs::path>&,Report&,bool required);
}
