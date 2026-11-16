#include "msvc.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace cpprails;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void put(const fs::path& path, const std::string& value) { std::ofstream(path, std::ios::binary) << value; }
std::string read(const fs::path& path) { std::ifstream stream(path, std::ios::binary); return {std::istreambuf_iterator<char>(stream), {}}; }
bool passed(const Report& report) {
    return std::ranges::any_of(report.gates, [](const auto& gate) { return gate.name == "msvc-compdb" && gate.status == Status::Pass; });
}
std::string details(const Report& report) {
    std::string result; for (const auto& gate : report.gates) result += gate.details + '\n'; return result;
}
struct Temp {
    fs::path path = fs::temp_directory_path() / ("cpprails-msvc-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { fs::create_directory(path); }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
}
int main() try {
    Temp temp;
    const auto output = temp.path / "compile_commands.json";
    put(output, "preserve existing database");
    Report missing;
    export_msvc_compilation_database(temp.path / "missing.vcxproj", "Release", "x64", output, missing);
    require(!passed(missing) && (missing.required_unverified() || missing.has_failure()), "missing project must fail closed");
    require(read(output) == "preserve existing database", "failure must preserve existing output");
    const auto project = temp.path / "real.vcxproj";
    put(project, "<Project />");
    for (const auto& config : {std::string{}, std::string{"Release;Other=unsafe"}, std::string{"Release,Other=unsafe"}}) {
        Report invalid;
        export_msvc_compilation_database(project, config, "x64", output, invalid);
        require(!passed(invalid) && read(output) == "preserve existing database", "invalid configuration must preserve output");
    }
#ifdef _WIN32
    put(temp.path / "forced header.hpp", "#define FROM_FORCED_HEADER 1\n");
    put(temp.path / "first source.cpp", "int first;\n");
    put(temp.path / "second.cpp", "int second;\n");
    put(temp.path / "excluded.cpp", "this source must be excluded\n");
    put(temp.path / "inherited.props", R"xml(<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>FROM_IMPORTED_PROPS=1;%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalOptions>/external:I &quot;$(MSBuildProjectDirectory)&quot; /Zc:__cplusplus /DADDITIONAL_OPTION=1 &quot;/DQUOTED_OPTION=&quot;&quot;literal&quot;&quot;&quot; %(AdditionalOptions)</AdditionalOptions></ClCompile></ItemDefinitionGroup></Project>)xml");
    const std::string fixture = R"xml(<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<ItemGroup Label="ProjectConfigurations"><ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration></ItemGroup>
<PropertyGroup Label="Globals"><ProjectGuid>{CE58F6E4-BA14-4565-8106-B555C3594920}</ProjectGuid><WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion><VcpkgEnabled>false</VcpkgEnabled></PropertyGroup>
<Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
<PropertyGroup Label="Configuration"><ConfigurationType>StaticLibrary</ConfigurationType><CharacterSet>Unicode</CharacterSet><PlatformToolset>$(DefaultPlatformToolset)</PlatformToolset></PropertyGroup>
<Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
<Import Project="inherited.props" />
<ItemDefinitionGroup><ClCompile><LanguageStandard>stdcpp17</LanguageStandard><RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary><ExceptionHandling>Sync</ExceptionHandling><RuntimeTypeInfo>false</RuntimeTypeInfo><ForcedIncludeFiles>forced header.hpp;%(ForcedIncludeFiles)</ForcedIncludeFiles><PreprocessorDefinitions>FILE_SETTING=&quot;quoted value&quot;;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>
<ItemGroup><ClCompile Include="first source.cpp" /><ClCompile Include="second.cpp"><LanguageStandard>stdcpp20</LanguageStandard><RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary><PreprocessorDefinitions>PER_FILE=1;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile><ClCompile Include="excluded.cpp"><ExcludedFromBuild>true</ExcludedFromBuild></ClCompile></ItemGroup>
<Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
<Target Name="NeverBuildDuringEvaluation" BeforeTargets="Build"><WriteLinesToFile File="build-was-run.txt" Lines="BUG" /></Target>
</Project>)xml";
    put(project, fixture);
    Report evaluated;
    export_msvc_compilation_database(project, "Release", "x64", output, evaluated);
    if (!passed(evaluated)) throw std::runtime_error("real MSBuild export failed: " + details(evaluated));
    const auto database = read(output);
    for (const auto& expected : {"clang-cl", "first source.cpp", "second.cpp", "/std:c++17", "/std:c++20", "/MD", "/MDd", "/GR-", "/EHsc", "FROM_IMPORTED_PROPS=1", "PER_FILE=1", "ADDITIONAL_OPTION=1", "forced header.hpp", "_UNICODE", "Windows Kits", "-fms-compatibility-version="})
        require(database.find(expected) != std::string::npos, (std::string{"evaluated context missing "} + expected).c_str());
    require(database.find("excluded.cpp") == std::string::npos, "per-file exclusion must be preserved");
    require(database.find("/DQUOTED_OPTION=\\\"literal\\\"") != std::string::npos, "Windows CRT doubled quotes in AdditionalOptions must preserve string-literal macro values");
    require(!fs::exists(temp.path / "build-was-run.txt"), "export must not execute build targets");
    const auto& provenance = evaluated.sections.at(0).json;
    const auto field = [&](const std::string& key) {
        const auto start = provenance.find('"' + key + "\":\"");
        require(start != std::string::npos, "missing compiler provenance");
        const auto begin = start + key.size() + 4;
        return provenance.substr(begin, provenance.find('"', begin) - begin);
    };
    const auto compiler = field("msvc_compiler");
    const auto compiler_version = field("msvc_compiler_version");
    put(temp.path / "macro-probe.cpp", "MSC_FULLVER_CHECK _MSC_FULL_VER\n");
    const auto native = exec({compiler, "/nologo", "/EP", "macro-probe.cpp"}, temp.path);
    require(native.started && native.exit_code == 0, "selected native compiler macro probe must run");
    unsigned major = 0, minor = 0, patch = 0; char first_dot = 0, second_dot = 0;
    std::istringstream version_input(compiler_version);
    version_input >> major >> first_dot >> minor >> second_dot >> patch;
    require(first_dot == '.' && second_dot == '.' && version_input && version_input.peek() == std::char_traits<char>::eof(), "compiler version must have three numeric components");
    const auto full_version = std::to_string((major * 100 + minor) * 100000 + patch);
    require(native.output.find("MSC_FULLVER_CHECK " + full_version) != std::string::npos, "actual _MSC_FULL_VER must agree with exported compatibility version, independent of toolset folder version");
    require(database.find("/clang:-fms-compatibility-version=" + compiler_version) != std::string::npos, "database must use actual selected compiler version");
    const auto cli_database = temp.path / "cli-compile_commands.json";
    std::vector<std::string> cli_args{"cpp-rails-audit", "compdb", "--root", utf8_bytes(temp.path.u8string()), "--msvc-project", utf8_bytes(project.u8string()), "--config", "Release", "--platform", "x64", "--output", utf8_bytes(cli_database.u8string())};
    std::vector<const char*> cli_argv;
    for (const auto& arg : cli_args) cli_argv.push_back(arg.c_str());
    require(cli_entry(false, static_cast<int>(cli_argv.size()), cli_argv.data()) == 0, "real CLI export must succeed");
    const auto cli_contents = read(cli_database);
    require(cli_contents.starts_with('[') && cli_contents.find("\"arguments\"") != std::string::npos, "CLI report rendering must not overwrite the exported database");
    cli_args[7] = "DoesNotExist";
    cli_argv.clear(); for (const auto& arg : cli_args) cli_argv.push_back(arg.c_str());
    require(cli_entry(false, static_cast<int>(cli_argv.size()), cli_argv.data()) != 0 && read(cli_database) == cli_contents, "CLI export failure must preserve existing database");
    const auto sentinel = database;
    for (const auto* variable : {L"CL", L"_CL_"}) {
        require(SetEnvironmentVariableW(variable, L"/DUNRECORDED_ENVIRONMENT=1") != 0, "must set isolated compiler-option environment regression");
        Report environment_options;
        export_msvc_compilation_database(project, "Release", "x64", output, environment_options);
        require(SetEnvironmentVariableW(variable, nullptr) != 0, "must restore compiler-option environment");
        require(!passed(environment_options) && details(environment_options).find("CL/_CL_") != std::string::npos && read(output) == sentinel, "ambient compiler options must fail closed");
    }
    Report bad_config;
    export_msvc_compilation_database(project, "DoesNotExist", "x64", output, bad_config);
    require(!passed(bad_config) && read(output) == sentinel, "unknown project configuration must fail without clobbering output");
    auto unsupported = fixture;
    const auto at = unsupported.find("<RuntimeTypeInfo>false</RuntimeTypeInfo>");
    unsupported.insert(at, "<AdditionalOptions>/someUnsupportedSemanticOption</AdditionalOptions>");
    put(project, unsupported);
    Report rejected;
    export_msvc_compilation_database(project, "Release", "x64", output, rejected);
    require(!passed(rejected) && read(output) == sentinel, "unknown compiler semantics must fail closed");
    unsupported = fixture;
    unsupported.insert(at, "<BasicRuntimeChecks>EnableFastChecks</BasicRuntimeChecks>");
    put(project, unsupported);
    Report runtime_checks;
    export_msvc_compilation_database(project, "Release", "x64", output, runtime_checks);
    require(!passed(runtime_checks) && details(runtime_checks).find("BasicRuntimeChecks") != std::string::npos && read(output) == sentinel, "active MSVC runtime-check preprocessing must fail closed");
    unsupported = fixture;
    unsupported.insert(at, "<SmallerTypeCheck>true</SmallerTypeCheck>");
    put(project, unsupported);
    Report smaller_type_checks;
    export_msvc_compilation_database(project, "Release", "x64", output, smaller_type_checks);
    require(!passed(smaller_type_checks) && details(smaller_type_checks).find("SmallerTypeCheck") != std::string::npos && read(output) == sentinel, "active /RTCc preprocessing must fail closed");
    unsupported = fixture;
    const auto definitions = unsupported.find("<PreprocessorDefinitions>FILE_SETTING");
    require(definitions != std::string::npos, "fixture preprocessing metadata must exist");
    unsupported.insert(definitions + std::string{"<PreprocessorDefinitions>"}.size(), "SEMICOLON=&quot;a%3Bb&quot;;");
    put(project, unsupported);
    Report escaped_list;
    export_msvc_compilation_database(project, "Release", "x64", output, escaped_list);
    require(!passed(escaped_list) && read(output) == sentinel, "lossy escaped-semicolon macro lists must fail closed");
    put(project, fixture);
    Report protected_project;
    export_msvc_compilation_database(project, "Release", "x64", project, protected_project);
    require(!passed(protected_project) && read(project) == fixture, "output must never replace the source project");
    std::cout << "PASS real MSBuild evaluation, imported/per-file settings, exclusions, SDK/toolset context, no build, fail-closed export\n";
#else
    std::cout << "PASS portable invalid-input/output-preservation contracts; real Windows MSBuild evaluation N/A\n";
#endif
    return 0;
} catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
