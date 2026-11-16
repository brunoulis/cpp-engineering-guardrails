#include "msvc.hpp"
#include "detail.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <variant>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace cpprails {
namespace {
using detail::json_quote;
using detail::trim;
std::string text(const fs::path& path) { return utf8_bytes(path.generic_u8string()); }
bool valid_config(const std::string& value) {
    return !value.empty() && std::ranges::all_of(value, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ' ';
    });
}
#ifdef _WIN32
constexpr std::uintmax_t max_evaluation_bytes = 64ULL * 1024 * 1024;
struct Json {
    using Object = std::map<std::string, Json, std::less<>>;
    using Array = std::vector<Json>;
    std::variant<std::string, Object, Array> value;
    const Object& object() const {
        if (const auto* result = std::get_if<Object>(&value)) return *result;
        throw std::runtime_error("MSBuild JSON object expected");
    }
    const Array& array() const {
        if (const auto* result = std::get_if<Array>(&value)) return *result;
        throw std::runtime_error("MSBuild JSON array expected");
    }
    const Json& at(std::string_view key) const {
        const auto& values = object(); const auto found = values.find(key);
        if (found == values.end()) throw std::runtime_error("missing MSBuild evaluation field: " + std::string(key));
        return found->second;
    }
    std::string string() const {
        if (const auto* result = std::get_if<std::string>(&value)) return *result;
        throw std::runtime_error("MSBuild JSON string expected");
    }
    std::string get(std::string_view key) const {
        const auto& values = object(); const auto found = values.find(key);
        return found == values.end() ? std::string{} : found->second.string();
    }
};
// MSBuild's getItem/getProperty protocol contains strings, objects and arrays.
// Reject atoms, duplicate keys, bad escapes and excess depth instead of guessing.
class Parser {
    std::string_view input;
    std::size_t position = 0;
    [[noreturn]] void error() const { throw std::runtime_error("invalid MSBuild JSON at byte " + std::to_string(position)); }
    void space() { while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) ++position; }
    unsigned hex() {
        unsigned result = 0;
        for (unsigned i = 0; i != 4; ++i) {
            if (position == input.size()) error();
            const unsigned char c = static_cast<unsigned char>(input[position++]);
            if (!std::isxdigit(c)) error();
            result = result * 16 + (c <= '9' ? c - '0' : std::tolower(c) - 'a' + 10);
        }
        return result;
    }
    static void utf8(std::string& out, unsigned c) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63)); }
        else if (c < 0x10000) { out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
        else { out += static_cast<char>(0xf0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 63)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    }
    std::string string() {
        if (position == input.size() || input[position++] != '"') error();
        std::string result;
        while (position < input.size()) {
            char c = input[position++];
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 32) error();
            if (c != '\\') { result += c; continue; }
            if (position == input.size()) error();
            switch (input[position++]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'u': {
                unsigned code = hex();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (input.substr(position, 2) != "\\u") error();
                    position += 2; const auto low = hex();
                    if (low < 0xdc00 || low > 0xdfff) error();
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) error();
                utf8(result, code); break;
            }
            default: error();
            }
        }
        error();
    }
    Json value(unsigned depth) {
        if (depth > 64) error();
        space(); if (position == input.size()) error();
        if (input[position] == '"') return {string()};
        if (input[position] == '{') {
            ++position; Json::Object object; space();
            if (position < input.size() && input[position] == '}') { ++position; return {std::move(object)}; }
            for (;;) {
                space(); auto key = string(); space();
                if (position == input.size() || input[position++] != ':') error();
                if (!object.emplace(std::move(key), value(depth + 1)).second) error();
                space(); if (position == input.size()) error();
                const auto next = input[position++];
                if (next == '}') return {std::move(object)};
                if (next != ',') error();
            }
        }
        if (input[position] == '[') {
            ++position; Json::Array array; space();
            if (position < input.size() && input[position] == ']') { ++position; return {std::move(array)}; }
            for (;;) {
                array.push_back(value(depth + 1)); space();
                if (position == input.size()) error();
                const auto next = input[position++];
                if (next == ']') return {std::move(array)};
                if (next != ',') error();
            }
        }
        error();
    }
public:
    explicit Parser(std::string_view data) : input(data) {
        if (input.starts_with("\xef\xbb\xbf")) position = 3;
    }
    Json parse() { auto result = value(0); space(); if (position != input.size()) error(); return result; }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
std::wstring quote_argument(std::wstring_view argument) {
    std::wstring result = L"\""; unsigned slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c; slashes = 0;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}
std::wstring environment(const wchar_t* name) {
    const auto count = GetEnvironmentVariableW(name, nullptr, 0);
    if (!count) return {};
    std::wstring result(count, L'\0');
    const auto copied = GetEnvironmentVariableW(name, result.data(), count);
    if (!copied || copied >= count) return {};
    result.resize(copied); return result;
}
fs::path find_msbuild() {
    const auto path = environment(L"PATH");
    for (std::size_t begin = 0; begin < path.size();) {
        const auto end = path.find(L';', begin);
        auto part = path.substr(begin, end == std::wstring::npos ? end : end - begin);
        if (part.size() > 1 && part.front() == L'"' && part.back() == L'"') part = part.substr(1, part.size() - 2);
        const auto candidate = fs::path(part) / "MSBuild.exe";
        if (!part.empty() && fs::path(part).is_absolute() && fs::is_regular_file(candidate)) return candidate;
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    for (const auto* variable : {L"ProgramFiles(x86)", L"ProgramFiles"}) {
        const auto base = environment(variable);
        if (base.empty()) continue;
        std::error_code error;
        const auto install_root = fs::path(base) / "Microsoft Visual Studio";
        std::vector<fs::path> versions;
        for (fs::directory_iterator it(install_root, error), end; !error && it != end; it.increment(error))
            if (it->is_directory()) versions.push_back(it->path());
        std::ranges::sort(versions, std::greater<>{});
        for (const auto& version : versions) {
            for (fs::directory_iterator it(version, error), end; !error && it != end; it.increment(error)) {
                if (!it->is_directory()) continue;
                for (const auto* tail : {L"MSBuild/Current/Bin/amd64/MSBuild.exe", L"MSBuild/Current/Bin/MSBuild.exe"}) {
                    const auto candidate = it->path() / tail;
                    if (fs::is_regular_file(candidate)) return candidate;
                }
            }
            error.clear();
        }
    }
    throw std::runtime_error("installed Visual Studio MSBuild with C++ tools not found on PATH or standard installation roots");
}
struct Temp {
    fs::path path;
    Temp() {
        const auto parent = fs::temp_directory_path();
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            path = parent / ("cpprails-msbuild-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) return;
        }
        throw std::runtime_error("cannot create private MSBuild evaluation directory");
    }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
std::string bounded_read(const fs::path& path) {
    if (!fs::is_regular_file(path) || fs::file_size(path) > max_evaluation_bytes)
        throw std::runtime_error("missing or oversized MSBuild evaluation JSON (limit 64 MiB); requires getResultOutputFile support");
    return detail::read_file(path);
}
void evaluate(const fs::path& msbuild, const fs::path& project, const std::string& config,
              const std::string& platform, const fs::path& result, const fs::path& log) {
    const std::vector<std::string> args{
        text(project), "-nologo", "-noAutoResponse", "-nodeReuse:false", "-verbosity:quiet",
        "-p:Configuration=" + config, "-p:Platform=" + platform,
        "-getItem:ClCompile,ProjectConfiguration",
        "-getProperty:IncludePath,ExternalIncludePath,VCToolsInstallDir,VCToolsVersion,WindowsSdkDir,WindowsTargetPlatformVersion,TargetPlatformVersion,UniversalCRT_IncludePath,TargetUniversalCRTVersion,PlatformToolset,Platform,Configuration,ConfigurationType,CharacterSet,UseOfMfc,UseOfAtl,CLRSupport,CLToolExe,CLToolPath,UseEnv,VC_IncludePath,WindowsSDK_IncludePath",
        "-getResultOutputFile:" + text(result)};
    std::wstring command = quote_argument(msbuild.native());
    for (const auto& argument : args) { command += L' '; command += quote_argument(path_from_utf8(argument).native()); }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle output(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!output || !input || !job || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw std::runtime_error("cannot prepare bounded MSBuild process");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.value; startup.hStdOutput = output.value; startup.hStdError = output.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(msbuild.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, project.parent_path().c_str(), &startup, &process))
        throw std::runtime_error("cannot start installed MSBuild");
    Handle child(process.hProcess), thread(process.hThread);
    if (!AssignProcessToJobObject(job.value, child.value) || ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
        TerminateProcess(child.value, 1); WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("cannot bound MSBuild process lifetime");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
        const auto wait = WaitForSingleObject(child.value, 50);
        if (wait == WAIT_OBJECT_0) break;
        LARGE_INTEGER size{};
        std::error_code result_error;
        const auto result_bytes = fs::file_size(result, result_error);
        const bool oversized = (GetFileSizeEx(output.value, &size) && static_cast<std::uintmax_t>(size.QuadPart) > max_evaluation_bytes) ||
                               (!result_error && result_bytes > max_evaluation_bytes);
        if (wait != WAIT_TIMEOUT || oversized || std::chrono::steady_clock::now() >= deadline) {
            TerminateJobObject(job.value, 1); WaitForSingleObject(child.value, 5000);
            throw std::runtime_error(oversized ? "MSBuild diagnostic output exceeded 64 MiB" : "MSBuild evaluation timed out or process wait failed (60 second limit)");
        }
    }
    if (fs::file_size(log) > max_evaluation_bytes) throw std::runtime_error("MSBuild diagnostic output exceeded 64 MiB");
    DWORD code = 1;
    if (!GetExitCodeProcess(child.value, &code) || code != 0) {
        // Read only a bounded diagnostic excerpt, never the full 64 MiB log.
        std::ifstream stream(log, std::ios::binary); std::string excerpt(4096, '\0');
        stream.read(excerpt.data(), static_cast<std::streamsize>(excerpt.size())); excerpt.resize(static_cast<std::size_t>(stream.gcount()));
        throw std::runtime_error("MSBuild evaluation failed (requires supported getItem/getProperty/getResultOutputFile): " + excerpt);
    }
}
void resolved(std::string_view value, std::string_view field) {
    if (value.find("$(") != std::string_view::npos || value.find("%(") != std::string_view::npos || value.find("@(") != std::string_view::npos ||
        std::ranges::any_of(value, [](unsigned char c) { return c < 32; }))
        throw std::runtime_error("unresolved or control-character MSBuild context: " + std::string(field));
}
std::vector<std::string> list(const std::string& value) {
    std::vector<std::string> result;
    bool quoted = false; unsigned slashes = 0;
    for (char c : value) {
        if (c == '\\') { ++slashes; continue; }
        if (c == '"' && slashes % 2 == 0) quoted = !quoted;
        if (c == ';' && quoted) throw std::runtime_error("quoted semicolon in evaluated MSBuild list is unsupported; query JSON does not preserve MSBuild escaping");
        slashes = 0;
    }
    for (std::size_t begin = 0; begin < value.size();) {
        const auto end = value.find(';', begin); auto part = trim(value.substr(begin, end == std::string::npos ? end : end - begin));
        if (!part.empty()) result.push_back(std::move(part));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}
// Windows command-line quote/backslash rules, without shell expansion or execution.
std::vector<std::string> split_options(const std::string& input) {
    std::vector<std::string> result; std::string part; bool quoted = false, active = false;
    for (std::size_t i = 0; i < input.size();) {
        if (!quoted && std::isspace(static_cast<unsigned char>(input[i]))) {
            if (active) { result.push_back(std::move(part)); part.clear(); active = false; }
            ++i; continue;
        }
        active = true;
        unsigned slashes = 0;
        while (i < input.size() && input[i] == '\\') { ++slashes; ++i; }
        if (i < input.size() && input[i] == '"') {
            part.append(slashes / 2, '\\');
            if (slashes % 2) part += '"';
            else if (quoted && i + 1 < input.size() && input[i + 1] == '"') { part += '"'; ++i; }
            else quoted = !quoted;
            ++i;
        } else {
            part.append(slashes, '\\');
            if (i < input.size()) part += input[i++];
        }
    }
    if (quoted) throw std::runtime_error("unterminated quote in MSBuild AdditionalOptions");
    if (active) result.push_back(std::move(part)); return result;
}
void additional_options(const std::string& value, std::vector<std::string>& args) {
    static const std::set<std::string, std::less<>> flags{
        "/EHsc", "/EHa", "/EHs", "/EHs-c-", "/GR", "/GR-", "/MD", "/MDd", "/MT", "/MTd", "/J",
        "/Gd", "/Gr", "/Gz", "/Gv", "/Od", "/O1", "/O2", "/Ox", "/Oi", "/Oi-", "/Os", "/Ot",
        "/permissive-", "/Zc:__cplusplus", "/Zc:preprocessor", "/Zc:preprocessor-", "/Zc:wchar_t", "/Zc:wchar_t-",
        "/Zc:forScope", "/Zc:forScope-", "/Zc:char8_t", "/Zc:char8_t-", "/Zc:alignedNew", "/Zc:alignedNew-",
        "/Zc:sizedDealloc", "/Zc:sizedDealloc-", "/Zc:strictStrings", "/Zc:strictStrings-", "/Zc:twoPhase", "/Zc:twoPhase-",
        "/utf-8", "/source-charset:utf-8", "/execution-charset:utf-8",
        "/fp:fast", "/fp:precise", "/fp:strict", "/arch:IA32", "/arch:SSE", "/arch:SSE2", "/arch:AVX", "/arch:AVX2", "/arch:AVX512",
        "/Zp1", "/Zp2", "/Zp4", "/Zp8", "/Zp16", "/std:c++14", "/std:c++17", "/std:c++20", "/std:c++latest", "/std:c++23preview", "/std:c11", "/std:c17"};
    auto tokens = split_options(value);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        bool accepted = flags.contains(token);
        for (const auto* prefix : {"/external:I", "/FI", "/D", "/U", "/I"}) {
            if (!token.starts_with(prefix)) continue;
            accepted = true;
            if (token == prefix) {
                if (++i == tokens.size() || tokens[i].empty() || tokens[i].front() == '@')
                    throw std::runtime_error("missing AdditionalOptions argument for " + token);
                args.push_back(token + tokens[i]);
            } else args.push_back(token);
            break;
        }
        if (!accepted) throw std::runtime_error("unsupported AdditionalOptions semantics: " + token);
        if (flags.contains(token)) args.push_back(token);
    }
}
void map_option(const Json& item, std::string_view field,
                std::initializer_list<std::pair<std::string_view, std::string_view>> mapping,
                std::vector<std::string>& args) {
    const auto value = item.get(field);
    for (const auto& [key, flag] : mapping) if (key == value) {
        if (!flag.empty()) args.emplace_back(flag); return;
    }
    throw std::runtime_error("unsupported evaluated " + std::string(field) + ": " + value);
}
std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value;
}
void boolean_option(const Json& item, std::string_view field, const std::string& yes, const std::string& no,
                    std::vector<std::string>& args) {
    const auto value = lower(item.get(field));
    if (value.empty()) return;
    if (value != "true" && value != "false") throw std::runtime_error("unsupported boolean metadata: " + std::string(field));
    const auto& flag = value == "true" ? yes : no; if (!flag.empty()) args.push_back(flag);
}
void require_off(const Json& item, std::string_view field) {
    const auto value = lower(item.get(field));
    if (!value.empty() && value != "false" && value != "none" && value != "notusing" && value != "default")
        throw std::runtime_error("unsupported semantic MSBuild metadata: " + std::string(field) + "=" + value);
}
void validate_metadata(const Json& item) {
    static const std::set<std::string, std::less<>> known{
        "Identity", "StructMemberAlignment", "DisableLanguageExtensions", "ModuleDependenciesFile", "UseFullPaths", "ProgramDataBaseFileName", "ImpLibCompiled", "ScanSourceForModuleDependencies", "EnableASAN", "RemoveUnreferencedCodeData", "ForceConformanceInForLoopScope", "DebugInformationFormat", "DiagnosticsFormat", "AdditionalIncludeDirectories", "SupportJustMyCode", "GenerateXMLDocumentationFiles", "AssemblerOutput", "EnableEnhancedInstructionSet", "RuntimeLibrary", "PreprocessToFile", "GenerateSourceDependencies", "AssemblerListingLocation", "PreprocessSuppressLineNumbers", "EnableVectorLength", "TreatWarningAsError", "EnableFiberSafeOptimizations", "IntelJCCErratum", "ShowIncludes", "LanguageStandard_C", "BrowseInformation", "PrecompiledHeader", "EnableFuzzer", "BrowseInformationFile", "UndefineAllPreprocessorDefinitions", "CallingConvention", "FavorSizeOrSpeed", "IntrinsicFunctions", "LanguageStandard", "SmallerTypeCheck", "AdditionalOptions", "ConformanceMode", "WarningLevel", "InlineFunctionExpansion", "PrecompiledHeaderFile", "SpectreMitigation", "SourceDependenciesFile", "XMLDocumentationFileName", "ExternalWarningLevel", "PrecompiledHeaderOutputFile", "BuildStlModules", "GenerateModuleDependencies", "LibCompiled", "MinimalRebuild", "PreprocessKeepComments", "ForcedUsingFiles", "WholeProgramOptimization", "IgnoreStandardIncludePath", "ObjectFileName", "ModuleOutputFile", "UseDynamicDebugging", "LinkCompiled", "TrackerLogDirectory", "TreatAngleIncludeAsExternal", "BasicRuntimeChecks", "AcceptableNonZeroExitCodes", "MinimalRebuildFromTracking", "Optimization", "SuppressStartupBanner", "DisableAnalyzeExternal", "BufferSecurityCheck", "ExceptionHandling", "TreatExternalTemplatesAsInternal", "ErrorReporting", "OmitDefaultLibName", "FloatingPointModel", "CompileAs", "TreatWChar_tAsBuiltInType", "PreprocessorDefinitions", "TranslateIncludes", "ForcedIncludeFiles", "ExpandAttributedSource", "RuntimeTypeInfo", "FullPath", "RootDir", "Filename", "Extension", "RelativeDir", "Directory", "RecursiveDir", "ModifiedTime", "CreatedTime", "AccessedTime", "DefiningProjectFullPath", "DefiningProjectDirectory", "DefiningProjectName", "DefiningProjectExtension",
        "ExcludedFromBuild", "UndefinePreprocessorDefinitions", "AdditionalExternalIncludeDirectories", "ExternalIncludeDirectories", "CompileAsManaged", "CompileAsWinRT", "SDLCheck", "EnforceTypeConversionRules", "OpenMPSupport", "MultiProcessorCompilation", "ProcessorNumber", "TrackerFrameworkPath", "TrackerSdkPath", "TrackFileAccess", "Sources", "BuildingInIDE", "EnablePREfast", "AdditionalUsingDirectories", "DisableSpecificWarnings", "TreatSpecificWarningsAsErrors", "AssemblerListingFileLocation", "ModuleScanSource", "EnableModules", "HeaderUnit", "SourceDependencies", "AllProjectBMIsArePublic"};
    for (const auto& [key, value] : item.object()) {
        resolved(value.string(), key);
        if (!known.contains(key)) throw std::runtime_error("unrecognized evaluated ClCompile metadata (cannot establish semantics): " + key);
    }
    for (const auto* field : {"CompileAsManaged", "CompileAsWinRT", "TranslateIncludes", "BuildStlModules", "GenerateModuleDependencies", "GenerateSourceDependencies", "OpenMPSupport", "EnableModules", "HeaderUnit", "ExpandAttributedSource", "UndefineAllPreprocessorDefinitions", "EnableFiberSafeOptimizations", "EnableFuzzer", "EnableASAN", "BasicRuntimeChecks", "SmallerTypeCheck", "EnforceTypeConversionRules", "OmitDefaultLibName", "SpectreMitigation", "WholeProgramOptimization"}) require_off(item, field);
    const auto pch = item.get("PrecompiledHeader");
    if (!pch.empty() && pch != "NotUsing") throw std::runtime_error("precompiled-header context unsupported; provide a project configuration with PrecompiledHeader=NotUsing");
    for (const auto* field : {"ForcedUsingFiles", "AdditionalUsingDirectories", "SourceDependencies", "ModuleScanSource"})
        if (!item.get(field).empty()) throw std::runtime_error("unsupported semantic MSBuild metadata: " + std::string(field));
    if (const auto value = item.get("EnableVectorLength"); !value.empty() && value != "NotSet")
        throw std::runtime_error("unsupported EnableVectorLength context");
}
void include_paths(std::vector<std::string>& args, const std::string& value, const std::string& prefix,
                   const fs::path& cwd, bool require_directories = false) {
    for (auto part : list(value)) {
        if (part.size() >= 2 && part.front() == '"' && part.back() == '"') part = part.substr(1, part.size() - 2);
        resolved(part, "include directory");
        auto path = path_from_utf8(part); if (path.is_relative()) path = cwd / path;
        path = path.lexically_normal();
        if (require_directories && !fs::is_directory(path)) throw std::runtime_error("selected toolset/SDK include directory unavailable: " + text(path));
        args.push_back(prefix + text(path));
    }
}
void write_database(const fs::path& output, const std::string& data) {
    auto temporary = output;
    temporary += L".cpprails-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".tmp";
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove(path, error); } } cleanup{temporary};
    detail::write_file(temporary, data);
    if (!MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot atomically replace compilation database: " + text(output));
}
std::string compiler_version(const fs::path& compiler) {
    DWORD ignored = 0;
    const auto bytes = GetFileVersionInfoSizeW(compiler.c_str(), &ignored);
    if (!bytes || bytes > 1024 * 1024) throw std::runtime_error("selected MSVC compiler version resource unavailable");
    std::vector<unsigned char> resource(bytes);
    if (!GetFileVersionInfoW(compiler.c_str(), 0, bytes, resource.data())) throw std::runtime_error("cannot read selected compiler version resource");
    VS_FIXEDFILEINFO* info = nullptr; UINT size = 0;
    if (!VerQueryValueW(resource.data(), L"\\", reinterpret_cast<void**>(&info), &size) || !info ||
        size < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xfeef04bd || HIWORD(info->dwFileVersionMS) != 19)
        throw std::runtime_error("unsupported selected compiler version resource");
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(info->dwFileVersionLS));
}
#endif
}
std::string msvc_compiler_version(const fs::path& compiler) {
#ifdef _WIN32
    return compiler_version(compiler);
#else
    (void)compiler;
    throw std::runtime_error("reading selected MSVC compiler version requires Windows");
#endif
}
void export_msvc_compilation_database(const fs::path& input, const std::string& config,
                                      const std::string& platform, const fs::path& destination, Report& report) {
    try {
        if (!valid_config(config) || (platform != "x64" && platform != "Win32" && platform != "ARM64"))
            throw std::runtime_error("explicit valid Configuration and Platform (x64, Win32, ARM64) are required");
        if (!fs::is_regular_file(input) || input.extension() != ".vcxproj") throw std::runtime_error("existing .vcxproj required: " + text(input));
        if (destination.empty() || destination.extension() != ".json") throw std::runtime_error("compilation database output must be an explicit .json file");
        const auto project = fs::canonical(input);
        const auto output = fs::weakly_canonical(fs::absolute(destination));
        std::error_code error;
        if (project == output || (fs::exists(output) && fs::equivalent(project, output, error))) throw std::runtime_error("database output cannot replace the source project");
#ifndef _WIN32
        report.gate("msvc-compdb", Status::Unverified, "Visual Studio MSBuild export requires Windows and installed Visual Studio C++ tools");
#else
        if (!environment(L"CL").empty() || !environment(L"_CL_").empty())
            throw std::runtime_error("ambient CL/_CL_ compiler options are unsupported; use an environment with those variables unset");
        const auto msbuild = find_msbuild(); Temp temporary;
        const auto result_path = temporary.path / "evaluation.json";
        evaluate(msbuild, project, config, platform, result_path, temporary.path / "evaluation.log");
        const auto data = bounded_read(result_path);
        const auto evaluated = Parser(data).parse();
        const auto& properties = evaluated.at("Properties");
        for (const auto& [key, value] : properties.object()) resolved(value.string(), key);
        const auto& items = evaluated.at("Items");
        bool configuration_exists = false;
        for (const auto& item : items.at("ProjectConfiguration").array())
            if (item.get("Identity") == config + "|" + platform) configuration_exists = true;
        if (!configuration_exists || properties.get("Configuration") != config || properties.get("Platform") != platform)
            throw std::runtime_error("requested configuration/platform is not present in evaluated ProjectConfiguration");
        for (const auto* field : {"CLRSupport", "UseEnv", "UseOfMfc", "UseOfAtl"}) require_off(properties, field);
        if (!properties.get("CLToolExe").empty() || !properties.get("CLToolPath").empty()) throw std::runtime_error("custom compiler executable/path is unsupported");
        const auto toolset = properties.get("PlatformToolset");
        if (toolset != "v143" && toolset != "v142" && toolset != "v141" && toolset != "v145" && toolset != "v144")
            throw std::runtime_error("unsupported or missing evaluated MSVC PlatformToolset: " + toolset);
        const auto version = properties.get("VCToolsVersion");
        if (!version.starts_with("14.") || !std::ranges::all_of(version, [](unsigned char c) { return std::isdigit(c) || c == '.'; }))
            throw std::runtime_error("missing or unsupported evaluated VCToolsVersion");
        const auto compiler = path_from_utf8(properties.get("VCToolsInstallDir")) / "bin" / "Hostx64" /
                              (platform == "Win32" ? "x86" : platform == "ARM64" ? "arm64" : "x64") / "cl.exe";
        if (properties.get("VCToolsInstallDir").empty() || !fs::is_regular_file(compiler)) throw std::runtime_error("selected MSVC toolset compiler unavailable: " + text(compiler));
        const auto actual_compiler_version = msvc_compiler_version(compiler);
        if (properties.get("WindowsSdkDir").empty() || properties.get("WindowsTargetPlatformVersion").empty() ||
            properties.get("VC_IncludePath").empty() || properties.get("WindowsSDK_IncludePath").empty() || properties.get("IncludePath").empty())
            throw std::runtime_error("evaluated Windows SDK/toolset include context is missing");
        std::vector<std::string> checked;
        include_paths(checked, properties.get("WindowsSDK_IncludePath"), "/I", project.parent_path(), true);
        // Optional ATL/test-support include entries may be absent in a valid VC installation.
        const auto vc_include = path_from_utf8(properties.get("VCToolsInstallDir")) / "include";
        if (!fs::is_regular_file(vc_include / "vcruntime.h")) throw std::runtime_error("selected VC headers unavailable");
        const auto sdk_version = properties.get("TargetPlatformVersion");
        if (sdk_version.empty() || properties.get("UniversalCRT_IncludePath").empty() || properties.get("TargetUniversalCRTVersion").empty())
            throw std::runtime_error("evaluated Windows SDK/UCRT version missing");
        bool ucrt_found = false;
        for (const auto& include : list(properties.get("UniversalCRT_IncludePath")))
            if (fs::is_regular_file(path_from_utf8(include) / "corecrt.h")) ucrt_found = true;
        if (!ucrt_found) throw std::runtime_error("selected Windows SDK UCRT headers unavailable");
        const auto target = platform == "Win32" ? "i686-pc-windows-msvc" : platform == "ARM64" ? "aarch64-pc-windows-msvc" : "x86_64-pc-windows-msvc";
        std::ostringstream database; database << "[\n"; std::size_t count = 0, excluded = 0;
        std::set<fs::path> sources;
        for (const auto& item : items.at("ClCompile").array()) {
            const auto exclusion = lower(item.get("ExcludedFromBuild"));
            if (exclusion == "true") { ++excluded; continue; }
            if (!exclusion.empty() && exclusion != "false") throw std::runtime_error("invalid ExcludedFromBuild metadata");
            validate_metadata(item);
            auto source = path_from_utf8(item.at("FullPath").string());
            if (!source.is_absolute() || !fs::is_regular_file(source)) throw std::runtime_error("evaluated source does not exist; export does not run generation targets: " + text(source));
            source = fs::canonical(source);
            if (!sources.insert(source).second) throw std::runtime_error("ambiguous duplicate evaluated ClCompile item: " + text(source));
            std::vector<std::string> args{"clang-cl", "/nologo", "/X", "/clang:--target=" + std::string(target), "/clang:-fms-compatibility-version=" + actual_compiler_version};
            const auto language = item.get("CompileAs");
            if (language.empty() || language == "Default") args.push_back(lower(source.extension().string()) == ".c" ? "/TC" : "/TP");
            else map_option(item, "CompileAs", {{"CompileAsC", "/TC"}, {"CompileAsCpp", "/TP"}}, args);
            if (args.back() == "/TC") map_option(item, "LanguageStandard_C", {{"", ""}, {"Default", ""}, {"stdc11", "/std:c11"}, {"stdc17", "/std:c17"}}, args);
            else map_option(item, "LanguageStandard", {{"", "/std:c++14"}, {"Default", "/std:c++14"}, {"stdcpp14", "/std:c++14"}, {"stdcpp17", "/std:c++17"}, {"stdcpp20", "/std:c++20"}, {"stdcpp23", "/std:c++23preview"}, {"stdcpp23preview", "/std:c++23preview"}, {"stdcpplatest", "/std:c++latest"}}, args);
            map_option(item, "RuntimeLibrary", {{"", "/MT"}, {"MultiThreaded", "/MT"}, {"MultiThreadedDebug", "/MTd"}, {"MultiThreadedDLL", "/MD"}, {"MultiThreadedDebugDLL", "/MDd"}}, args);
            map_option(item, "ExceptionHandling", {{"", "/EHs-c-"}, {"false", "/EHs-c-"}, {"Sync", "/EHsc"}, {"Async", "/EHa"}, {"SyncCThrow", "/EHs"}}, args);
            boolean_option(item, "RuntimeTypeInfo", "/GR", "/GR-", args);
            boolean_option(item, "TreatWChar_tAsBuiltInType", "/Zc:wchar_t", "/Zc:wchar_t-", args);
            boolean_option(item, "ForceConformanceInForLoopScope", "/Zc:forScope", "/Zc:forScope-", args);
            map_option(item, "ConformanceMode", {{"", ""}, {"Default", ""}, {"true", "/permissive-"}, {"false", ""}}, args);
            map_option(item, "DisableLanguageExtensions", {{"", ""}, {"false", ""}, {"true", "/Za"}}, args);
            map_option(item, "CallingConvention", {{"", ""}, {"Cdecl", "/Gd"}, {"FastCall", "/Gr"}, {"StdCall", "/Gz"}, {"VectorCall", "/Gv"}}, args);
            map_option(item, "StructMemberAlignment", {{"", ""}, {"Default", ""}, {"1Byte", "/Zp1"}, {"2Bytes", "/Zp2"}, {"4Bytes", "/Zp4"}, {"8Bytes", "/Zp8"}, {"16Bytes", "/Zp16"}}, args);
            map_option(item, "EnableEnhancedInstructionSet", {{"", ""}, {"NotSet", ""}, {"NoExtensions", "/arch:IA32"}, {"StreamingSIMDExtensions", "/arch:SSE"}, {"StreamingSIMDExtensions2", "/arch:SSE2"}, {"AdvancedVectorExtensions", "/arch:AVX"}, {"AdvancedVectorExtensions2", "/arch:AVX2"}, {"AdvancedVectorExtensions512", "/arch:AVX512"}}, args);
            map_option(item, "Optimization", {{"", ""}, {"Disabled", "/Od"}, {"MinSpace", "/O1"}, {"MaxSpeed", "/O2"}, {"Full", "/Ox"}}, args);
            map_option(item, "FloatingPointModel", {{"", ""}, {"Precise", "/fp:precise"}, {"Strict", "/fp:strict"}, {"Fast", "/fp:fast"}}, args);
            boolean_option(item, "IntrinsicFunctions", "/Oi", "/Oi-", args);
            for (const auto& define : list(item.get("PreprocessorDefinitions"))) args.push_back("/D" + define);
            for (const auto& undefine : list(item.get("UndefinePreprocessorDefinitions"))) args.push_back("/U" + undefine);
            for (const auto& forced : list(item.get("ForcedIncludeFiles"))) args.push_back("/FI" + forced);
            include_paths(args, item.get("AdditionalIncludeDirectories"), "/I", project.parent_path());
            include_paths(args, item.get("AdditionalExternalIncludeDirectories"), "/external:I", project.parent_path());
            include_paths(args, item.get("ExternalIncludeDirectories"), "/external:I", project.parent_path());
            if (lower(item.get("IgnoreStandardIncludePath")) != "true") {
                include_paths(args, properties.get("IncludePath"), "/I", project.parent_path());
                include_paths(args, properties.get("ExternalIncludePath"), "/external:I", project.parent_path());
            }
            additional_options(item.get("AdditionalOptions"), args);
            args.push_back(text(source));
            if (count++) database << ",\n";
            database << "{\"directory\":" << json_quote(text(project.parent_path())) << ",\"file\":" << json_quote(text(source)) << ",\"arguments\":[";
            for (std::size_t i = 0; i != args.size(); ++i) { if (i) database << ','; database << json_quote(args[i]); }
            database << "]}";
        }
        if (!count) throw std::runtime_error("evaluated project has no included ClCompile translation units");
        database << "\n]\n";
        write_database(output, database.str());
        report.notes.push_back("Explicit trusted-project MSBuild evaluation only; no targets, build, restore, downloads or source generation requested. Project evaluation can execute property functions and imported project code.");
        report.notes.push_back("Exported reconstructed Clang analysis context, not original MSVC invocations; evaluation does not capture target-time metadata changes or produce generated sources. Missing source files and unsupported evaluated options fail closed. Source projects and imports are not modified by the exporter.");
        const auto provenance = "{\"project\":" + json_quote(text(project)) + ",\"configuration\":" + json_quote(config) + ",\"platform\":" + json_quote(platform) + ",\"msbuild\":" + json_quote(text(msbuild)) + ",\"toolset\":" + json_quote(toolset) + ",\"vc_tools_version\":" + json_quote(version) + ",\"msvc_compiler_version\":" + json_quote(actual_compiler_version) + ",\"msvc_compiler\":" + json_quote(text(compiler)) + ",\"windows_sdk_version\":" + json_quote(sdk_version) + ",\"reconstructed_clang_context\":true,\"original_invocations\":false,\"targets_executed\":false,\"translation_units\":" + std::to_string(count) + ",\"excluded_translation_units\":" + std::to_string(excluded) + "}";
        report.sections.push_back({"msvc-compdb-provenance", provenance, "MSBuild evaluated " + config + "|" + platform + "; MSVC " + actual_compiler_version + ", SDK " + sdk_version + "; " + std::to_string(count) + " Clang analysis entries."});
        report.gate("msvc-compdb", Status::Pass, "Exported " + std::to_string(count) + " reconstructed Clang analysis entries to " + text(output) + "; " + std::to_string(excluded) + " excluded ClCompile items");
#endif
    } catch (const std::exception& exception) {
        report.gate("msvc-compdb", Status::Unverified, exception.what());
    }
}
}
