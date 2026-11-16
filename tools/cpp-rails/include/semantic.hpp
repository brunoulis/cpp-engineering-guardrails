#pragma once

#include "cpp_rails.hpp"

namespace cpprails {
// Opt-in compiler-backed analysis. Missing compilation context never counts as PASS.
// analysis_budget counts visited AST nodes; zero means unlimited visits.
void analyze_semantic(const Options& options, const fs::path& root,
                      const std::vector<fs::path>& files, Report& report);
// Compiler-produced complete dependency closure for conservative result caches.
// Returns false for missing compile context, unsupported argv, or unreadable deps.
bool semantic_dependencies(const Options& options, const fs::path& root,
                           const fs::path& unit, std::vector<fs::path>& dependencies,
                           std::string& diagnostics);
} // namespace cpprails
