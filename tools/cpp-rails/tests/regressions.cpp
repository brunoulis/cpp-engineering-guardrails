#include "cpp_rails.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace cpprails;
static void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
int main(int argc,char** argv) {
    if(argc>1&&std::string(argv[1])=="--emit-streams") {
        for(int i=0;i<80;++i){std::cout<<std::string(4096,'o')<<std::flush;std::cerr<<std::string(4096,'e')<<std::flush;}
        return 0;
    }
    if(argc>1&&std::string(argv[1])=="--emit-small") {std::cout<<"stdout";std::cerr<<"stderr";return 0;}
    const auto temp=fs::temp_directory_path()/("cpp-rails-regressions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp);
    int failures=0;
    auto test=[&](const char* name, const std::function<void()>& run) {
        try {run();std::cout<<name<<": PASS\n";} catch(const std::exception& e) {++failures;std::cerr<<name<<": FAIL: "<<e.what()<<'\n';}
    };
    test("subprocess drains bounded independent streams",[&] {
        const auto exe=utf8_bytes(fs::absolute(path_from_utf8(argv[0])).u8string());
        const auto result=exec({exe,"--emit-streams"},temp,true);
        require(result.started&&result.exit_code==0,"dual pipe child did not finish");
        require(result.output==std::string(128*1024,'o')&&result.error_output==std::string(128*1024,'e'),"stream contamination or output cap regression");
        const auto combined=exec({exe,"--emit-small"},temp);
        require(combined.started&&combined.exit_code==0&&combined.output.find("stdout")!=std::string::npos&&combined.output.find("stderr")!=std::string::npos,"legacy combined capture changed");
    });
    test("digit separators preserve following findings and lines",[&] {
        const auto dir=temp/"digits";fs::create_directories(dir);
        for(const auto& number:{"1000","1'000","0xAB'CD","0b10'01","1.2'34e+1'0"}) {
            std::ofstream(dir/"source.cpp")<<"auto n = "<<number<<";\nvoid f(){ auto p = new int; delete p; }\nchar c = '\\'';\nconst char* s = \"new ignored\";\n";
            const auto result=scan(dir);
            require(result.findings.size()==1 && result.findings[0].line==2,"lost/relocated finding after "+std::string(number));
        }
        std::ofstream(dir/"source.cpp")<<"char c = '\\''; auto ascii = u8'x';\nconst char* s = \"new ignored \\\ncontinued\";\nvoid f(){ auto p = new int; delete p; }\n";
        const auto escaped=scan(dir);
        require(escaped.findings.size()==1&&escaped.findings[0].line==4,"escaped characters/continued string changed line accounting");
    });
    test("changed root/subdir UTF-8 untracked rename deletion",[&] {
        const auto dir=temp/fs::path(u8"git ni\u00f1o");fs::create_directories(dir/"src");
        auto git=[&](std::vector<std::string> args) {args.insert(args.begin(),"git");auto p=exec(args,dir);require(p.started&&p.exit_code==0,p.output);};
        git({"init","--quiet"});
        for(const auto& name:std::vector<std::string>{"src/worker.cpp",utf8_bytes(u8"src/ni\u00f1o.cpp"),"src/old.cpp","src/deleted.cpp"}) std::ofstream(dir/path_from_utf8(name))<<"void f() {}\n";
        git({"add","."});git({"-c","user.name=Fixture","-c","user.email=fixture@example.invalid","commit","--quiet","-m","fixture"});
        git({"config","core.autocrlf","true"}); // Git warns on stderr for modified LF files.
        for(const auto& name:std::vector<std::string>{"src/worker.cpp",utf8_bytes(u8"src/ni\u00f1o.cpp"),"src/untracked space.cpp"}) std::ofstream(dir/path_from_utf8(name),std::ios::binary)<<"void f(){auto p=new int;delete p;}\n";
        git({"mv","src/old.cpp","src/renamed.cpp"});fs::remove(dir/"src/deleted.cpp");
        std::vector<std::string> notes;
        const auto root=changed_paths(dir,&notes), sub=changed_paths(dir/"src");
        require(!notes.empty(),"Git warnings must remain available to humans");
        require(root.count(utf8_bytes(u8"src/ni\u00f1o.cpp"))==1 && sub.count(utf8_bytes(u8"ni\u00f1o.cpp"))==1,"Unicode root normalization");
        require(sub.count("worker.cpp")&&sub.count("untracked space.cpp")&&sub.count("renamed.cpp")&&sub.count("deleted.cpp"),"incomplete change set");
        require(scan(dir/"src",sub).files.size()==4,"changed scan omitted existing files or scanned deleted path");
    });
    // NONE isolates test-result handling from compiler/sanitizer availability. These
    // exercise every gate dispatch path, not actual instrumentation correctness.
    for(const auto& scenario:{"none","skip","skip-return","disabled","partial","failure","success"}) test(scenario,[&] {
        const std::string name=scenario;const auto dir=temp/name;fs::create_directories(dir);
        std::ofstream cmake(dir/"CMakeLists.txt");
        cmake<<"cmake_minimum_required(VERSION 3.16)\nproject(result_fixture NONE)\nenable_testing()\n";
        if(name=="skip"||name=="partial") cmake<<"add_test(NAME skipped COMMAND ${CMAKE_COMMAND} -E echo skip)\nset_tests_properties(skipped PROPERTIES SKIP_REGULAR_EXPRESSION skip)\n";
        if(name=="disabled") cmake<<"add_test(NAME disabled COMMAND ${CMAKE_COMMAND} -E echo yes)\nset_tests_properties(disabled PROPERTIES DISABLED TRUE)\n";
        if(name=="skip-return") cmake<<"add_test(NAME skipped_return COMMAND ${CMAKE_COMMAND} -E false)\nset_tests_properties(skipped_return PROPERTIES SKIP_RETURN_CODE 1)\n";
        if(name=="partial"||name=="success") cmake<<"add_test(NAME executed COMMAND ${CMAKE_COMMAND} -E echo \"<Test>misleading output</Test>\")\n";
        if(name=="failure") cmake<<"add_test(NAME failed COMMAND ${CMAKE_COMMAND} -E false)\n";
        cmake.close();
        Options opt;opt.root=dir;opt.check_program=true;opt.configure=true;opt.sanitizer="asan-ubsan";opt.compiler_matrix="c++";
        Report report;dispatch(opt,report);
        const auto expected=name=="success"?Status::Pass:name=="failure"?Status::Fail:Status::Unverified;
        for(const auto& gateName:{"CTest","asan-ubsan tests","c++ matrix tests"}) {
            bool seen=false;for(const auto& g:report.gates) if(g.name==gateName) {seen=true;require(g.status==expected,g.name+" returned "+status_text(g.status)+"; "+g.details);}
            require(seen,std::string("missing gate ")+gateName);
        }
        opt.sanitizer="tsan";opt.compiler_matrix.clear();Report tsan;dispatch(opt,tsan);
        bool seen=false;for(const auto& g:tsan.gates) if(g.name=="tsan tests") {seen=true;require(g.status==expected,"tsan result");}require(seen,"tsan gate missing");
    });
    fs::remove_all(temp);
    return failures?1:0;
}
