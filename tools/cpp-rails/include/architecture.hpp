#pragma once
#include "cpp_rails.hpp"

namespace cpprails {
// Reads existing CMake File API replies. Never configures or executes the
// target project.
void analyze_architecture(const Options &, const fs::path &root,
                          const std::vector<fs::path> &files, Report &);
// Applies only the explicitly supplied restricted project-rule policy.
void apply_project_rules(const fs::path &policy, const fs::path &root,
                         const std::vector<fs::path> &files, Report &);
} // namespace cpprails
