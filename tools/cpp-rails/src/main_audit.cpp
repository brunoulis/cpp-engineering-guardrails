#include "cpp_rails.hpp"
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> utf8;
    for(int i=0;i<argc;++i)utf8.push_back(std::filesystem::path(argv[i]).u8string());
    std::vector<const char*> args;for(const auto& arg:utf8)args.push_back(arg.c_str());
    return cpprails::cli_entry(false,argc,args.data());
}
#else
int main(int argc, char** argv) { return cpprails::cli_entry(false, argc, const_cast<const char* const*>(argv)); }
#endif
