#include "semantic.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
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
    std::string ast_source, ast_file;
    fs::path ast_cwd;
    void space() { while (at < input.size() && std::isspace(static_cast<unsigned char>(input[at]))) ++at; }
    [[noreturn]] void error() const { throw std::runtime_error("invalid JSON at byte " + std::to_string(at)); }
    void skip() {
        space(); if (at == input.size()) error();
        if (input[at] != '[' && input[at] != '{') { (void)parse(0); return; }
        std::vector<char> brackets; bool in_string = false;
        do {
            if (at == input.size()) error();
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
            ++at; Json::Object object; space();
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
                const bool header_inner = !ast_source.empty() && !ast_file.empty() && ast_file != ast_source && key == "inner" && !ast_kind.empty() && !container;
                if (header_inner) skip();
                else {
                    auto parsed = parse(depth + 1);
                    if (key == "loc" && !ast_source.empty() && !ast_kind.empty()) {
                        const auto& loc = parsed.has("expansionLoc") ? parsed["expansionLoc"] : parsed;
                        if (!loc["file"].text().empty()) {
                            // Clang elides repeated physical filenames. Preserve
                            // its traversal order, not C++ lexical parent scope.
                            auto path = path_from_utf8(loc["file"].text());
                            ast_file = fs::weakly_canonical(path.is_absolute() ? path : ast_cwd / path).generic_string();
#ifdef _WIN32
                            std::ranges::transform(ast_file, ast_file.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
                        }
                    }
                    if (!object.emplace(std::move(key), std::move(parsed)).second) error();
                }
                space(); if (at == input.size()) error(); char next = input[at++];
                if (next == '}') return Json{std::move(object)};
                if (next != ',') error();
            }
        }
        if (input[at] == '[') {
            ++at; Json::Array array; space();
            if (at < input.size() && input[at] == ']') { ++at; return Json{std::move(array)}; }
            for (;;) {
                array.push_back(parse(depth + 1)); space(); if (at == input.size()) error();
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
    explicit Parser(std::string_view text, const fs::path& source = {}, const fs::path& cwd = {}) : input(text), ast_cwd(cwd) {
        if (!source.empty()) {
            ast_source = source.lexically_normal().generic_string();
#ifdef _WIN32
            std::ranges::transform(ast_source, ast_source.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        }
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
    if (error) throw std::runtime_error("cannot read " + path.generic_string());
    if (size > limit) throw std::runtime_error("complete capture exceeds explicit " + std::to_string(limit) + " byte input limit");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.generic_string());
    std::string data(static_cast<std::size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) throw std::runtime_error("incomplete file read");
    return data;
}
fs::path absolute(const fs::path& path, const fs::path& base) { return fs::weakly_canonical(path.is_absolute() ? path : base / path); }
bool same_path(const fs::path& left, const fs::path& right) {
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
std::vector<std::string> split_command(const std::string& command) {
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
    for (std::size_t i = 1; i < unit.args.size(); ++i) {
        const auto& arg = unit.args[i];
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
    args.insert(args.end(), {"-fsyntax-only", "-fno-color-diagnostics", "-Xclang", "-ast-dump=json"});
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
    std::vector<std::string> function_summaries;
};
struct Analysis {
    const Options& options;
    const fs::path& root;
    const Unit& unit;
    Report& report;
    Stats& stats;
    std::string source;
    std::map<std::string, std::string> owners, views;
    std::string function_name;
    std::string function_type;
    std::string compiler_version;
    std::string source_fingerprint;
    std::vector<std::string> borrow_relations;
    std::map<std::string, unsigned> finding_occurrences;
    Analysis(const Options& opt, const fs::path& base, const Unit& translation_unit,
             Report& output, Stats& counts, std::string content, std::string version)
        : options(opt), root(base), unit(translation_unit), report(output), stats(counts),
          source(std::move(content)), compiler_version(std::move(version)), source_fingerprint(fingerprint(source)) {}
    bool spend() {
        if (options.analysis_budget && stats.nodes >= options.analysis_budget) { stats.exhausted = true; return false; }
        ++stats.nodes; return true;
    }
    void finding(const std::string& rule, const Json& node, const std::string& message, const std::string& evidence) {
        Finding finding;
        finding.rule = rule; finding.path = relative_path(unit.source, root); finding.line = line_of(node, source);
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
        auto k = kind(node);
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
                    borrow_relations.push_back("{\"borrow\":" + quote(node["name"].text()) + ",\"owner\":" + quote(owners[owner]) + ",\"line\":" + std::to_string(line_of(node, source)) + ",\"scope\":\"initializer-structure\"}");
                }
            }
            if ((value_type.find('*') != std::string::npos || value_type.find('&') != std::string::npos) && !descendant(node, "CXXNewExpr")) ++stats.aliases;
        }
        if (k == "ReturnStmt") {
            for (const auto& child : node["inner"].array()) if (view_type(type(child))) {
                auto owner = borrowed_owner(child);
                if (!owner.empty()) {
                    finding("SEM-DANGLING-VIEW", node, "Returned standard view borrows local automatic storage", "return expression of type " + type(child) + " refers to local owner " + owners[owner]);
                    borrow_relations.push_back("{\"borrow\":\"return\",\"owner\":" + quote(owners[owner]) + ",\"line\":" + std::to_string(line_of(node, source)) + ",\"scope\":\"return-expression-structure\"}");
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
            if (complex_flow(k) || k == "LambdaExpr" || k == "BinaryOperator" || k == "CompoundAssignOperator" || k == "CallExpr" || k == "CXXMemberCallExpr" || k == "CXXOperatorCallExpr" || k == "CXXConstructExpr" || k == "CXXTemporaryObjectExpr" || k == "ReturnStmt") supported = false;
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
        if (!spend()) return;
        const auto& loc = location(node);
        if (!loc["file"].text().empty()) current_file = absolute(path_from_utf8(loc["file"].text()), unit.cwd);
        const auto k = kind(node);
        if ((k == "FunctionDecl" || k == "CXXMethodDecl" || k == "CXXConversionDecl") && same_path(current_file, unit.source) && !node.has("isImplicit")) {
            for (const auto& child : node["inner"].array()) if (kind(child) == "CompoundStmt") {
                const auto before_new = stats.acquisitions, before_delete = stats.releases, before_unknown = stats.unknown;
                const auto before_invalidations = stats.invalidations, before_flow = stats.unsupported_flow, before_aliases = stats.aliases;
                ++stats.functions; function_name = node["name"].text(); function_type = type(node); owners.clear(); views.clear(); borrow_relations.clear();
                visit_body(child); if (options.safety && !stats.exhausted) { ownership(child); invalidation(child); }
                std::ostringstream summary;
                const bool unknown_effects = stats.unknown != before_unknown || stats.unsupported_flow != before_flow || stats.aliases != before_aliases || stats.exhausted;
                summary << "{\"name\":" << quote(function_name) << ",\"mangled_name\":" << quote(node["mangledName"].text()) << ",\"type\":" << quote(type(node))
                        << ",\"path\":" << quote(relative_path(unit.source, root)) << ",\"line\":" << line_of(node, source)
                        << ",\"source_fingerprint\":" << quote(source_fingerprint) << ",\"compiler_executable\":" << quote(options.clang_executable)
                        << ",\"compiler_version\":" << quote(compiler_version) << ",\"compile_command_digest\":" << quote(command_digest(unit))
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

void analyze_semantic(const Options& options, const fs::path& root, const std::vector<fs::path>& files, Report& report) {
    if (!options.semantic && !options.safety) return;
    Stats stats;
    auto unavailable = [&](const std::string& details) {
        report.gate("semantic", Status::Unverified, details);
        if (options.safety) report.gate("safety", Status::Unverified, "Bounded safety analysis requires successfully parsed Clang ASTs; " + details);
        sections(options, stats, report);
    };
    try {
        fs::path database;
        if (options.compilation_database) database = absolute(*options.compilation_database, root);
        else if (fs::is_regular_file(root / "compile_commands.json")) database = root / "compile_commands.json";
        else if (options.build_dir && fs::is_regular_file(absolute(*options.build_dir, root) / "compile_commands.json")) database = absolute(*options.build_dir, root) / "compile_commands.json";
        else { unavailable("No compilation database: provide --compilation-database with the target's actual flags and working directories."); return; }
        if (fs::is_directory(database)) database /= "compile_commands.json";
        auto entries = units(Parser(read(database)).all(), database.parent_path());
        std::vector<Unit> selected;
        for (const auto& unit : entries) if (std::ranges::any_of(files, [&](const auto& path) { return same_path(absolute(path, root), unit.source); })) selected.push_back(unit);
        stats.selected = selected.size();
        for (const auto& file : files) {
            auto extension = file.extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".c" && extension != ".cc" && extension != ".cpp" && extension != ".cxx" && extension != ".c++" && extension != ".m" && extension != ".mm") continue;
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
        for (const auto& unit : selected) {
            if (stats.exhausted) break;
            try {
                Temp temp;
                const auto output = temp.directory / "ast.json";
                const auto diagnostics = temp.directory / "diagnostics.txt";
                auto source_content = read(unit.source);
                const auto run = capture(compiler_args(unit, options), unit.cwd, output, diagnostics);
                if (!run.started || run.timed_out || run.code != 0) {
                    ++failures;
                    std::string detail = !run.started ? "compiler could not be started" : run.timed_out ? "60 second compiler timeout" : "compiler exited " + std::to_string(run.code);
                    if (fs::is_regular_file(diagnostics)) { auto message = read(diagnostics, 16 * 1024 * 1024); if (message.size() > 4000) message.resize(4000); if (!message.empty()) detail += ": " + message; }
                    report.notes.push_back("Semantic UNVERIFIED for " + relative_path(unit.source, root) + ": " + detail);
                    continue;
                }
                auto ast = Parser(read(output), unit.source, unit.cwd).all();
                if (kind(ast) != "TranslationUnitDecl") throw std::runtime_error("compiler output is not a Clang TranslationUnitDecl AST");
                if (source_content != read(unit.source)) throw std::runtime_error("source changed during compiler analysis; evidence cannot be bound");
                Analysis analysis{options, root, unit, report, stats, std::move(source_content), compiler_version};
                fs::path current_file; analysis.walk(ast, current_file);
                if (!stats.exhausted) ++stats.analyzed;
            } catch (const std::exception& error) { ++failures; report.notes.push_back("Semantic UNVERIFIED for " + relative_path(unit.source, root) + ": " + error.what()); }
        }
        const bool complete = !failures && !stats.uncovered && !stats.exhausted && stats.analyzed == stats.selected;
        report.gate("semantic", complete ? Status::Pass : Status::Unverified,
                    std::to_string(stats.analyzed) + "/" + std::to_string(stats.selected) + " translation units parsed with actual compile flags; " + (stats.exhausted ? "AST node budget exhausted. " : "") + "PASS means AST coverage only; unsupported effects remain unknown.");
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
        // compiler_args appends exactly the AST action tail; replace that action.
        args.resize(args.size() - 4);
        args.insert(args.end(), {"-M", "-MT", "cpprails"});
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
