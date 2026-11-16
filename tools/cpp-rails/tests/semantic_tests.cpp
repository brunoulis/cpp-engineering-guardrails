#include "semantic.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cpprails;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string quote(std::string text) {
    std::string out = "\"";
    for (char ch : text) { if (ch == '\\' || ch == '"') out += '\\'; out += ch; }
    return out + '"';
}
void put(const fs::path& path, const std::string& text) { std::ofstream(path) << text; }
bool rule(const Report& report, const std::string& name) {
    return std::ranges::any_of(report.findings, [&](const auto& f) { return f.rule == name; });
}
bool passed(const Report& report) {
    return std::ranges::any_of(report.gates, [](const auto& g) { return g.name == "semantic" && g.status == Status::Pass; });
}
struct Temp {
    fs::path path = fs::temp_directory_path() / ("cpprails-semantic-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { fs::create_directories(path / "source space"); }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
}
int main(int argc, char** argv) try {
    Temp temp;
    auto source = temp.path / "source space" / "case.cpp";
    put(source, "int main() {}\n");
    Options options;
    options.semantic = true;
    options.safety = true;
    Report missing;
    analyze_semantic(options, temp.path, {source}, missing);
    require(!passed(missing) && missing.required_unverified(), "missing database must be explicitly UNVERIFIED");
    auto database = temp.path / "compile_commands.json";
    options.compilation_database = database;
    put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) + ",\"arguments\":[\"clang++\",\"-std=c++23\"," + quote(source.generic_string()) + "]}]");
    options.clang_executable = "cpprails-no-such-clang-87129";
    Report unavailable;
    analyze_semantic(options, temp.path, {source}, unavailable);
    require(!passed(unavailable) && unavailable.required_unverified(), "missing compiler must be explicitly UNVERIFIED");
    if (argc < 2) { std::cout << "PASS unavailable backend/database; UNVERIFIED real Clang tests: pass clang++ path as argv[1]\n"; return 0; }
    options.clang_executable = argv[1];
    const std::string source_text = R"cpp(
#include <string>
#include <string_view>
#include <span>
#include <vector>
#define RETURN_VIEW(value) return value
std::string_view bad_string() { std::string text = "hello"; RETURN_VIEW(text); }
std::span<int> bad_span() { int values[2]{}; return std::span<int>{values, 2}; }
std::string_view good_literal() { return "hello"; }
std::string_view good_parameter(const std::string& text) { return text; }
std::string_view good_static() { static std::string text = "hi"; return text; }
std::string_view good_tls() { thread_local std::string text = "hi"; return text; }
std::vector<std::string_view> good_container() { std::vector<std::string_view> values{"literal"}; return values; }
std::vector<std::span<int>> good_span_container() { std::vector<std::span<int>> values; return values; }
void good_delete() { auto* p = new int; delete p; }
void bad_delete() { auto* p = new int[2]; delete p; }
void bad_double() { auto* p = new int; delete p; delete p; }
int bad_invalidation() { std::vector<int> values{1, 2}; std::span<int> view{values}; values.clear(); return view[0]; }
int good_view_use() { std::vector<int> values{1, 2}; std::span<int> view{values}; return view[0]; }
void opaque(int*);
void unknown() { auto* p = new int; opaque(p); delete p; delete p; }
struct Wrapper { operator std::string_view() const { return "safe"; } };
std::string_view good_wrapper() { Wrapper wrapper; return wrapper; }
std::string_view good_reassigned() { std::string text = "hello"; std::string_view view{text}; view = "safe"; return view; }
struct Release { Release*& pointer; ~Release() { pointer = nullptr; } };
void good_destructor_effect() { Release* pointer = new Release{pointer}; delete pointer; delete pointer; }
struct Reset { Reset(int*& pointer) { delete pointer; pointer = nullptr; } };
void good_constructor() { auto* pointer = new int; Reset reset(pointer); delete pointer; delete pointer; }
void good_conditional() { auto* pointer = false ? new int[1] : new int; delete pointer; }
void good_throw() { auto* pointer = new int; delete pointer; throw 0; delete pointer; }
)cpp";
    put(source, source_text);
    Report analyzed;
    analyze_semantic(options, temp.path, {source}, analyzed);
    if (!passed(analyzed)) {
        for (const auto& g : analyzed.gates) std::cerr << g.name << ": " << g.details << '\n';
        for (const auto& note : analyzed.notes) std::cerr << note << '\n';
    }
    require(passed(analyzed), "real Clang AST must be analyzed");
    auto effects = std::ranges::find_if(analyzed.sections, [](const auto& section) { return section.name == "safety-function-effects"; });
    require(effects != analyzed.sections.end(), "safety contracts must include inferred per-function effects");
    require(effects->json.find("bad_string") != std::string::npos && effects->json.find("source_fingerprint") != std::string::npos && effects->json.find("compiler_version") != std::string::npos && effects->json.find("compile_command_digest") != std::string::npos, "per-function effects must bind names to source/compiler/command evidence");
    require(rule(analyzed, "SEM-DANGLING-VIEW"), "AST must find local string/span return");
    require(rule(analyzed, "SEM-DELETE-MISMATCH"), "AST must find scalar/array release mismatch");
    require(rule(analyzed, "SEM-DOUBLE-DELETE"), "AST must find consecutive double delete");
    require(rule(analyzed, "SEM-INVALIDATED-VIEW"), "AST must find direct view element access after vector clear");
    for (const auto& finding : analyzed.findings) {
        require(finding.analyzer == "clang-ast" && finding.evidence_level == "verified-structure", "semantic evidence metadata");
        require(finding.evidence.find("good_") == std::string::npos, "valid std and wrapper examples must be negative");
        require(finding.evidence.find("unknown") == std::string::npos, "opaque effects must not be treated as proof");
        require(finding.line > 0, "AST offset must resolve source line");
    }
    auto count = std::ranges::count_if(analyzed.findings, [](const auto& f) { return f.rule == "SEM-DANGLING-VIEW"; });
    if (count != 2) for (const auto& finding : analyzed.findings) std::cerr << finding.rule << " " << finding.evidence << '\n';
    require(count == 2, "exactly two dangling views including macro expansion");
    put(source, "\n\n" + source_text);
    Report shifted;
    analyze_semantic(options, temp.path, {source}, shifted);
    require(passed(shifted), "prefixed source AST must remain covered");
    require(analyzed.findings.size() == shifted.findings.size(), "whitespace must preserve semantic finding count");
    for (const auto& finding : analyzed.findings) require(std::ranges::any_of(shifted.findings, [&](const auto& other) { return finding.id == other.id; }), "semantic baseline IDs must survive line shifts");
    std::vector<fs::path> dependencies;
    std::string dependency_diagnostics;
    require(semantic_dependencies(options, temp.path, source, dependencies, dependency_diagnostics), "compiler must provide complete dependency closure");
    require(dependencies.size() > 4 && std::ranges::any_of(dependencies, [&](const auto& path) { return fs::equivalent(path, source); }), "dependency closure must include source and standard library headers");
    options.analysis_budget = 1;
    Report bounded;
    analyze_semantic(options, temp.path, {source}, bounded);
    require(!passed(bounded) && bounded.required_unverified(), "exhausted AST budget must be UNVERIFIED");
    options.analysis_budget = 0;
    put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) + ",\"command\":\"clang++ -std=c++23 -DNEEDED=1 \\\"source space/case.cpp\\\" -c -o ignored.o\"}]");
    Report command;
    analyze_semantic(options, temp.path, {source}, command);
    require(passed(command) && rule(command, "SEM-DANGLING-VIEW"), "quoted command compilation DB must preserve argv/cwd");
    require(!fs::exists(temp.path / "ignored.o"), "semantic mode must not produce compilation objects");
    put(source, "int listed() { return 0; }\n");
    put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) + ",\"arguments\":[\"clang++\",\"-fpass-plugin=must-not-load.dll\"," + quote(source.generic_string()) + "]}]");
    Report plugin;
    analyze_semantic(options, temp.path, {source}, plugin);
    require(!passed(plugin) && plugin.required_unverified(), "frontend plugins must be rejected before replay");
    put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) + ",\"arguments\":[\"clang++\",\"-std=c++23\"," + quote(source.generic_string()) + "]}]");
    auto missing_source = temp.path / "unlisted.cpp";
    put(missing_source, "int unlisted;\n");
    Report uncovered;
    analyze_semantic(options, temp.path, {source, missing_source}, uncovered);
    require(!passed(uncovered) && uncovered.required_unverified(), "unmatched selected translation units must be UNVERIFIED");
    std::cout << "PASS semantic compiler/database/AST/safety/budget/quoted-command tests\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
