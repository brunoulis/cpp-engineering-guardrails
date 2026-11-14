#include "cpp_rails.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <regex>
#include <sstream>

static bool missing_insert(const cpprails::Process& result, bool msvc) {
    if(!result.started||result.exit_code==0)return false;
    std::istringstream lines(result.output);std::string line;
    // Examine only primary error diagnostics, never paths, source echoes or notes.
    const std::regex diagnostic(msvc?R"(^.*\([0-9]+(?:,[0-9]+)?\): error (C[0-9]+): (.*)$)":R"(^.*:[0-9]+(?::[0-9]+)?: (?:fatal )?error: (.*)$)");
    while(std::getline(lines,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::smatch match;if(!std::regex_match(line,match,diagnostic))continue;
        if(msvc&&match[1]!="C2039")continue;
        const auto message=match[msvc?2:1].str();
        const bool symbol=message.find("\"insert\"")!=std::string::npos||message.find("'insert'")!=std::string::npos||message.find(u8"\u2018insert\u2019")!=std::string::npos;
        if(symbol&&message.find("array")!=std::string::npos&&
           (msvc||message.find("no member named")!=std::string::npos||message.find("has no member")!=std::string::npos))return true;
    }
    return false;
}

// A native C++ fixture harness. A negative compile counts only if the compiler
// reports the *expected* missing insertion operation, not just any failure.
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: cpp-rails-fixtures CXX FIXTURES_DIR\n";
        return 2;
    }
    namespace fs=std::filesystem;
    const std::string compiler=argv[1];
    const fs::path dir=fs::absolute(argv[2]);
    const auto exe=fs::path(compiler).filename().string();
    const bool msvc=exe=="cl"||exe=="cl.exe";
    const auto work=fs::current_path();
    auto compile=[&](const std::string& name) {
        std::vector<std::string> args;
        if(msvc)args={compiler,"/nologo","/std:c++20","/Zs",(dir/name).u8string()};
        else args={compiler,"-std=c++20","-Wall","-Wextra","-pedantic","-fsyntax-only","-fdiagnostics-color=never",(dir/name).u8string()};
        return cpprails::exec(args,work);
    };
    auto positive=compile("concepts_positive.cpp");
    if(!positive.started||positive.exit_code!=0){
        std::cerr << "positive concepts fixture failed: " << positive.output << '\n';return 1;
    }
    auto negative=compile("concepts_negative.cpp");
    const auto output=negative.output;
    if(!negative.started||negative.exit_code==0){
        std::cerr << "negative concepts fixture did not fail as expected\n";return 1;
    }
    if(!missing_insert(negative,msvc)) {
        std::cerr << "negative fixture failed for unrelated reason: " << output << '\n';return 1;
    }
    // Real compiler failures must not satisfy the negative fixture just because
    // their filename or echoed source contains the expected API spelling.
    const auto scratch=fs::temp_directory_path()/("cpp-rails-insert-array-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch);
    for(const auto& source:{"#error insert array unrelated failure\n","#include <insert_array_missing_fixture_header.h>\n"}) {
        const auto file=scratch/"insert_array.cpp";std::ofstream(file)<<source;
        const auto unrelated=compile(file.u8string());
        if(!unrelated.started||unrelated.exit_code==0||missing_insert(unrelated,msvc)) {
            std::cerr<<"negative harness accepted unrelated failure: "<<unrelated.output<<'\n';fs::remove_all(scratch);return 1;
        }
    }
    fs::remove_all(scratch);
    auto lifetime=compile("lifetime_positive.cpp");
    if(!lifetime.started||lifetime.exit_code!=0){
        std::cerr << "lifetime positive fixture failed: " << lifetime.output << '\n';return 1;
    }
    std::cout << "Concrete positive and negative template compilations PASS; positive lifetime fixture PASS\n";
    return 0;
}
