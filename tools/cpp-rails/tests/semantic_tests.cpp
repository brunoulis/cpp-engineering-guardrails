#include "semantic.hpp"
#include "msvc.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

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
    Temp() {
        fs::create_directories(path / "source space");
#ifdef _WIN32
        // Hosted Windows TEMP can use an 8.3 alias. Exercise that root identity
        // locally as well, while retaining physical fragment-relative paths.
        const auto needed = GetShortPathNameW(path.c_str(), nullptr, 0);
        if (needed) {
            std::wstring short_path(needed, L'\0');
            const auto written = GetShortPathNameW(path.c_str(), short_path.data(), needed);
            if (written && written < needed) { short_path.resize(written); path = short_path; }
        }
#endif
    }
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
    // Included implementation fragments must be analyzed only in their real TU.
    const auto fragment = temp.path / "source space" / "body.inc";
    const auto unrequested = temp.path / "source space" / "not-scanned.inl";
    put(fragment, "\n\nvoid fragment_bad() { auto* p = new int[2]; delete p; }\n");
    put(unrequested, "void ignored_fragment() { auto* p = new int[2]; delete p; }\n");
    put(source, "#include \"body.inc\"\n#include \"not-scanned.inl\"\n");
    Report included;
    analyze_semantic(options, temp.path, {source, fragment}, included);
    require(passed(included), "included selected fragment must have real TU coverage");
    if (included.findings.size() != 1 || included.findings[0].path != "source space/body.inc" || included.findings[0].line != 3)
        std::cerr << "Fragment root: " << utf8_bytes(temp.path.u8string()) << '\n' << json(included);
    require(included.findings.size() == 1 && included.findings[0].rule == "SEM-DELETE-MISMATCH" &&
            included.findings[0].path == "source space/body.inc" && included.findings[0].line == 3,
            "AST fragment finding must retain physical path and line; unrequested fragments are projected out");
    auto fragment_effects = std::ranges::find_if(included.sections, [](const auto& section) { return section.name == "safety-function-effects"; });
    require(fragment_effects != included.sections.end() && fragment_effects->json.find("source space/body.inc") != std::string::npos &&
            fragment_effects->json.find("\"translation_unit\":\"source space/case.cpp\"") != std::string::npos,
            "fragment effect evidence must bind the physical source and includer translation unit");
    const auto statement_fragment = temp.path / "source space" / "statements.inl";
    put(statement_fragment, "\n\nauto* p = new int[2]; delete p;\n");
    put(source, "void included_statements() {\n#include \"statements.inl\"\n}\n");
    Report statements;
    analyze_semantic(options, temp.path, {source, statement_fragment}, statements);
    require(passed(statements) && statements.findings.size() == 1 && statements.findings[0].path == "source space/statements.inl" && statements.findings[0].line == 3,
            "fragments inside function bodies must retain physical expression locations");
    put(statement_fragment, "{ auto* p = new int[2]; delete p; }\n");
    put(source, "void included_whole_body()\n#include \"statements.inl\"\n");
    Report whole_body;
    analyze_semantic(options, temp.path, {source, statement_fragment}, whole_body);
    require(passed(whole_body) && whole_body.findings.size() == 1 && whole_body.findings[0].path == "source space/statements.inl" && whole_body.findings[0].line == 1,
            "range end file transitions must propagate into an included complete function body");
    put(source, "#include \"body.inc\"\n#include \"not-scanned.inl\"\n");
    const auto inactive = temp.path / "source space" / "inactive.inl";
    put(inactive, "void inactive();\n");
    Report fragment_uncovered;
    analyze_semantic(options, temp.path, {source, fragment, inactive}, fragment_uncovered);
    require(!passed(fragment_uncovered) && fragment_uncovered.required_unverified(), "unincluded fragment must remain explicitly UNVERIFIED");
    put(unrequested, "p = nullptr;\n");
    put(source, "void opaque_include() { auto* p = new int; delete p;\n#include \"not-scanned.inl\"\ndelete p; }\n");
    Report excluded_effect;
    analyze_semantic(options, temp.path, {source}, excluded_effect);
    require(passed(excluded_effect) && !rule(excluded_effect, "SEM-DOUBLE-DELETE"),
            "excluded include effects must invalidate ownership proofs instead of disappearing from the function");
    const auto unicode_dir = temp.path / path_from_utf8("prueba-\xc3\xb1");
    fs::create_directory(unicode_dir);
    const auto unicode_source = unicode_dir / "case.cpp";
    const auto unicode_fragment = unicode_dir / path_from_utf8("c\xc3\xb3" "digo.inl");
    put(unicode_source, "#include \"c\xc3\xb3" "digo.inl\"\n");
    put(unicode_fragment, "\nvoid unicode_bad() { auto* p = new int[2]; delete p; }\n");
    put(database, "[{\"directory\":" + quote(utf8_bytes(temp.path.generic_u8string())) + ",\"file\":" + quote(utf8_bytes(unicode_source.generic_u8string())) +
        ",\"arguments\":[\"clang++\",\"-std=c++23\"," + quote(utf8_bytes(unicode_source.generic_u8string())) + "]}]");
    Report unicode;
    analyze_semantic(options, temp.path, {unicode_source, unicode_fragment}, unicode);
    require(passed(unicode) && unicode.findings.size() == 1 && unicode.findings[0].path == "prueba-\xc3\xb1/c\xc3\xb3" "digo.inl" && unicode.findings[0].line == 2,
            "Unicode project/fragment paths must round-trip UTF-8 through AST projection and evidence");
    put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) + ",\"arguments\":[\"clang++\",\"-std=c++23\"," + quote(source.generic_string()) + "]}]");
#ifdef _WIN32
    const auto* vc_tools = std::getenv("VCToolsInstallDir");
    require(vc_tools && *vc_tools, "Windows real-MSVC integration requires a Visual Studio developer environment");
    auto toolset = path_from_utf8(vc_tools);
    if (toolset.filename().empty()) toolset = toolset.parent_path();
    const auto actual_cl = toolset / "bin" / "Hostx64" / "x64" / "cl.exe";
    require(fs::is_regular_file(actual_cl), "real x64 cl.exe must be available for context fidelity regressions");
    const auto actual_compatibility = msvc_compiler_version(actual_cl);
    const auto include_dir = temp.path / "include space";
    fs::create_directory(include_dir);
    put(include_dir / "config.hpp", "#define FROM_INCLUDE 77\n");
    put(source, R"cpp(
#include "config.hpp"
#include <string>
#include <string_view>
#include <span>
#include <vector>
#if FROM_COMMAND != 41 || FROM_INCLUDE != 77 || !defined(_DLL) || !defined(_MT) || !defined(_CPPUNWIND)
#error MSVC compilation context was lost
#endif
static_assert(sizeof(void*) == 8);
std::string_view msvc_bad() { std::string text = "MSVC STL"; return text; }
)cpp");
    const auto msvc_database = [&](const std::string& driver, const std::string& extra = "") {
        put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) +
            ",\"arguments\":[" + quote(driver) + ",\"/nologo\",\"/std:c++latest\",\"/EHsc\",\"/MD\",\"/Zc:__cplusplus\",\"/permissive-\",\"/DFROM_COMMAND=41\",\"/I\"," +
            quote(utf8_bytes(include_dir.u8string()) + "\\") + ",\"/showIncludes\",\"/c\",\"/Fo\",\"ignored.obj\",\"/Fdignored.pdb\"," + quote(source.generic_string()) + extra + "]}]");
    };
    for (const auto& driver : {utf8_bytes(actual_cl.u8string()), std::string("C:\\Program Files\\LLVM\\bin\\clang-cl.exe")}) {
        // A reconstructed clang-cl DB can provide its target/version explicitly
        // even when the original driver is absent. The configured real Clang
        // still parses the installed MSVC STL; this is not an AST mock.
        const auto explicit_context = driver.find("clang-cl") == std::string::npos ? std::string{} :
            ",\"/clang:--target=x86_64-pc-windows-msvc\"," + quote("/clang:-fms-compatibility-version=" + actual_compatibility);
        msvc_database(driver, explicit_context);
        Report msvc;
        analyze_semantic(options, temp.path, {source}, msvc);
        if (!passed(msvc)) for (const auto& note : msvc.notes) std::cerr << note << '\n';
        require(passed(msvc) && rule(msvc, "SEM-DANGLING-VIEW"), "MSVC argv must preserve actual defines, includes, CRT, ABI and real STL in CL driver mode");
        require(!fs::exists(temp.path / "ignored.obj") && !fs::exists(temp.path / "ignored.pdb"), "MSVC replay must not emit object/debug files");
        std::vector<fs::path> deps;
        std::string details;
        require(semantic_dependencies(options, temp.path, source, deps, details) && deps.size() > 4,
                "CL driver mode must produce complete dependency closure including real MSVC STL");
    }
    // CRT quoting doubles the backslash immediately before a closing quote.
    const auto cl_command = std::string("\"") + utf8_bytes(actual_cl.u8string()) + "\" /nologo -std:c++latest -EHsc -MD -DFROM_COMMAND=41 -I\"" +
        utf8_bytes(include_dir.u8string()) + "\\\\\" /showIncludes /c /Fo\"ignored object.obj\" \"source space\\case.cpp\"";
    put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) + ",\"command\":" + quote(cl_command) + "}]");
    Report msvc_command;
    analyze_semantic(options, temp.path, {source}, msvc_command);
    if (!passed(msvc_command)) for (const auto& note : msvc_command.notes) std::cerr << note << '\n';
    require(passed(msvc_command) && rule(msvc_command, "SEM-DANGLING-VIEW"), "MSVC command tokenization must preserve spaces and quoted trailing backslashes");
    put(source, "#if !defined(_M_IX86) || defined(_M_X64)\n#error Wrong MSVC target ABI\n#endif\nstatic_assert(sizeof(void*) == 4);\nvoid x86_bad() { auto* p = new int[2]; delete p; }\n");
    const auto actual_x86 = toolset / "bin" / "Hostx64" / "x86" / "cl.exe";
    require(fs::is_regular_file(actual_x86), "real x86 cl.exe must be available for target fidelity regressions");
    msvc_database(utf8_bytes(actual_x86.u8string()));
    Report x86_target;
    analyze_semantic(options, temp.path, {source}, x86_target);
    require(passed(x86_target) && rule(x86_target, "SEM-DELETE-MISMATCH"), "real MSVC executable paths must preserve the selected x86 target ABI");
    const std::wstring original_path = _wgetenv(L"PATH") ? _wgetenv(L"PATH") : L"";
    _wputenv_s(L"PATH", (actual_x86.parent_path().wstring() + L";" + original_path).c_str());
    msvc_database("cl.exe");
    Report bare_x86;
    analyze_semantic(options, temp.path, {source}, bare_x86);
    _wputenv_s(L"PATH", original_path.c_str());
    require(passed(bare_x86) && rule(bare_x86, "SEM-DELETE-MISMATCH"), "bare cl.exe must bind the original driver resolved from PATH, including its x86 ABI");
    require(x86_target.analyzer_contexts.at("clang-ast") == bare_x86.analyzer_contexts.at("clang-ast"),
            "equivalent resolved compiler contexts must have the same analyzer identity regardless of original argv0 spelling");
    // The installed compiler's FileVersion, not its toolset directory revision,
    // controls _MSC_VER/_MSC_FULL_VER in replayed conditional compilation.
    const auto dot = actual_compatibility.find('.'), last_dot = actual_compatibility.rfind('.');
    const auto major = std::stoul(actual_compatibility.substr(0, dot));
    const auto minor = std::stoul(actual_compatibility.substr(dot + 1, last_dot - dot - 1));
    const auto build = std::stoul(actual_compatibility.substr(last_dot + 1));
    put(source, "static_assert(_MSC_VER == " + std::to_string(major * 100 + minor) + ");\nstatic_assert(_MSC_FULL_VER == " + std::to_string((major * 100 + minor) * 100000 + build) + ");\nint exact_original_version;\n");
    std::map<std::string, std::string> version_contexts;
    for (const auto& entry : fs::directory_iterator(toolset.parent_path())) {
        const auto original = entry.path() / "bin" / "Hostx64" / "x64" / "cl.exe";
        if (!fs::is_regular_file(original)) continue;
        const auto version = msvc_compiler_version(original);
        const auto first = version.find('.'), last = version.rfind('.');
        const auto msc = std::stoul(version.substr(0, first)) * 100 + std::stoul(version.substr(first + 1, last - first - 1));
        put(source, "static_assert(_MSC_VER == " + std::to_string(msc) + ");\nstatic_assert(_MSC_FULL_VER == " + std::to_string(msc * 100000 + std::stoul(version.substr(last + 1))) + ");\nint exact_original_version;\n");
        msvc_database(utf8_bytes(original.u8string()));
        Report original_version;
        analyze_semantic(options, temp.path, {source}, original_version);
        require(passed(original_version), "each real installed MSVC compiler version must preserve its exact predefined compatibility version");
        version_contexts.emplace(version, original_version.analyzer_contexts.at("clang-ast"));
    }
    require(!version_contexts.empty(), "the installed original-compiler version matrix must execute at least one real case");
    if (version_contexts.size() > 1) require(version_contexts.begin()->second != version_contexts.rbegin()->second,
            "different original MSVC versions must change effective analyzer context");
    put(source, "int trusted_identity;\n");
    const auto trusted_alias = temp.path / "clang-cl.exe";
    fs::create_hard_link(path_from_utf8(options.clang_executable), trusted_alias);
    msvc_database(utf8_bytes(trusted_alias.u8string()));
    Report trusted_clang_cl;
    analyze_semantic(options, temp.path, {source}, trusted_clang_cl);
    if (!passed(trusted_clang_cl)) for (const auto& note : trusted_clang_cl.notes) std::cerr << note << '\n';
    require(passed(trusted_clang_cl), "the explicitly selected trusted Clang binary may supply its own clang-cl compatibility predefines");
    const std::string original_cl_options = std::getenv("CL") ? std::getenv("CL") : "";
    _putenv_s("CL", "/clang:-fpass-plugin=must-not-load.dll");
    msvc_database(utf8_bytes(actual_cl.u8string()));
    Report hidden_options;
    analyze_semantic(options, temp.path, {source}, hidden_options);
    _putenv_s("CL", original_cl_options.c_str());
    require(!passed(hidden_options) && std::ranges::any_of(hidden_options.notes, [](const auto& note) { return note.find("hidden compiler environment options are unsupported: CL") != std::string::npos; }),
            "ambient CL plugin/response options must be rejected before compiler replay");
    msvc_database("C:\\missing-msvc\\cl.exe");
    Report unknown_identity;
    analyze_semantic(options, temp.path, {source}, unknown_identity);
    require(!passed(unknown_identity) && unknown_identity.required_unverified(), "unresolved original MSVC driver context must fail closed");
    for (const auto& flag : {std::string("/unsupported-semantic-option"), std::string("@hidden.rsp"), std::string("/clang:-fpass-plugin=hidden.dll"), std::string("/B1hidden.exe")}) {
        msvc_database("cl.exe", "," + quote(flag));
        Report unsupported;
        analyze_semantic(options, temp.path, {source}, unsupported);
        require(!passed(unsupported) && unsupported.required_unverified(), "unsupported MSVC options, response files and plugins must fail closed");
        require(std::ranges::any_of(unsupported.notes, [&](const auto& note) { return note.find("unsupported MSVC semantic argument: " + flag) != std::string::npos; }),
                "unsupported MSVC replay must be rejected before compiler execution");
    }
    put(database, "[{\"directory\":" + quote(temp.path.generic_string()) + ",\"file\":" + quote(source.generic_string()) + ",\"arguments\":[\"clang++\",\"-std=c++23\"," + quote(source.generic_string()) + "]}]");
#endif
    // Model libc++'s conversion-to-alias AST spelling independently of the host
    // standard library. This is a protocol fixture, not a replacement STL.
    put(source, R"cpp(
namespace std { inline namespace __1 {
template<class C> struct basic_string_view { const C* pointer; };
template<class C> struct basic_string {
    using __self_view = basic_string_view<C>;
    C bytes[1]{};
    operator __self_view() const { return {bytes}; }
};
using string = basic_string<char>;
using string_view = basic_string_view<char>;
} }
#define RETURN_ALIAS(value) return value
std::string_view bad_alias() { std::string text; RETURN_ALIAS(text); }
std::string_view good_alias_parameter(const std::string& text) { return text; }
std::string_view good_alias_static() { static std::string text; return text; }
)cpp");
    Report alias_conversion;
    analyze_semantic(options, temp.path, {source}, alias_conversion);
    require(passed(alias_conversion), "conversion alias AST must be covered");
    require(std::ranges::count_if(alias_conversion.findings, [](const auto& finding) {
        return finding.rule == "SEM-DANGLING-VIEW" && finding.evidence.find("bad_alias") != std::string::npos;
    }) == 1, "canonical return type must recognize libc++ conversion aliases");
    require(alias_conversion.findings.size() == 1, "borrowed/static alias conversions must stay negative");
    // Apple Clang can retain standard typedef spellings without a desugared
    // template type. Keep that AST representation covered on every host.
    put(source, R"cpp(
namespace std {
struct string_view { const char* pointer; };
struct string {
    using __self_view = string_view;
    char bytes[1]{};
    operator __self_view() const { return {bytes}; }
};
}
struct string_like { operator std::string_view() const { return {"safe"}; } };
#define RETURN_SHORTNAME(value) return value
std::string_view bad_shortname() { std::string text; RETURN_SHORTNAME(text); }
std::string_view good_shortname_parameter(const std::string& text) { return text; }
std::string_view good_shortname_static() { static std::string text; return text; }
std::string_view good_shortname_wrapper() { string_like text; return text; }
)cpp");
    Report standard_spelling;
    analyze_semantic(options, temp.path, {source}, standard_spelling);
    require(passed(standard_spelling), "standard typedef spelling AST must be covered");
    require(standard_spelling.findings.size() == 1 && standard_spelling.findings.front().rule == "SEM-DANGLING-VIEW" &&
            standard_spelling.findings.front().evidence.find("bad_shortname") != std::string::npos,
            "standard string/string_view spellings must retain lifetime evidence without matching user wrappers");
    put(unrequested, "values = {3, 4}; view = std::span<int>{values};\n");
    put(temp.path / "source space" / "index-reset.inl", "(values = std::vector<int>{7}, view = std::span<int>{values}, 0)\n");
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
int good_excluded_invalidation() { std::vector<int> values{1, 2}; std::span<int> view{values}; values.clear();
#include "not-scanned.inl"
return view[0]; }
int good_excluded_index() { std::vector<int> values{1, 2}; std::span<int> view{values}; values.clear(); return view[
#include "index-reset.inl"
]; }
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
    if (count != 2) {
        for (const auto& finding : analyzed.findings) std::cerr << finding.rule << " " << finding.evidence << '\n';
        std::cerr << exec({options.clang_executable, "--version"}, temp.path).output;
        std::cerr << exec({options.clang_executable, "-std=c++23", "-fsyntax-only", "-Xclang", "-ast-dump", "-Xclang", "-ast-dump-filter=bad_string", utf8_bytes(source.u8string())}, temp.path).output;
    }
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
    auto missing_module = temp.path / "unlisted.ixx";
    put(missing_module, "export module unlisted;\n");
    Report uncovered_module;
    analyze_semantic(options, temp.path, {source, missing_module}, uncovered_module);
    require(!passed(uncovered_module) && uncovered_module.required_unverified(), "unmatched selected module translation units must be UNVERIFIED");
    std::cout << "PASS semantic compiler/database/AST/safety/budget/quoted-command tests\n";
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
