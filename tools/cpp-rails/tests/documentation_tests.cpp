#include "cpp_rails.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cpprails;
static std::string read(const fs::path& path){std::ifstream in(path);if(!in)throw std::runtime_error("cannot read "+path.string());return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("skill root required");const fs::path root=path_from_utf8(argv[1]);
    auto version=read(root/"VERSION");while(!version.empty()&&(version.back()=='\r'||version.back()=='\n'))version.pop_back();
    if(version.empty()||version.find_first_not_of("0123456789.")!=std::string::npos)throw std::runtime_error("invalid version source");
    for(auto name:{"README.md","README.es.md","SKILL.md"}){
        const auto text=read(root/name);if(text.find(version)==std::string::npos||text.find("C++23")==std::string::npos)throw std::runtime_error(std::string(name)+" version/standard drift");
    }
    if(sarif(Report{}).find("\"version\":\""+version+"\"")==std::string::npos)throw std::runtime_error("SARIF tool version drift");
    for(auto name:{"CMakeLists.txt","tools/cpp-rails/CMakeLists.txt"})if(read(root/name).find("version.cmake")==std::string::npos)throw std::runtime_error("CMake version source drift");
    std::cout<<"version, C++23 and generated CLI metadata agree: "<<version<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
