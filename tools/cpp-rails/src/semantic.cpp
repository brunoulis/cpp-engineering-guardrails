#include "semantic.hpp"
#include "msvc.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <variant>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cpprails {
namespace {
// Clang's JSON AST is deliberately an optional subprocess protocol: no LLVM
// development headers/library ABI is imposed on the lightweight auditor.
constexpr std::uintmax_t max_json_bytes = 1024ULL * 1024 * 1024;
struct Json {
    struct Atom { std::string text; };
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    std::variant<std::monostate, std::string, Atom, Array, Object> value;
    const Json& operator[](std::string_view key) const {
        static const Json empty;
        if (const auto* object = std::get_if<Object>(&value)) {
            if (const auto it = object->find(key); it != object->end()) return it->second;
        }
        return empty;
    }
    std::string text() const { if (auto p = std::get_if<std::string>(&value)) return *p; if (auto p = std::get_if<Atom>(&value)) return p->text; return {}; }
    const Array& array() const { static const Array empty; if (auto p = std::get_if<Array>(&value)) return *p; return empty; }
    bool has(std::string_view key) const { auto p = std::get_if<Object>(&value); return p && p->contains(key); }
};
class Parser {
    std::string_view input;
    std::size_t at = 0;
    std::set<std::string> ast_sources;
    std::map<std::string, std::string> file_keys;
    std::string ast_file;
    unsigned project_function_depth = 0;
    fs::path ast_cwd;
    std::string path_key(const fs::path& path) {
        const auto raw = utf8_bytes(path.generic_u8string());
        if (auto found = file_keys.find(raw); found != file_keys.end()) return found->second;
        auto result = utf8_bytes(fs::weakly_canonical(path.is_absolute() ? path : ast_cwd / path).generic_u8string());
#ifdef _WIN32
        std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        file_keys.emplace(raw, result); return result;
    }
    void record_location(const Json& value) {
        if (value.has("spellingLoc")) record_location(value["spellingLoc"]);
        const auto& loc = value.has("expansionLoc") ? value["expansionLoc"] : value;
        if (!loc["file"].text().empty()) ast_file = path_key(path_from_utf8(loc["file"].text()));
    }
    void space() { while (at < input.size() && std::isspace(static_cast<unsigned char>(input[at]))) ++at; }
    [[noreturn]] void error() const { throw std::runtime_error("invalid JSON at byte " + std::to_string(at)); }
    void skip(bool locations = true) {
        space(); if (at == input.size()) error();
        if (input[at] != '[' && input[at] != '{') { (void)parse(0); return; }
        std::vector<char> brackets; bool in_string = false;
        do {
            if (at == input.size()) error();
            // Filename elision follows the complete JSON traversal, including
            // projected header bodies. Track physical locations without retaining
            // those trees (includedFrom is deliberately not a physical location).
            if (locations && !in_string && input[at] == '"' && (input.substr(at, 5) == "\"loc\"" || input.substr(at, 7) == "\"range\"")) {
                auto key = string(); space();
                if (at < input.size() && input[at] == ':') {
                    ++at; space(); const auto start = at;
                    skip(false);
                    const auto raw = input.substr(start, at - start);
                    bool physical_file = false;
                    for (auto file = raw.find("\"file\""); file != std::string_view::npos; file = raw.find("\"file\"", file + 6)) {
                        const auto includer = raw.rfind("\"includedFrom\"", file);
                        if (includer == std::string_view::npos || raw.find('}', includer) < file) { physical_file = true; break; }
                    }
                    // Most SDK locations have only an elided filename plus
                    // includedFrom. Avoid allocating JSON for those millions of
                    // locations; only explicit physical file changes need it.
                    if (physical_file) {
                        const auto end = at; at = start; auto value = parse(0); at = end;
                        if (key == "range") { record_location(value["begin"]); record_location(value["end"]); }
                        else record_location(value);
                    }
                    continue;
                }
                continue;
            }
            char c = input[at++];
            if (in_string) { if (c == '\\') { if (at == input.size()) error(); ++at; } else if (c == '"') in_string = false; continue; }
            if (c == '"') in_string = true;
            else if (c == '[' || c == '{') brackets.push_back(c);
            else if (c == ']' || c == '}') {
                if (brackets.empty() || (c == ']' ? brackets.back() != '[' : brackets.back() != '{')) error();
                brackets.pop_back();
            }
        } while (!brackets.empty());
    }
    unsigned hex4() {
        unsigned n = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (at == input.size()) error();
            unsigned char c = static_cast<unsigned char>(input[at++]);
            if (!std::isxdigit(c)) error();
            n = n * 16 + (c <= '9' ? c - '0' : static_cast<unsigned>(std::tolower(c) - 'a' + 10));
        }
        return n;
    }
    static void utf8(std::string& out, unsigned n) {
        if (n < 0x80) out += static_cast<char>(n);
        else if (n < 0x800) { out += static_cast<char>(0xc0 | (n >> 6)); out += static_cast<char>(0x80 | (n & 63)); }
        else if (n < 0x10000) { out += static_cast<char>(0xe0 | (n >> 12)); out += static_cast<char>(0x80 | ((n >> 6) & 63)); out += static_cast<char>(0x80 | (n & 63)); }
        else { out += static_cast<char>(0xf0 | (n >> 18)); out += static_cast<char>(0x80 | ((n >> 12) & 63)); out += static_cast<char>(0x80 | ((n >> 6) & 63)); out += static_cast<char>(0x80 | (n & 63)); }
    }
    std::string string() {
        if (at == input.size() || input[at++] != '"') error();
        std::string out;
        while (at < input.size()) {
            char c = input[at++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 32) error();
            if (c != '\\') { out += c; continue; }
            if (at == input.size()) error();
            switch (input[at++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned code = hex4();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (at + 2 > input.size() || input.substr(at, 2) != "\\u") error();
                    at += 2; unsigned low = hex4();
                    if (low < 0xdc00 || low > 0xdfff) error();
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) error();
                utf8(out, code); break;
            }
            default: error();
            }
        }
        error();
    }
    Json parse(unsigned depth) {
        if (depth > 512) throw std::runtime_error("JSON nesting exceeds supported 512 levels");
        space(); if (at == input.size()) error();
        if (input[at] == '"') return Json{string()};
        if (input[at] == '{') {
            ++at; Json::Object object; std::string node_file; space();
            if (at < input.size() && input[at] == '}') { ++at; return Json{std::move(object)}; }
            for (;;) {
                space(); auto key = string(); space();
                if (at == input.size() || input[at++] != ':') error();
                // Project header ASTs out before allocating their enormous inner
                // trees. Clang still parses the complete TU with real includes.
                // Keep TU/namespace/linkage containers so user declarations can
                // be found even when headers open surrounding namespace scopes.
                auto kind_it = object.find("kind");
                auto ast_kind = kind_it == object.end() ? std::string{} : kind_it->second.text();
                const bool container = ast_kind == "TranslationUnitDecl" || ast_kind == "NamespaceDecl" || ast_kind == "LinkageSpecDecl";
                const bool header_inner = !ast_sources.empty() && !ast_file.empty() && !ast_sources.contains(ast_file) && key == "inner" && !ast_kind.empty() && !container;
                if (header_inner) skip();
                else {
                    const bool project_function = key == "inner" && ast_sources.contains(node_file) &&
                        (ast_kind == "FunctionDecl" || ast_kind == "CXXMethodDecl" || ast_kind == "CXXConversionDecl");
                    if (project_function) ++project_function_depth;
                    auto parsed = parse(depth + 1);
                    if (project_function) --project_function_depth;
                    if ((key == "loc" || key == "range") && !ast_sources.empty() && !ast_kind.empty()) {
                        record_location(key == "range" ? parsed["begin"] : parsed);
                        if (node_file.empty()) node_file = ast_file;
                        if (key == "range") record_location(parsed["end"]);
                    }
                    if (!object.emplace(std::move(key), std::move(parsed)).second) error();
                }
                space(); if (at == input.size()) error(); char next = input[at++];
                if (next == '}') {
                    if (!node_file.empty() && !ast_sources.contains(node_file)) {
                        auto found = object.find("kind");
                        const auto k = found == object.end() ? std::string{} : found->second.text();
                        const bool retained_container = k == "TranslationUnitDecl" || k == "NamespaceDecl" || k == "LinkageSpecDecl";
                        if (!k.empty() && !retained_container) {
                            // Dropping an excluded assignment/call from an authored
                            // function must not turn unknown effects into a proof.
                            if (project_function_depth) return Json{Json::Object{{"kind", Json{std::string("CppRailsOpaqueEffect")}}}};
                            return Json{};
                        }
                        if (retained_container && k != "TranslationUnitDecl") {
                            auto children = object.find("inner");
                            if (children == object.end() || children->second.array().empty()) return Json{};
                        }
                    }
                    if (!node_file.empty()) object.emplace("_cpprails_file", Json{std::move(node_file)});
                    return Json{std::move(object)};
                }
                if (next != ',') error();
            }
        }
        if (input[at] == '[') {
            ++at; Json::Array array; space();
            if (at < input.size() && input[at] == ']') { ++at; return Json{std::move(array)}; }
            for (;;) {
                auto value = parse(depth + 1);
                if (ast_sources.empty() || !std::holds_alternative<std::monostate>(value.value)) array.push_back(std::move(value));
                space(); if (at == input.size()) error();
                char next = input[at++]; if (next == ']') return Json{std::move(array)};
                if (next != ',') error();
            }
        }
        const auto start = at;
        while (at < input.size() && input[at] != ',' && input[at] != ']' && input[at] != '}' && !std::isspace(static_cast<unsigned char>(input[at]))) ++at;
        auto token = input.substr(start, at - start);
        if (token.empty()) error();
        if (token != "true" && token != "false" && token != "null") {
            std::size_t i = token.front() == '-' ? 1 : 0;
            if (i == token.size()) error();
            for (; i < token.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(token[i])) && token[i] != '.' && token[i] != 'e' && token[i] != 'E' && token[i] != '+' && token[i] != '-') error();
        }
        return Json{Json::Atom{std::string(token)}};
    }
public:
    explicit Parser(std::string_view text, const std::vector<fs::path>& sources = {}, const fs::path& cwd = {}) : input(text), ast_cwd(cwd) {
        for (const auto& source : sources) ast_sources.insert(path_key(source));
    }
    Json all() { auto result = parse(0); space(); if (at != input.size()) error(); return result; }
};
std::string quote(std::string_view value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) { constexpr char hex[] = "0123456789abcdef"; out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string fingerprint(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; }
    std::ostringstream out; out << "fnv1a64:" << std::hex << hash; return out.str();
}
std::string read(const fs::path& path, std::uintmax_t limit = max_json_bytes) {
    std::error_code error; auto size = fs::file_size(path, error);
    if (error) throw std::runtime_error("cannot read " + utf8_bytes(path.generic_u8string()));
    if (size > limit) throw std::runtime_error("complete capture exceeds explicit " + std::to_string(limit) + " byte input limit");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + utf8_bytes(path.generic_u8string()));
    std::string data(static_cast<std::size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) throw std::runtime_error("incomplete file read");
    return data;
}
fs::path absolute(const fs::path& path, const fs::path& base) { return fs::weakly_canonical(path.is_absolute() ? path : base / path); }
std::string lexical_path_key(const fs::path& path) {
    auto key = utf8_bytes(path.lexically_normal().generic_u8string());
#ifdef _WIN32
    std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}
bool same_path(const fs::path& left, const fs::path& right) {
    if (left == right) return true;
    std::error_code error;
    if (fs::equivalent(left, right, error)) return true;
    return left.lexically_normal() == right.lexically_normal();
}
struct Temp {
    fs::path directory;
    Temp() {
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned n = 0; n < 100; ++n) {
            directory = fs::temp_directory_path() / ("cpprails-clang-" + std::to_string(seed) + "-" + std::to_string(n));
            std::error_code error; if (fs::create_directory(directory, error)) return;
        }
        throw std::runtime_error("cannot create private AST capture directory");
    }
    ~Temp() { std::error_code error; fs::remove_all(directory, error); }
};
#ifdef _WIN32
std::wstring widen(const std::string& value) { return path_from_utf8(value).wstring(); }
std::wstring windows_arg(const std::string& text) {
    const auto value = widen(text); std::wstring out = L"\""; unsigned backslashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
        else out.append(backslashes, L'\\');
        backslashes = 0; out += c;
    }
    out.append(backslashes * 2, L'\\'); return out + L'"';
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
#endif
struct Captured { bool started = false; int code = -1; bool timed_out = false; };
Captured capture(const std::vector<std::string>& args, const fs::path& cwd, const fs::path& output, const fs::path& diagnostics) {
    Captured result;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle out(CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle err(CreateFileW(diagnostics.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (out.value == INVALID_HANDLE_VALUE || err.value == INVALID_HANDLE_VALUE || input.value == INVALID_HANDLE_VALUE) return result;
    std::wstring command;
    for (const auto& arg : args) { if (!command.empty()) command += L' '; command += windows_arg(arg); }
    std::vector<wchar_t> mutable_command(command.begin(), command.end()); mutable_command.push_back(0);
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = out.value; startup.hStdError = err.value; startup.hStdInput = input.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, cwd.c_str(), &startup, &process)) return result;
    Handle process_handle(process.hProcess); Handle thread_handle(process.hThread); result.started = true;
    if (WaitForSingleObject(process.hProcess, 60000) == WAIT_TIMEOUT) {
        result.timed_out = true; TerminateProcess(process.hProcess, 124); WaitForSingleObject(process.hProcess, INFINITE);
    }
    DWORD code = 0; if (GetExitCodeProcess(process.hProcess, &code)) result.code = static_cast<int>(code);
#else
    // Build argv before fork; the child only performs async-signal-safe calls.
    std::vector<char*> argv; for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str())); argv.push_back(nullptr);
    int out = open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    int err = open(diagnostics.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (out == -1 || err == -1) { if (out != -1) close(out); if (err != -1) close(err); return result; }
    const auto pid = fork();
    if (pid == 0) {
        if (chdir(cwd.c_str()) || dup2(out, STDOUT_FILENO) == -1 || dup2(err, STDERR_FILENO) == -1) _exit(126);
        close(out); close(err); execvp(argv[0], argv.data()); _exit(127);
    }
    close(out); close(err); if (pid == -1) return result;
    result.started = true; int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
        auto waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited == -1 && errno != EINTR) return result;
        if (std::chrono::steady_clock::now() > deadline) { result.timed_out = true; kill(pid, SIGKILL); while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {} break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (WIFEXITED(status)) result.code = WEXITSTATUS(status);
#endif
    return result;
}
// This is argument tokenization, never command execution. Reject shell syntax
// outside quotes instead of giving command strings accidental shell semantics.
bool cl_driver(std::string executable) {
    std::ranges::replace(executable, '\\', '/');
    executable = executable.substr(executable.find_last_of('/') + 1);
    std::ranges::transform(executable, executable.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return executable == "cl" || executable == "cl.exe" || executable == "clang-cl" || executable == "clang-cl.exe";
}
std::string cl_target(std::string executable) {
    std::ranges::replace(executable, '\\', '/');
    std::ranges::transform(executable, executable.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // A real MSVC compiler path encodes target architecture after Host<arch>.
    const auto host = executable.find("/bin/host");
    if (host == std::string::npos) return {};
    const auto start = executable.find('/', host + 5);
    if (start == std::string::npos) return {};
    const auto end = executable.find('/', start + 1);
    if (end == std::string::npos) return {};
    const auto arch = executable.substr(start + 1, end - start - 1);
    if (arch == "x64") return "x86_64-pc-windows-msvc";
    if (arch == "x86") return "i686-pc-windows-msvc";
    if (arch == "arm64") return "aarch64-pc-windows-msvc";
    if (arch == "arm64ec") return "arm64ec-pc-windows-msvc";
    throw std::runtime_error("unsupported MSVC compiler target directory: " + arch);
}
fs::path resolve_driver(const std::string& executable, const fs::path& cwd) {
    const auto named = path_from_utf8(executable);
    auto candidate = [&](const fs::path& base) -> fs::path {
        auto path = base / named;
#ifdef _WIN32
        if (path.extension().empty()) path += L".exe";
#endif
        return fs::is_regular_file(path) ? absolute(path, cwd) : fs::path{};
    };
    if (named.has_parent_path()) { if (auto found = candidate(cwd); !found.empty()) return found; }
    else {
        if (auto found = candidate(cwd); !found.empty()) return found;
#ifdef _WIN32
        const auto size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        std::wstring paths(size, L'\0');
        if (size) { GetEnvironmentVariableW(L"PATH", paths.data(), size); paths.resize(size - 1); }
        std::wistringstream entries(paths); std::wstring entry;
        while (std::getline(entries, entry, L';')) {
            if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"') entry = entry.substr(1, entry.size() - 2);
            if (auto found = candidate(entry.empty() ? cwd : fs::path(entry)); !found.empty()) return found;
        }
#else
        if (const auto* paths = std::getenv("PATH")) {
            std::istringstream entries(paths); std::string entry;
            while (std::getline(entries, entry, ':')) if (auto found = candidate(entry.empty() ? cwd : path_from_utf8(entry)); !found.empty()) return found;
        }
#endif
    }
    throw std::runtime_error("original MSVC driver cannot be resolved; provide explicit target and MSVC compatibility version: " + executable);
}
std::string environment_value(const std::string& name) {
#ifdef _WIN32
    const std::wstring key(name.begin(), name.end());
    const auto size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (!size) return {};
    std::wstring value(size, L'\0'); GetEnvironmentVariableW(key.c_str(), value.data(), size); value.resize(size - 1);
    return utf8_bytes(fs::path(value).u8string());
#else
    const auto* value = std::getenv(name.c_str()); return value ? value : "";
#endif
}
std::pair<std::string, std::string> cl_context(const std::string& executable, const fs::path& cwd, const std::string& backend) {
    const auto driver = resolve_driver(executable, cwd);
    auto name = utf8_bytes(driver.filename().u8string());
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "cl.exe" || name == "cl") {
        auto target = cl_target(utf8_bytes(driver.u8string()));
        return {target, msvc_compiler_version(driver)};
    }
    // A compilation database may name arbitrary repo-local executables. Only
    // the user-selected backend is trusted to execute; other clang-cl identities
    // need explicit context rather than an executable probe from the database.
    if (!path_from_utf8(backend).is_absolute() || !same_path(driver, path_from_utf8(backend))) throw std::runtime_error("original clang-cl is not the selected absolute trusted backend; provide explicit target and MSVC compatibility version");
    Temp temp;
    const auto output = temp.directory / "driver-version.txt", errors = temp.directory / "driver-errors.txt";
    auto run = capture({utf8_bytes(driver.u8string()), "--no-default-config", "--version"}, cwd, output, errors);
    if (!run.started || run.timed_out || run.code) throw std::runtime_error("original clang-cl identity is unavailable");
    const auto identity = read(output, 64 * 1024);
    auto start = identity.find("Target: ");
    if (start == std::string::npos || identity.find("clang version") == std::string::npos) throw std::runtime_error("original clang-cl did not identify its target");
    start += 8;
    const auto target = identity.substr(start, identity.find_first_of("\r\n ", start) - start);
    if (target != "x86_64-pc-windows-msvc" && target != "i686-pc-windows-msvc" && target != "aarch64-pc-windows-msvc" && target != "arm64ec-pc-windows-msvc") throw std::runtime_error("unsupported original clang-cl target: " + target);
    const auto probe = temp.directory / "predefines.cpp";
    { std::ofstream file(probe); file << "// Compiler predefined identity; no project source or headers.\n"; }
    const auto macros = temp.directory / "macros.txt", macro_errors = temp.directory / "macro-errors.txt";
    run = capture({utf8_bytes(driver.u8string()), "--no-default-config", "/nologo", "/E", "/clang:-dM", "/TP", utf8_bytes(probe.u8string())}, cwd, macros, macro_errors);
    if (!run.started || run.timed_out || run.code) throw std::runtime_error("original clang-cl MSVC compatibility predefines are unavailable");
    const auto defines = read(macros, 1024 * 1024);
    auto value = [&](const std::string& macro) {
        auto at = defines.find("#define " + macro + " ");
        if (at == std::string::npos) throw std::runtime_error("missing original clang-cl predefined identity: " + macro);
        return std::stoull(defines.substr(at + macro.size() + 9));
    };
    const auto version = value("_MSC_VER"), full = value("_MSC_FULL_VER");
    if (version < 1900 || full / 100000 != version) throw std::runtime_error("unsupported original clang-cl MSVC version representation");
    return {target, std::to_string(version / 100) + "." + std::to_string(version % 100) + "." + std::to_string(full % 100000)};
}
std::vector<std::string> split_windows_command(const std::string& command) {
    std::vector<std::string> result;
    std::size_t i = 0;
    while (i < command.size()) {
        while (i < command.size() && (command[i] == ' ' || command[i] == '\t')) ++i;
        if (i == command.size()) break;
        std::string token; bool quoted = false;
        while (i < command.size()) {
            if (!quoted && (command[i] == ' ' || command[i] == '\t')) break;
            std::size_t slashes = 0;
            while (i < command.size() && command[i] == '\\') { ++slashes; ++i; }
            if (i < command.size() && command[i] == '"') {
                token.append(slashes / 2, '\\');
                if (slashes % 2) token += '"';
                else if (quoted && i + 1 < command.size() && command[i + 1] == '"') { token += '"'; ++i; }
                else quoted = !quoted;
                ++i; continue;
            }
            token.append(slashes, '\\');
            if (i == command.size()) break;
            const char c = command[i];
            if (!quoted && (c == ' ' || c == '\t')) break;
            if (c == '\n' || c == '\r' || (!quoted && (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '`' || c == '$')))
                throw std::runtime_error("shell syntax in compilation command is unsupported; provide arguments[]");
            token += c; ++i;
        }
        if (quoted) throw std::runtime_error("unterminated compilation command quote");
        result.push_back(std::move(token));
    }
    return result;
}
std::vector<std::string> split_command(const std::string& command) {
#ifdef _WIN32
    return split_windows_command(command);
#else
    // MSVC databases retain Windows CRT quoting even when inspected elsewhere.
    if (auto windows = split_windows_command(command); !windows.empty() && cl_driver(windows.front())) return windows;
    std::vector<std::string> result; std::string part; char quoted = 0; bool active = false;
    for (std::size_t i = 0; i < command.size(); ++i) {
        char c = command[i];
        if (c == '\\' && i + 1 < command.size() && (command[i + 1] == '"' || command[i + 1] == '\\' || (!quoted && std::isspace(static_cast<unsigned char>(command[i + 1]))))) { part += command[++i]; active = true; continue; }
        if (c == '\'' || c == '"') {
            if (quoted == c) quoted = 0;
            else if (!quoted) quoted = c;
            else part += c;
            active = true; continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(c))) { if (active) { result.push_back(std::move(part)); part.clear(); active = false; } continue; }
        if (!quoted && (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '`' || c == '$' || c == '\n')) throw std::runtime_error("shell syntax in compilation command is unsupported; provide arguments[]");
        part += c; active = true;
    }
    if (quoted) throw std::runtime_error("unterminated compilation command quote");
    if (active) result.push_back(std::move(part));
    return result;
#endif
}
struct Unit { fs::path source, cwd; std::vector<std::string> args; };
std::string command_digest(const Unit& unit) {
    std::string command = utf8_bytes(unit.cwd.u8string()); command += '\0';
    for (const auto& argument : unit.args) { command += argument; command += '\0'; }
    return fingerprint(command);
}
std::vector<Unit> units(const Json& database, const fs::path& database_dir) {
    if (!std::holds_alternative<Json::Array>(database.value)) throw std::runtime_error("compilation database must be an array");
    std::vector<Unit> result;
    for (const auto& entry : database.array()) {
        if (!std::holds_alternative<std::string>(entry["file"].value) || !std::holds_alternative<std::string>(entry["directory"].value) || entry["file"].text().empty() || entry["directory"].text().empty() || entry["file"].text().find('\0') != std::string::npos || entry["directory"].text().find('\0') != std::string::npos) throw std::runtime_error("compilation entry requires string file and directory without NUL bytes");
        Unit unit; unit.cwd = absolute(path_from_utf8(entry["directory"].text()), database_dir);
        unit.source = absolute(path_from_utf8(entry["file"].text()), unit.cwd);
        if (entry.has("arguments")) {
            if (!std::holds_alternative<Json::Array>(entry["arguments"].value)) throw std::runtime_error("arguments must be an array");
            for (const auto& argument : entry["arguments"].array()) {
                if (!std::holds_alternative<std::string>(argument.value)) throw std::runtime_error("arguments must contain strings");
                unit.args.push_back(argument.text());
            }
        } else {
            if (!std::holds_alternative<std::string>(entry["command"].value)) throw std::runtime_error("compilation command must be a string");
            unit.args = split_command(entry["command"].text());
        }
        if (unit.args.empty()) throw std::runtime_error("empty compilation command");
        for (const auto& argument : unit.args) if (argument.find('\0') != std::string::npos) throw std::runtime_error("compilation argument contains NUL byte");
        result.push_back(std::move(unit));
    }
    return result;
}
std::vector<std::string> compiler_args(const Unit& unit, const Options& options) {
    if (options.clang_executable.empty() || options.clang_executable.find('\0') != std::string::npos) throw std::runtime_error("compiler executable is empty or contains NUL byte");
    std::vector<std::string> args{options.clang_executable, "--no-default-config"}; bool source_found = false;
    const bool msvc = cl_driver(unit.args.front());
    if (msvc) {
        for (const auto name : {"CL", "_CL_", "CCC_OVERRIDE_OPTIONS"}) if (!environment_value(name).empty())
            throw std::runtime_error(std::string("hidden compiler environment options are unsupported: ") + name);
        args.push_back("--driver-mode=cl");
        const bool explicit_target = std::ranges::any_of(unit.args, [](const auto& value) { return value.starts_with("/clang:--target=") || value.starts_with("-clang:--target="); });
        const bool explicit_version = std::ranges::any_of(unit.args, [](const auto& value) { return value.starts_with("/clang:-fms-compatibility-version=") || value.starts_with("-clang:-fms-compatibility-version="); });
        if (!explicit_target || !explicit_version) {
            const auto [target, version] = cl_context(unit.args.front(), unit.cwd, options.clang_executable);
            if (!explicit_target && target.empty()) throw std::runtime_error("original cl.exe target architecture is unknown; provide an explicit target");
            if (!explicit_target) args.push_back("/clang:--target=" + target);
            if (!explicit_version) args.push_back("/clang:-fms-compatibility-version=" + version);
        }
        auto original = unit.args.front(); std::ranges::replace(original, '\\', '/'); original = original.substr(original.find_last_of('/') + 1);
        std::ranges::transform(original, original.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (original == "cl" || original == "cl.exe") {
            // cl.exe uses INCLUDE, not Clang's independently selected latest
            // MSVC/SDK defaults. Replay those actual environment search paths.
            const bool no_environment_includes = std::ranges::any_of(unit.args, [](const auto& value) { return value == "/X" || value == "-X"; });
            if (!no_environment_includes) {
                args.push_back("/X");
                std::istringstream includes(environment_value("INCLUDE")); std::string path;
                while (std::getline(includes, path, ';')) if (!path.empty()) args.push_back("/imsvc" + path);
            }
        }
    }
    for (std::size_t i = 1; i < unit.args.size(); ++i) {
        auto arg = unit.args[i];
        if (msvc) {
            // Closed replay vocabulary: ignored/unknown clang-cl switches must
            // never turn a changed target context into a successful AST gate.
            if (arg.empty()) throw std::runtime_error("empty MSVC argument");
            // cl accepts '-' as the spelling of '/' switches. CMake uses it
            // for -I, -std:, -MD and -c; -MD here selects the DLL CRT.
            if (arg.front() == '-' && !arg.starts_with("--")) arg.front() = '/';
            if (arg == utf8_bytes(unit.source.u8string()) || ((arg.front() != '/' && arg.front() != '-') && same_path(absolute(path_from_utf8(arg), unit.cwd), unit.source))) { args.push_back(arg); source_found = true; continue; }
            if (arg == "/c" || arg == "/showIncludes" || arg == "/showIncludes:user" || arg == "/FC" || arg == "/FS" || arg == "/MP" || arg == "/Zi" || arg == "/ZI" || arg == "/Z7") continue;
            bool handled = false;
            for (const auto prefix : {"/Fo", "/Fe", "/Fd", "/Fa", "/Fi", "/sourceDependencies", "/scanDependencies"}) {
                if (arg.starts_with(prefix)) {
                    if (arg == prefix && ++i == unit.args.size()) throw std::runtime_error("missing MSVC output argument: " + arg);
                    handled = true; break;
                }
            }
            if (handled) continue;
            for (const auto prefix : {"/external:I", "/imsvc", "/FI", "/I", "/D", "/U"}) {
                if (arg.starts_with(prefix)) {
                    args.push_back(arg);
                    if (arg == prefix) {
                        if (++i == unit.args.size() || unit.args[i].empty() || unit.args[i].starts_with('@')) throw std::runtime_error("missing/unsupported MSVC option argument: " + arg);
                        args.push_back(unit.args[i]);
                    }
                    handled = true; break;
                }
            }
            if (handled) continue;
            static const std::set<std::string> supported{
                "/nologo", "/TP", "/TC", "/X", "/MD", "/MDd", "/MT", "/MTd", "/EHsc", "/EHs", "/EHa", "/EHs-c-", "/EHsc-", "/GR", "/GR-",
                "/std:c++14", "/std:c++17", "/std:c++20", "/std:c++23", "/std:c++23preview", "/std:c++latest", "/std:c11", "/std:c17",
                "/permissive-", "/Zc:__cplusplus", "/Zc:wchar_t", "/Zc:wchar_t-", "/Zc:forScope", "/Zc:forScope-", "/Zc:threadSafeInit", "/Zc:threadSafeInit-",
                "/Zc:alignedNew", "/Zc:alignedNew-", "/Zc:sizedDealloc", "/Zc:sizedDealloc-", "/Zc:dllexportInlines-", "/Zc:inline", "/Zc:inline-", "/sdl", "/sdl-",
                "/Od", "/O1", "/O2", "/Ox", "/Ob0", "/Ob1", "/Ob2", "/Ob3", "/Oi", "/Oi-", "/Ot", "/Os", "/Oy", "/Oy-", "/GF", "/GF-", "/GS", "/GS-", "/Gy", "/Gy-", "/Gw", "/Gw-",
                "/fp:precise", "/fp:strict", "/fp:fast", "/Gd", "/Gr", "/Gz", "/Gv", "/J", "/utf-8", "/validate-charset", "/validate-charset-",
                "/Zp1", "/Zp2", "/Zp4", "/Zp8", "/Zp16", "/arch:IA32", "/arch:SSE", "/arch:SSE2", "/arch:AVX", "/arch:AVX2", "/arch:AVX512",
                "/W0", "/W1", "/W2", "/W3", "/W4", "/Wall", "/WX", "/WX-"};
            const bool warning = (arg.starts_with("/wd") || arg.starts_with("/we") || arg.starts_with("/wo")) && arg.size() > 3 && std::ranges::all_of(arg.substr(3), [](unsigned char c) { return std::isdigit(c); });
            const std::string compatibility = "/clang:-fms-compatibility-version=";
            const bool version = arg.starts_with(compatibility) && arg.size() > compatibility.size() && std::ranges::all_of(arg.substr(compatibility.size()), [](unsigned char c) { return std::isdigit(c) || c == '.'; });
            const bool target = arg == "/clang:--target=x86_64-pc-windows-msvc" || arg == "/clang:--target=i686-pc-windows-msvc" || arg == "/clang:--target=aarch64-pc-windows-msvc" || arg == "/clang:--target=arm64ec-pc-windows-msvc";
            if (supported.contains(arg) || warning || version || target) { args.push_back(arg); continue; }
            throw std::runtime_error("unsupported MSVC semantic argument: " + arg);
        }
        if (arg == "--" || arg.starts_with("--config")) throw std::runtime_error("unsupported argument/config boundary: " + arg);
        if (arg == "-c" || arg == "-S" || arg == "-E" || arg == "-MD" || arg == "-MMD" || arg == "-MP" || arg == "/c") continue;
        if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ" || arg == "-MJ" || arg == "--serialize-diagnostics" || arg == "-serialize-diagnostics") { if (++i == unit.args.size()) throw std::runtime_error("missing compilation option argument"); continue; }
        if ((arg.starts_with("-o") && arg.size() > 2) || arg.starts_with("-MF") || arg.starts_with("-MJ") || arg.starts_with("/Fo") || arg.starts_with("/Fe")) continue;
        // Plugins, response files and alternate frontend output actions are not
        // replayed. They obscure both the actual TU and bounded capture contract.
        if (arg.starts_with('@') || arg.starts_with("-Xclang") || arg.starts_with("-cc1") || arg.starts_with("-fplugin") || arg.starts_with("-fpass-plugin") || arg == "-load" || arg == "-wrapper" || arg.starts_with("-save-temps") || arg.starts_with("-ftime-trace") || arg.starts_with("-fmodule-output") || arg.starts_with("-Wp,") || arg == "-Xpreprocessor" || arg == "-dependency-file" || arg == "-emit-ast" || arg == "--analyze" || arg == "--precompile" || arg == "-rewrite-objc" || arg == "-extract-api") throw std::runtime_error("unsupported frontend/plugin/response/action argument: " + arg);
        if (!arg.empty() && arg[0] != '-' && same_path(absolute(path_from_utf8(arg), unit.cwd), unit.source)) source_found = true;
        args.push_back(arg);
    }
    if (!source_found) args.push_back(utf8_bytes(unit.source.u8string()));
    if (msvc) args.insert(args.end(), {"/clang:-Werror=unknown-argument", "/clang:-Werror=unused-command-line-argument", "/clang:-fsyntax-only", "/clang:-fno-color-diagnostics", "/clang:-Xclang", "/clang:-ast-dump=json"});
    else args.insert(args.end(), {"-fsyntax-only", "-fno-color-diagnostics", "-Xclang", "-ast-dump=json"});
    return args;
}

std::string kind(const Json& node) { return node["kind"].text(); }
bool complex_flow(const std::string& value) {
    static const std::set<std::string> kinds{
        "IfStmt", "ForStmt", "CXXForRangeStmt", "WhileStmt", "DoStmt", "SwitchStmt", "CXXTryStmt", "CXXThrowExpr",
        "GotoStmt", "IndirectGotoStmt", "LabelStmt", "ConditionalOperator", "BinaryConditionalOperator",
        "CoroutineBodyStmt", "CoreturnStmt", "CoawaitExpr", "CoyieldExpr", "SEHTryStmt", "SEHLeaveStmt"};
    return kinds.contains(value);
}
std::string type(const Json& node) { auto value = node["type"]["desugaredQualType"].text(); return value.empty() ? node["type"]["qualType"].text() : value; }
bool std_type(const std::string& value, const std::string& name) {
    std::string_view outer(value);
    for (;;) {
        bool stripped = false;
        for (const auto qualifier : {"const ", "volatile ", "class ", "struct "}) if (outer.starts_with(qualifier)) { outer.remove_prefix(std::string_view(qualifier).size()); stripped = true; break; }
        if (!stripped) break;
    }
    const auto opening = outer.find('<');
    const auto spelling_end = opening == std::string_view::npos ? outer.find_first_of(" \t&*[") : opening;
    auto prefix = outer.substr(0, spelling_end);
    if (!prefix.starts_with("std::")) return false;
    prefix.remove_prefix(5);
    // Recognize the standard libraries' reserved implementation namespaces,
    // while requiring the outer template itself, not one of its arguments.
    while (prefix.starts_with("__")) {
        auto next = prefix.find("::"); if (next == std::string_view::npos) break;
        prefix.remove_prefix(next + 2);
    }
    if (opening == std::string_view::npos) {
        // Apple Clang may preserve these standard aliases in expression types
        // without providing desugaredQualType. Match whole qualified names,
        // retaining the same pointer/array exclusions as template spellings.
        if (outer.find('*') != std::string_view::npos || outer.find('[') != std::string_view::npos) return false;
        if (name == "basic_string")
            return prefix == "string" || prefix == "wstring" || prefix == "u8string" || prefix == "u16string" || prefix == "u32string";
        if (name == "basic_string_view")
            return prefix == "string_view" || prefix == "wstring_view" || prefix == "u8string_view" || prefix == "u16string_view" || prefix == "u32string_view";
        return false;
    }
    if (prefix != name) return false;
    unsigned depth = 0; std::size_t closing = std::string_view::npos;
    for (std::size_t i = opening; i < outer.size(); ++i) {
        if (outer[i] == '<') ++depth;
        else if (outer[i] == '>' && --depth == 0) { closing = i; break; }
    }
    if (closing == std::string_view::npos) return false;
    auto suffix = outer.substr(closing + 1);
    return suffix.find('*') == std::string_view::npos && suffix.find('[') == std::string_view::npos;
}
bool view_type(const std::string& value) { return std_type(value, "basic_string_view") || std_type(value, "span"); }
bool owner_type(const std::string& value) {
    if (std_type(value, "basic_string") || std_type(value, "vector") || std_type(value, "array")) return true;
    int depth = 0;
    for (char c : value) { if (c == '<') ++depth; else if (c == '>') --depth; else if (c == '[' && depth == 0) return true; }
    return false;
}
bool standard_string_view_conversion(const Json& node) {
    if (kind(node) != "CXXMemberCallExpr" || !std_type(type(node), "basic_string_view")) return false;
    const auto& call = node["inner"].array();
    if (call.empty() || kind(call[0]) != "MemberExpr" || !call[0]["name"].text().starts_with("operator ")) return false;
    const auto& base = call[0]["inner"].array();
    return base.size() == 1 && std_type(type(base[0]), "basic_string");
}
const Json& location(const Json& node) {
    auto& loc = node.has("loc") ? node["loc"] : node["range"]["begin"];
    return loc.has("expansionLoc") ? loc["expansionLoc"] : loc;
}
std::size_t number(const Json& value) { try { return static_cast<std::size_t>(std::stoull(value.text())); } catch (...) { return 0; } }
std::size_t line_of(const Json& node, const std::string& source) {
    const auto& loc = location(node);
    if (loc.has("line")) return number(loc["line"]);
    if (loc.has("offset")) {
        auto offset = number(loc["offset"]);
        if (offset <= source.size()) return 1 + static_cast<std::size_t>(std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
    }
    return 0;
}
std::string reference(const Json& node) {
    if (kind(node) == "DeclRefExpr") return node["referencedDecl"]["id"].text();
    const auto& inner = node["inner"].array();
    if (inner.size() == 1 && (kind(node) == "ImplicitCastExpr" || kind(node) == "ParenExpr" || kind(node) == "ExprWithCleanups")) return reference(inner.front());
    return {};
}
struct Stats {
    std::size_t nodes = 0, functions = 0, unknown = 0, acquisitions = 0, releases = 0, invalidations = 0, unsupported_flow = 0, aliases = 0, analyzed = 0, selected = 0, uncovered = 0;
    bool exhausted = false;
    std::size_t requested_fragments = 0, observed_fragments = 0, uncovered_fragments = 0;
    std::vector<std::string> function_summaries;
};
struct Analysis {
    const Options& options;
    const fs::path& root;
    const Unit& unit;
    Report& report;
    Stats& stats;
    std::string source;
    const std::map<fs::path, std::string>& project_sources;
    std::map<std::string, std::pair<fs::path, const std::string*>> source_lookup;
    std::set<fs::path>& observed_files;
    fs::path function_file;
    std::string function_file_key;
    std::map<std::string, std::string> owners, views;
    std::string function_name;
    std::string function_type;
    std::string compiler_version;
    std::string replay_digest;
    std::string source_fingerprint;
    std::vector<std::string> borrow_relations;
    std::map<std::string, unsigned> finding_occurrences;
    Analysis(const Options& opt, const fs::path& base, const Unit& translation_unit,
             Report& output, Stats& counts, std::string content, std::string version,
             const std::map<fs::path, std::string>& sources, std::set<fs::path>& observed, std::string replay)
        : options(opt), root(base), unit(translation_unit), report(output), stats(counts),
          source(std::move(content)), project_sources(sources), observed_files(observed), function_file(translation_unit.source),
          compiler_version(std::move(version)), replay_digest(std::move(replay)), source_fingerprint(fingerprint(source)) {
        for (const auto& [path, text] : project_sources) source_lookup.emplace(lexical_path_key(path), std::make_pair(path, &text));
        function_file_key = lexical_path_key(function_file);
    }
    const std::pair<fs::path, const std::string*>* source_entry(const Json& node) const {
        auto value = node["_cpprails_file"].text();
        auto found = source_lookup.find(value.empty() ? function_file_key : value);
        return found == source_lookup.end() ? nullptr : &found->second;
    }
    const std::string& node_source(const Json& node) const {
        const auto* entry = source_entry(node);
        return entry ? *entry->second : source;
    }
    std::size_t line(const Json& node) const { return line_of(node, node_source(node)); }
    bool spend() {
        if (options.analysis_budget && stats.nodes >= options.analysis_budget) { stats.exhausted = true; return false; }
        ++stats.nodes; return true;
    }
    void finding(const std::string& rule, const Json& node, const std::string& message, const std::string& evidence) {
        Finding finding;
        const auto* entry = source_entry(node);
        if (!entry) return;
        finding.rule = rule; finding.path = relative_path(entry->first, root); finding.line = line(node);
        finding.severity = "P2"; finding.confidence = "confirmed-structure";
        finding.message = message; finding.evidence = function_name + " [" + function_type + "]: " + evidence;
        finding.recommendation = "Review the named intraprocedural lifetime/release contract and add an execution regression; AST structure is not a whole-program safety proof.";
        finding.analyzer = "clang-ast"; finding.evidence_level = "verified-structure";
        finding.scope = is_test_path(finding.path) ? "test" : "production";
        const auto key = rule + '\n' + finding.path + '\n' + finding.evidence;
        finding.id = make_id(rule, finding.path, finding.evidence, finding_occurrences[key]++);
        if (std::ranges::none_of(report.findings, [&](const auto& old) { return old.id == finding.id; })) report.findings.push_back(std::move(finding));
    }
    std::string borrowed_owner(const Json& node) {
        auto id = reference(node);
        if (owners.contains(id)) return id;
        if (auto it = views.find(id); it != views.end()) return it->second;
        const auto k = kind(node); const auto& children = node["inner"].array();
        if (k == "CXXMemberCallExpr") {
            if (children.empty() || kind(children[0]) != "MemberExpr") return {};
            auto member = children[0]["name"].text();
            if (member != "data" && member != "c_str" && !standard_string_view_conversion(node)) return {};
            const auto& base = children[0]["inner"].array(); return base.size() == 1 ? borrowed_owner(base[0]) : std::string{};
        }
        if (k == "CXXConstructExpr" || k == "CXXTemporaryObjectExpr" || k == "CXXFunctionalCastExpr" || k == "ImplicitCastExpr" || k == "ExprWithCleanups" || k == "MaterializeTemporaryExpr" || k == "CXXBindTemporaryExpr" || k == "ParenExpr") {
            for (const auto& child : children) { auto found = borrowed_owner(child); if (!found.empty()) return found; }
        }
        return {};
    }
    void visit_body(const Json& node) {
        if (!spend()) return;
        const auto* requested = source_entry(node);
        if (requested && (node.has("loc") || node.has("range"))) observed_files.insert(requested->first);
        auto k = kind(node);
        if (k == "CppRailsOpaqueEffect") { ++stats.unknown; views.clear(); return; }
        if (k == "LambdaExpr" || k == "CXXRecordDecl" || k == "FunctionDecl") { ++stats.unknown; return; }
        if (k == "CXXNewExpr") ++stats.acquisitions;
        if (k == "CXXDeleteExpr") ++stats.releases;
        if (complex_flow(k)) ++stats.unsupported_flow;
        if (k == "VarDecl" && node["storageClass"].text() != "static" && node["storageClass"].text() != "extern" && !node.has("tls")) {
            auto id = node["id"].text(); auto value_type = type(node);
            if (owner_type(value_type) && value_type.find('&') == std::string::npos && value_type.find('*') == std::string::npos) owners[id] = node["name"].text();
            if (view_type(value_type)) for (const auto& child : node["inner"].array()) {
                auto owner = borrowed_owner(child);
                if (!owner.empty()) {
                    views[id] = owner;
                    borrow_relations.push_back("{\"borrow\":" + quote(node["name"].text()) + ",\"owner\":" + quote(owners[owner]) + ",\"line\":" + std::to_string(line(node)) + ",\"scope\":\"initializer-structure\"}");
                }
            }
            if ((value_type.find('*') != std::string::npos || value_type.find('&') != std::string::npos) && !descendant(node, "CXXNewExpr")) ++stats.aliases;
        }
        if (k == "ReturnStmt") {
            for (const auto& child : node["inner"].array()) if (view_type(type(child))) {
                auto owner = borrowed_owner(child);
                if (!owner.empty()) {
                    finding("SEM-DANGLING-VIEW", node, "Returned standard view borrows local automatic storage", "return expression of type " + type(child) + " refers to local owner " + owners[owner]);
                    borrow_relations.push_back("{\"borrow\":\"return\",\"owner\":" + quote(owners[owner]) + ",\"line\":" + std::to_string(line(node)) + ",\"scope\":\"return-expression-structure\"}");
                }
            }
        }
        // Assignments and opaque effects invalidate the alias model. A later
        // direct return of an automatic owner can still be checked structurally.
        if (k == "BinaryOperator" || k == "CompoundAssignOperator") { ++stats.aliases; views.clear(); }
        if (k == "CallExpr" || k == "CXXOperatorCallExpr") { ++stats.unknown; views.clear(); }
        if (k == "CXXConstructExpr" || k == "CXXTemporaryObjectExpr") {
            if (!view_type(type(node))) ++stats.unknown;
            if (!view_type(type(node)) && !owner_type(type(node))) views.clear();
        }
        if (k == "CXXMemberCallExpr") {
            const auto& call = node["inner"].array();
            auto member = call.empty() ? std::string{} : call[0]["name"].text();
            if (member != "data" && member != "c_str" && !standard_string_view_conversion(node)) { ++stats.unknown; views.clear(); }
        }
        for (const auto& child : node["inner"].array()) visit_body(child);
    }
    const Json* descendant(const Json& node, const std::string& wanted) {
        if (kind(node) == wanted) return &node;
        for (const auto& child : node["inner"].array()) if (auto result = descendant(child, wanted)) return result;
        return nullptr;
    }
    void ownership(const Json& body) {
        // Only straight-line function bodies with no opaque call, alias, mutation
        // or control-flow construct qualify. Unsupported effects reset coverage.
        struct Allocation { bool array = false, released = false; std::string name; };
        std::unordered_map<std::string, Allocation> live;
        const auto& statements = body["inner"].array();
        bool supported = true;
        std::function<void(const Json&)> check = [&](const Json& n) {
            if (!spend()) { supported = false; return; }
            const auto k = kind(n);
            if (complex_flow(k) || k == "CppRailsOpaqueEffect" || k == "LambdaExpr" || k == "BinaryOperator" || k == "CompoundAssignOperator" || k == "CallExpr" || k == "CXXMemberCallExpr" || k == "CXXOperatorCallExpr" || k == "CXXConstructExpr" || k == "CXXTemporaryObjectExpr" || k == "ReturnStmt") supported = false;
            for (const auto& child : n["inner"].array()) check(child);
        };
        check(body);
        if (!supported) return;
        for (const auto& statement : statements) {
            auto k = kind(statement);
            if (k == "DeclStmt") {
                for (const auto& decl : statement["inner"].array()) {
                    if (kind(decl) != "VarDecl") continue;
                    if (const auto* allocation = descendant(decl, "CXXNewExpr")) {
                        static const std::set<std::string> primitive_pointers{
                            "bool *", "char *", "signed char *", "unsigned char *", "wchar_t *", "char8_t *", "char16_t *", "char32_t *",
                            "short *", "unsigned short *", "int *", "unsigned int *", "long *", "unsigned long *", "long long *", "unsigned long long *", "float *", "double *", "long double *"};
                        // A user destructor can mutate escaped pointer aliases;
                        // its effects are outside this straight-line contract.
                        if (!primitive_pointers.contains(type(*allocation))) { live.clear(); continue; }
                        live[decl["id"].text()] = {(*allocation)["isArray"].text() == "true", false, decl["name"].text()};
                    } else if (type(decl).find('*') != std::string::npos || type(decl).find('&') != std::string::npos) {
                        // Even a simple alias can change which variable is released.
                        live.clear();
                    }
                }
            } else if (k == "CXXDeleteExpr") {
                const auto& children = statement["inner"].array();
                auto id = children.size() == 1 ? reference(children[0]) : std::string{};
                auto it = live.find(id); if (it == live.end()) continue;
                if (it->second.released) finding("SEM-DOUBLE-DELETE", statement, "Repeated delete of the same local allocation in a straight-line body", "local pointer " + it->second.name + " has two releases without reassignment");
                else if (it->second.array != (statement["isArray"].text() == "true")) finding("SEM-DELETE-MISMATCH", statement, "Array/scalar allocation and delete forms disagree", "local pointer " + it->second.name + (it->second.array ? " acquired with array new and released with scalar delete" : " acquired with scalar new and released with array delete"));
                it->second.released = true;
            } else { live.clear(); }
        }
    }
    void invalidation(const Json& body) {
        const auto saved_views = views;
        views.clear();
        std::set<std::string> invalidated;
        bool supported = true;
        std::function<void(const Json&)> scan = [&](const Json& node) {
            if (!spend()) { supported = false; return; }
            const auto k = kind(node);
            if (complex_flow(k) || k == "LambdaExpr" || k == "CallExpr" || k == "BinaryOperator" || k == "CompoundAssignOperator") supported = false;
            for (const auto& child : node["inner"].array()) scan(child);
        };
        scan(body);
        if (!supported) { views = saved_views; return; }
        std::function<void(const Json&)> walk = [&](const Json& node) {
            if (!spend()) return;
            const auto k = kind(node); const auto& children = node["inner"].array();
            if (k == "CppRailsOpaqueEffect") { views.clear(); invalidated.clear(); return; }
            // Argument/index effects happen before the call. The projection may
            // hide those effects, so pre-order recognition cannot use old state.
            if ((k == "CXXOperatorCallExpr" || k == "CXXMemberCallExpr") && descendant(node, "CppRailsOpaqueEffect")) { views.clear(); invalidated.clear(); }
            if ((k == "CXXConstructExpr" || k == "CXXTemporaryObjectExpr") && !view_type(type(node)) && !owner_type(type(node))) { views.clear(); invalidated.clear(); }
            if (k == "VarDecl" && view_type(type(node))) {
                for (const auto& child : children) { auto owner = borrowed_owner(child); if (!owner.empty()) views[node["id"].text()] = owner; }
            }
            if (k == "CXXMemberCallExpr") {
                auto member = children.empty() ? std::string{} : children[0]["name"].text();
                const auto& base = children.empty() ? Json{}.array() : children[0]["inner"].array();
                auto owner = base.size() == 1 ? reference(base[0]) : std::string{};
                if (member == "clear" && owners.contains(owner) && !base.empty() && std_type(type(base[0]), "vector")) { invalidated.insert(owner); ++stats.invalidations; }
                else if (member != "data" && member != "c_str" && !standard_string_view_conversion(node)) { views.clear(); invalidated.clear(); }
            }
            if (k == "CXXOperatorCallExpr") {
                auto name = children.empty() ? std::string{} : children[0]["inner"].array().empty() ? std::string{} : children[0]["inner"].array()[0]["referencedDecl"]["name"].text();
                auto view = children.size() > 1 ? reference(children[1]) : std::string{};
                auto alias = views.find(view);
                if (name == "operator[]" && alias != views.end() && invalidated.contains(alias->second)) finding("SEM-INVALIDATED-VIEW", node, "Standard view element access follows clear of its local vector owner", "view borrows local vector " + owners[alias->second] + "; vector::clear ends element lifetimes before operator[]");
                else if (name != "operator[]") { views.clear(); invalidated.clear(); }
            }
            for (const auto& child : children) walk(child);
        };
        walk(body);
        views = saved_views;
    }
    void walk(const Json& node, fs::path& current_file) {
        if (std::holds_alternative<std::monostate>(node.value)) return;
        if (!spend()) return;
        const auto file_key = node["_cpprails_file"].text();
        if (!file_key.empty()) current_file = path_from_utf8(file_key);
        const auto requested = source_lookup.find(file_key.empty() ? lexical_path_key(current_file) : file_key);
        if (requested != source_lookup.end() && (node.has("loc") || node.has("range"))) observed_files.insert(requested->second.first);
        const auto k = kind(node);
        if ((k == "FunctionDecl" || k == "CXXMethodDecl" || k == "CXXConversionDecl") && requested != source_lookup.end() && !node.has("isImplicit")) {
            for (const auto& child : node["inner"].array()) if (kind(child) == "CompoundStmt") {
                function_file = requested->second.first; function_file_key = requested->first; source_fingerprint = fingerprint(*requested->second.second);
                const auto before_new = stats.acquisitions, before_delete = stats.releases, before_unknown = stats.unknown;
                const auto before_invalidations = stats.invalidations, before_flow = stats.unsupported_flow, before_aliases = stats.aliases;
                ++stats.functions; function_name = node["name"].text(); function_type = type(node); owners.clear(); views.clear(); borrow_relations.clear();
                visit_body(child); if (options.safety && !stats.exhausted) { ownership(child); invalidation(child); }
                std::ostringstream summary;
                const bool unknown_effects = stats.unknown != before_unknown || stats.unsupported_flow != before_flow || stats.aliases != before_aliases || stats.exhausted;
                summary << "{\"name\":" << quote(function_name) << ",\"mangled_name\":" << quote(node["mangledName"].text()) << ",\"type\":" << quote(type(node))
                        << ",\"path\":" << quote(relative_path(function_file, root)) << ",\"line\":" << line(node)
                        << ",\"translation_unit\":" << quote(relative_path(unit.source, root))
                        << ",\"source_fingerprint\":" << quote(source_fingerprint) << ",\"compiler_executable\":" << quote(options.clang_executable)
                        << ",\"compiler_version\":" << quote(compiler_version) << ",\"compile_command_digest\":" << quote(command_digest(unit))
                        << ",\"replay_command_digest\":" << quote(replay_digest)
                        << ",\"observed_acquisitions\":" << stats.acquisitions - before_new << ",\"observed_releases\":" << stats.releases - before_delete
                        << ",\"recognized_invalidations\":" << stats.invalidations - before_invalidations << ",\"opaque_calls\":" << stats.unknown - before_unknown
                        << ",\"unsupported_control_flow\":" << stats.unsupported_flow - before_flow << ",\"unmodeled_aliases\":" << stats.aliases - before_aliases
                        << ",\"effect_scope\":" << quote(unknown_effects ? "unknown" : "selected-intraprocedural-structure")
                        << ",\"evidence_level\":\"verified-structure\",\"locals\":[";
                bool comma = false;
                for (const auto& [id, name] : owners) { (void)id; if (comma) summary << ','; comma = true; summary << "{\"name\":" << quote(name) << ",\"relation\":\"automatic-storage-owner\"}"; }
                summary << "],\"borrow_relations\":[";
                for (std::size_t i = 0; i < borrow_relations.size(); ++i) { if (i) summary << ','; summary << borrow_relations[i]; }
                summary << "],\"whole_function_safety\":\"UNVERIFIED\"}";
                stats.function_summaries.push_back(summary.str());
                return;
            }
        }
        for (const auto& child : node["inner"].array()) walk(child, current_file);
    }
};
void sections(const Options& options, const Stats& stats, Report& report) {
    std::ostringstream coverage;
    coverage << "{\"backend\":\"clang-json-ast\",\"selected_translation_units\":" << stats.selected << ",\"analyzed_translation_units\":" << stats.analyzed
             << ",\"uncovered_requested_translation_units\":" << stats.uncovered
             << ",\"requested_implementation_fragments\":" << stats.requested_fragments << ",\"observed_implementation_fragments\":" << stats.observed_fragments
             << ",\"uncovered_implementation_fragments\":" << stats.uncovered_fragments
             << ",\"visited_ast_nodes\":" << stats.nodes << ",\"analyzed_functions\":" << stats.functions << ",\"opaque_call_boundaries\":" << stats.unknown
             << ",\"node_budget\":" << options.analysis_budget << ",\"budget_exhausted\":" << (stats.exhausted ? "true" : "false")
             << ",\"whole_program_proof\":false,\"ast_input_limit_bytes\":" << max_json_bytes << ",\"per_tu_timeout_seconds\":60}";
    report.sections.push_back({"semantic-coverage", coverage.str(), "Clang AST coverage: " + std::to_string(stats.analyzed) + "/" + std::to_string(stats.selected) + " selected translation units; " + std::to_string(stats.functions) + " functions; " + std::to_string(stats.unknown) + " opaque call boundaries. Whole-program safety remains UNVERIFIED."});
    if (options.safety) {
        std::string contracts = "{\"scope\":\"intraprocedural\",\"properties\":[{\"id\":\"SEM-DANGLING-VIEW\",\"contract\":\"returned std::string_view/std::span must outlive local automatic storage\"},{\"id\":\"SEM-DELETE-MISMATCH\",\"contract\":\"direct primitive-element local new and delete must use matching array forms\"},{\"id\":\"SEM-DOUBLE-DELETE\",\"contract\":\"straight-line primitive-element local allocation has at most one release\"},{\"id\":\"SEM-INVALIDATED-VIEW\",\"contract\":\"direct view element access must precede vector::clear of its local owner\"}],\"observed_acquisitions\":" + std::to_string(stats.acquisitions) + ",\"observed_releases\":" + std::to_string(stats.releases) + ",\"boundaries\":[\"opaque calls and wrappers\",\"aliases and mutations\",\"exceptions and branches\",\"cross-TU ownership\",\"concurrency\",\"other iterator invalidation\"],\"unsupported_scope\":\"unknown\",\"universal_safety\":\"UNVERIFIED\"}";
        report.sections.push_back({"safety-contracts", contracts, "Bounded contracts: local view return lifetime; primitive-element direct new/delete form and duplicate release; view element access after local vector::clear. Unknown: opaque calls/wrappers, aliases, mutation, control flow/exceptions, cross-TU ownership, concurrency and other iterator invalidation. A clean result does not prove safety."});
        std::string functions = "{\"fingerprint_algorithm\":\"FNV-1a-64 noncryptographic provenance\",\"functions\":[";
        for (std::size_t i = 0; i < stats.function_summaries.size(); ++i) { if (i) functions += ','; functions += stats.function_summaries[i]; }
        functions += "],\"effects_beyond_selected_contracts\":\"unknown\"}";
        std::string markdown = "Inferred per-function effects (source/compiler/compile-command bound):\n\n| Function | Evidence |\n|---|---|\n";
        for (const auto& entry : stats.function_summaries) {
            auto parsed = Parser(entry).all();
            auto display = parsed["name"].text(); std::ranges::replace(display, '|', '/');
            markdown += "| " + display + " | " + parsed["observed_acquisitions"].text() + " acquisitions; " + parsed["observed_releases"].text() + " releases; " + parsed["recognized_invalidations"].text() + " invalidations; " + parsed["opaque_calls"].text() + " opaque calls; " + parsed["effect_scope"].text() + " |\n";
        }
        report.sections.push_back({"safety-function-effects", functions, markdown});
    }
}
} // namespace

void analyze_semantic(const Options& options, const fs::path& requested_root, const std::vector<fs::path>& files, Report& report) {
    if (!options.semantic && !options.safety) return;
    Stats stats;
    auto unavailable = [&](const std::string& details) {
        report.gate("semantic", Status::Unverified, details);
        if (options.safety) report.gate("safety", Status::Unverified, "Bounded safety analysis requires successfully parsed Clang ASTs; " + details);
        sections(options, stats, report);
    };
    try {
        // Source paths are canonicalized below. Match the reporting root to
        // that identity too (Windows TEMP can contain an 8.3 directory alias).
        const auto root = fs::weakly_canonical(requested_root);
        fs::path database;
        if (options.compilation_database) database = absolute(*options.compilation_database, root);
        else if (fs::is_regular_file(root / "compile_commands.json")) database = root / "compile_commands.json";
        else if (options.build_dir && fs::is_regular_file(absolute(*options.build_dir, root) / "compile_commands.json")) database = absolute(*options.build_dir, root) / "compile_commands.json";
        else { unavailable("No compilation database: provide --compilation-database with the target's actual flags and working directories."); return; }
        if (fs::is_directory(database)) database /= "compile_commands.json";
        auto entries = units(Parser(read(database)).all(), database.parent_path());
        std::map<fs::path, std::string> project_sources;
        std::vector<fs::path> requested_paths, fragments;
        std::set<fs::path> observed_files;
        for (const auto& file : files) {
            auto path = absolute(file, root);
            project_sources.emplace(path, read(path)); requested_paths.push_back(path);
            auto extension = path.extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension == ".inc" || extension == ".inl" || extension == ".ipp" || extension == ".tpp") fragments.push_back(path);
        }
        stats.requested_fragments = fragments.size();
        stats.uncovered_fragments = stats.requested_fragments;
        std::vector<Unit> selected;
        for (const auto& unit : entries) if (std::ranges::any_of(files, [&](const auto& path) { return same_path(absolute(path, root), unit.source); })) selected.push_back(unit);
        stats.selected = selected.size();
        for (const auto& file : files) {
            auto extension = file.extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".c" && extension != ".cc" && extension != ".cpp" && extension != ".cxx" && extension != ".c++" && extension != ".m" && extension != ".mm" && extension != ".ixx") continue;
            const auto wanted = absolute(file, root);
            if (std::ranges::none_of(selected, [&](const Unit& unit) { return same_path(unit.source, wanted); })) {
                ++stats.uncovered; report.notes.push_back("Semantic UNVERIFIED: no compilation command for selected source " + relative_path(wanted, root));
            }
        }
        if (selected.empty()) { unavailable("No selected source has a matching compilation database entry; headers require an includer translation unit."); return; }
        if (options.clang_executable.empty() || options.clang_executable.find('\0') != std::string::npos) { unavailable("Invalid compiler executable"); return; }
        Temp identity;
        const auto identity_output = identity.directory / "version.txt";
        const auto identity_errors = identity.directory / "version-errors.txt";
        const auto version_run = capture({options.clang_executable, "--no-default-config", "--version"}, root, identity_output, identity_errors);
        if (!version_run.started || version_run.timed_out || version_run.code != 0) { unavailable("Clang compiler identity unavailable; executable could not report --version"); return; }
        auto compiler_version = read(identity_output, 64 * 1024);
        if (compiler_version.find("clang version") == std::string::npos) { unavailable("Backend did not identify itself as Clang"); return; }
        std::size_t failures = 0;
        std::string replay_context = "clang-ast-replay-v2\n" + options.clang_executable + '\n' + compiler_version + "\nsafety=" + (options.safety ? "true" : "false");
#ifdef CPP_RAILS_VERSION
        replay_context += "\ntool=" CPP_RAILS_VERSION;
#endif
        for (const auto name : {"INCLUDE", "CL", "_CL_", "CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "CCC_OVERRIDE_OPTIONS"})
            replay_context += '\n' + std::string(name) + '=' + environment_value(name);
        for (const auto& unit : selected) {
            if (stats.exhausted) break;
            try {
                Temp temp;
                const auto output = temp.directory / "ast.json";
                const auto diagnostics = temp.directory / "diagnostics.txt";
                auto source_content = read(unit.source);
                auto replay_args = compiler_args(unit, options);
                std::string command = utf8_bytes(unit.cwd.u8string()); command += '\0';
                for (const auto& arg : replay_args) { command += arg; command += '\0'; }
                const auto replay_digest = fingerprint(command);
                replay_context += '\n' + replay_digest;
                const auto run = capture(replay_args, unit.cwd, output, diagnostics);
                if (!run.started || run.timed_out || run.code != 0) {
                    ++failures;
                    std::string detail = !run.started ? "compiler could not be started" : run.timed_out ? "60 second compiler timeout" : "compiler exited " + std::to_string(run.code);
                    if (fs::is_regular_file(diagnostics)) { auto message = read(diagnostics, 16 * 1024 * 1024); if (message.size() > 4000) message.resize(4000); if (!message.empty()) detail += ": " + message; }
                    report.notes.push_back("Semantic UNVERIFIED for " + relative_path(unit.source, root) + ": " + detail);
                    continue;
                }
                auto ast = Parser(read(output), requested_paths, unit.cwd).all();
                if (kind(ast) != "TranslationUnitDecl") throw std::runtime_error("compiler output is not a Clang TranslationUnitDecl AST");
                if (source_content != read(unit.source)) throw std::runtime_error("source changed during compiler analysis; evidence cannot be bound");
                for (const auto& [path, content] : project_sources) if (content != read(path)) throw std::runtime_error("requested project file changed during compiler analysis; evidence cannot be bound: " + relative_path(path, root));
                Analysis analysis{options, root, unit, report, stats, std::move(source_content), compiler_version, project_sources, observed_files, replay_digest};
                fs::path current_file; analysis.walk(ast, current_file);
                if (!stats.exhausted) ++stats.analyzed;
            } catch (const std::exception& error) { ++failures; report.notes.push_back("Semantic UNVERIFIED for " + relative_path(unit.source, root) + ": " + error.what()); }
        }
        report.analyzer_contexts["clang-ast"] = fingerprint(replay_context);
        stats.uncovered_fragments = 0;
        for (const auto& fragment : fragments) {
            if (observed_files.contains(fragment)) ++stats.observed_fragments;
            else {
                ++stats.uncovered_fragments;
                report.notes.push_back("Semantic UNVERIFIED: no retained AST declaration/expression for requested implementation fragment " + relative_path(fragment, root) + "; it may be unincluded, inactive, empty or outside retained AST scope; fragments are never compiled standalone.");
            }
        }
        const bool complete = !failures && !stats.uncovered && !stats.uncovered_fragments && !stats.exhausted && stats.analyzed == stats.selected;
        report.gate("semantic", complete ? Status::Pass : Status::Unverified,
                    std::to_string(stats.analyzed) + "/" + std::to_string(stats.selected) + " translation units parsed with actual compile flags; " + std::to_string(stats.observed_fragments) + "/" + std::to_string(stats.requested_fragments) + " requested implementation fragments observed in retained AST; " + (stats.exhausted ? "AST node budget exhausted. " : "") + "PASS means AST coverage only for active parsed contexts; unsupported effects remain unknown.");
        if (options.safety) report.gate("safety", complete ? Status::Pass : Status::Unverified, "Selected bounded intraprocedural contracts executed; whole-program safety, opaque effects and unsupported invalidation remain UNVERIFIED. Findings are reported separately.");
        sections(options, stats, report);
    } catch (const std::exception& error) { unavailable(error.what()); }
}

bool semantic_dependencies(const Options& options, const fs::path& root, const fs::path& source,
                           std::vector<fs::path>& dependencies, std::string& diagnostics) {
    dependencies.clear();
    try {
        fs::path database;
        if (options.compilation_database) database = absolute(*options.compilation_database, root);
        else if (fs::is_regular_file(root / "compile_commands.json")) database = root / "compile_commands.json";
        else if (options.build_dir && fs::is_regular_file(absolute(*options.build_dir, root) / "compile_commands.json")) database = absolute(*options.build_dir, root) / "compile_commands.json";
        else throw std::runtime_error("no compilation database for verified dependencies");
        if (fs::is_directory(database)) database /= "compile_commands.json";
        const auto entries = units(Parser(read(database)).all(), database.parent_path());
        const auto wanted = absolute(source, root);
        auto entry = std::ranges::find_if(entries, [&](const Unit& unit) { return same_path(unit.source, wanted); });
        if (entry == entries.end()) throw std::runtime_error("no matching translation unit in compilation database");
        if (std::ranges::count_if(entries, [&](const Unit& unit) { return same_path(unit.source, wanted); }) != 1) throw std::runtime_error("ambiguous compile commands; dependency cache disabled");
        auto args = compiler_args(*entry, options);
        // compiler_args appends exactly four AST action arguments in each mode;
        // CL diagnostics policy remains before that action tail.
        args.resize(args.size() - 4);
        if (cl_driver(entry->args.front())) args.insert(args.end(), {"/clang:-M", "/clang:-MT", "/clang:cpprails"});
        else args.insert(args.end(), {"-M", "-MT", "cpprails"});
        Temp temp;
        const auto output = temp.directory / "dependencies.d";
        const auto errors = temp.directory / "diagnostics.txt";
        const auto run = capture(args, entry->cwd, output, errors);
        if (!run.started || run.timed_out || run.code != 0) throw std::runtime_error("compiler dependency extraction unavailable or failed");
        auto data = read(output);
        if (!data.starts_with("cpprails:")) throw std::runtime_error("unsupported compiler dependency output");
        std::string token;
        auto append = [&] {
            if (token.empty()) return;
            auto path = absolute(path_from_utf8(token), entry->cwd);
            if (!fs::is_regular_file(path)) throw std::runtime_error("missing compiler dependency: " + token);
            if (std::ranges::none_of(dependencies, [&](const fs::path& old) { return same_path(old, path); })) dependencies.push_back(std::move(path));
            token.clear();
        };
        for (std::size_t i = 9; i < data.size(); ++i) {
            char c = data[i];
            if (c == '\\' && i + 1 < data.size()) {
                if (data[i + 1] == '\n') { ++i; continue; }
                if (data[i + 1] == '\r' && i + 2 < data.size() && data[i + 2] == '\n') { i += 2; continue; }
                if (std::isspace(static_cast<unsigned char>(data[i + 1])) || data[i + 1] == '#' || data[i + 1] == '\\' || data[i + 1] == ':') { token += data[++i]; continue; }
            }
            if (c == '$' && i + 1 < data.size() && data[i + 1] == '$') { token += '$'; ++i; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) append(); else token += c;
        }
        append();
        if (dependencies.empty() || std::ranges::none_of(dependencies, [&](const fs::path& path) { return same_path(path, wanted); })) throw std::runtime_error("compiler dependency closure does not contain source");
        diagnostics = "Compiler -M closure includes source and all reported user/system headers; " + std::to_string(dependencies.size()) + " readable files.";
        return true;
    } catch (const std::exception& error) { dependencies.clear(); diagnostics = error.what(); return false; }
}
} // namespace cpprails
