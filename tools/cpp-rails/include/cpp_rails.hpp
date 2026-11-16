#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace cpprails {
namespace fs = std::filesystem;

// Preserve UTF-8 code units at the std::string process/report boundary.
std::string utf8_bytes(std::u8string_view text);
fs::path path_from_utf8(std::string_view text);

enum class Status { Pass, Fail, Unverified, NotApplicable };
struct Gate {
    std::string name;
    Status status = Status::Unverified;
    std::string details;
    bool required = true;
};
struct Finding {
    std::string id, rule, severity, confidence, path, message, evidence, recommendation;
    std::size_t line = 0;
    bool existing = false;
    bool suppressed = false;
    std::string scope = "production";  // "production" or "test" (tests are reported one level lower)
    std::string analyzer = "lexical";
    std::string evidence_level = "heuristic";
};
struct AnalysisSection { std::string name, json, markdown; };
struct BaselineDelta {
    bool loaded = false;
    std::size_t added = 0, known = 0, unobserved = 0;
    std::vector<Finding> resolved;
    std::vector<Finding> unobserved_entries;
};
struct Report {
    std::string action, root, profile;
    std::size_t files_scanned = 0, files_skipped = 0, findings_capped = 0;
    std::vector<Finding> findings;
    std::vector<Gate> gates;
    std::vector<std::string> notes;
    std::vector<AnalysisSection> sections;
    std::size_t code_lines = 0, production_lines = 0, test_lines = 0;
    std::set<std::string> scanned_paths;
    bool scan_complete = false;
    BaselineDelta delta;
    std::map<std::string,std::string> analyzer_contexts;
    std::set<std::string> completed_analyzers;
    void gate(std::string name, Status status, std::string details, bool required = true);
    bool has_failure() const;
    bool required_unverified() const;
    bool any_unverified() const;
    std::size_t new_unsuppressed_p2() const;
};
struct Profile {
    bool require_build = true, require_tests = true, require_lint = false;
    bool require_asan_ubsan = false, require_tsan = false, require_abi = false;
    bool require_benchmark = false, block_heuristic_p2 = false;
    unsigned min_samples = 5;
    double max_regression_pct = 5.0;
};
struct Options {
    bool check_program = false, changed = false, strict = false, lint = false;
    bool no_build = false, no_tests = false, lifetime = false, configure = false;
    bool help = false, compile_checks = false, exclude_tests = false;
    bool score = false, semantic = false, safety = false, targets = false;
    bool lint_cache = false, fail_on_new_confirmed = false;
    unsigned analysis_budget = 0, lint_batch_size = 30, lint_max_units = 0;
    unsigned lint_budget_seconds = 0;
    std::string clang_executable = "clang++", tidy_executable = "clang-tidy";
    std::optional<fs::path> compilation_database, project_rules;
    std::vector<fs::path> include_dirs;
    std::string expect_error;
    std::string action = "audit", subaction, profile = "general", format = "markdown";
    std::string compiler = "c++", cpp_standard = "c++20", sanitizer, compiler_matrix;
    std::optional<fs::path> root, output, baseline, exception_file, profile_file;
    std::optional<fs::path> build_dir, source, results, abi_old, abi_new;
    std::optional<std::string> config;  // CMake build configuration (Debug/Release/...)
    std::optional<double> max_regression;
    std::optional<unsigned> min_samples;
};
struct Process {
    int exit_code = -1;
    bool started = false;
    std::string output;
    std::string error_output; // populated when separate_error is requested
};
struct Scan {
    std::vector<fs::path> files;
    std::vector<Finding> findings;
    std::vector<std::string> notes;
    std::size_t skipped = 0;
    std::size_t tests_excluded = 0;
    std::size_t capped = 0, capped_files = 0;  // hints dropped by the per-file/per-rule limit
    std::size_t code_lines = 0, production_lines = 0, test_lines = 0;
    std::set<std::string> observed_paths;
};

Options parse_options(bool check_program, const std::vector<std::string>& args);
std::string usage(bool check_program);
std::string markdown(const Report& report);
std::string json(const Report& report);
Process exec(const std::vector<std::string>& args, const fs::path& cwd, bool separate_error = false);
Scan scan(const fs::path& root, const std::optional<std::set<std::string>>& changed = {},
          const std::string& profile = "general", bool exclude_tests = false);
bool is_test_path(const std::string& relative_path);
std::string sarif(const Report& report);
// Include topology over `files`; include_dirs are extra search roots (e.g. from -I flags).
void report_topology(const fs::path& root, const std::vector<fs::path>& files,
                     const std::vector<fs::path>& include_dirs, Report& report);
std::string build_config(const Options& options, const fs::path& build_dir);
std::set<std::string> changed_paths(const fs::path& root, std::vector<std::string>* diagnostics = nullptr);
std::string relative_path(const fs::path& file, const fs::path& root);
std::string make_id(const std::string& rule, const std::string& path, const std::string& evidence, unsigned occurrence);
std::string status_text(Status status);
Profile load_profile(const Options& options, const fs::path& root, Report& report);
void apply_exceptions(const fs::path& path, Report& report);
void load_baseline(const fs::path& path, Report& report);
void write_baseline(const fs::path& path, const Report& report);
void benchmark_compare(const fs::path& path, unsigned min_samples, double max_pct, Report& report);
int dispatch(const Options& options, Report& report);
int cli_entry(bool check_program, int argc, const char* const argv[]);
} // namespace cpprails
