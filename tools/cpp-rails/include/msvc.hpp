#pragma once

#include "cpp_rails.hpp"

namespace cpprails {
// Reads selected cl.exe's actual FileVersion (19.minor.build), not its toolset
// directory name. Throws when unavailable/unsupported, including non-Windows.
std::string msvc_compiler_version(const fs::path& compiler);

// Explicitly evaluates a trusted project with installed MSBuild, without targets.
// Writes reconstructed Clang analysis arguments, not an original build invocation.
// Failure leaves an existing output untouched and adds a non-passing required gate.
void export_msvc_compilation_database(const fs::path& project,
                                      const std::string& config,
                                      const std::string& platform,
                                      const fs::path& output,
                                      Report& report);
}
