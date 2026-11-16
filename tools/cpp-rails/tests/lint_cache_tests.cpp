#include "lint.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cpprails;
namespace {
void put(const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream(p)<<s;}
std::string q(const fs::path& p){std::string s="\"";for(char c:utf8_bytes(p.generic_u8string())){if(c=='\\'||c=='"')s+='\\';s+=c;}return s+'"';}
void require(bool b,const char* text){if(!b)throw std::runtime_error(text);}
}
int main(int argc,char** argv){
    if(argc!=3){std::cout<<"Real Clang + clang-tidy required for dependency cache verification\n";return 77;}
    const auto temp=fs::temp_directory_path()/("cpprails-lint-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root=temp/"project", external=temp/"external";int result=0;
    try{
        put(root/"local.hpp","inline int value(){return 1;}\n");put(external/"external.hpp","inline int other(){return 2;}\n");
        put(root/"selected.hpp","inline int selected(){return 3;}\n");
        put(root/"unit.cpp","#include \"local.hpp\"\n#include \"external.hpp\"\n#ifdef BUILD\n#include \"selected.hpp\"\n#endif\nint f(){return value()+other();}\n");
        put(root/".clang-tidy","Checks: '-*,bugprone-sizeof-expression'\n");
        auto database=[&](int macro){put(root/"build/compile_commands.json","[{\"directory\":"+q(root)+",\"file\":"+q(root/"unit.cpp")+",\"arguments\":["+q(path_from_utf8(argv[1]))+",\"-std=c++20\",\"-DBUILD\",\"-DVALUE="+std::to_string(macro)+"\",\"-I\","+q(root)+",\"-I\","+q(external)+",\"-c\","+q(root/"unit.cpp")+"]}]");};
        database(1);Options options;options.lint_cache=true;options.clang_executable=argv[1];options.tidy_executable=argv[2];
        {std::ifstream in(root/"build/compile_commands.json");std::string db{std::istreambuf_iterator<char>(in),{}};
            const auto pos=db.find(",\"-DBUILD\"");require(pos!=std::string::npos,"fixture has no conditional flag");db.erase(pos,10);put(root/"compile_commands.json",db);}
        unsigned run_number=0;
        auto run=[&](bool hit,Status status=Status::Pass){++run_number;Report r;run_lint(options,root,{root/"unit.cpp"},r,true);
            if(r.gates.empty()||r.gates.back().status!=status)throw std::runtime_error("run "+std::to_string(run_number)+" "+json(r));
            require(json(r).find(hit?"\"cached\":1":"\"cached\":0")!=std::string::npos,"invalid cache reuse");};
        run(false);run(true);
        put(root/"local.hpp","inline int value(){return 4;}\n");run(false);run(true);
        put(external/"external.hpp","inline int other(){return 6;}\n");run(false);
        database(2);run(false);run(true);
        put(root/"selected.hpp","#error selected build dependency changed\n");run(false,Status::Fail);
        put(root/"selected.hpp","inline int selected(){return 8;}\n");run(false);
        put(root/".clang-tidy","Checks: '-*,bugprone-sizeof-expression'\nWarningsAsErrors: '*'\n");run(false);
        put(root/".clang-tidy","Checks: [invalid\n");run(false,Status::Fail);run(false,Status::Fail);
        std::cout<<"Real dependency cache: local/external headers, compile flags, config and failed runs PASS\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    std::error_code error;fs::remove_all(temp,error);return result;
}
