#include "lint.hpp"
#include "semantic.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace cpprails {
namespace {
std::string bytes(const fs::path& p) {return utf8_bytes(p.u8string());}
std::string read(const fs::path& path) {std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("cannot fingerprint "+bytes(path));return {std::istreambuf_iterator<char>(in),{}};}
std::string digest(const std::string& data) {
    std::uint64_t a=14695981039346656037ULL,b=1099511628211ULL;
    for(unsigned char c:data){a=(a^c)*1099511628211ULL;b=(b+c+1)*0x9e3779b185ebca87ULL;}
    std::ostringstream o;o<<std::hex<<a<<'-'<<b<<'-'<<data.size();return o.str();
}
fs::path resolve(const fs::path& root,const fs::path& path){return path.is_absolute()?path:root/path;}
std::string cache_key(const Options& options,const fs::path& root,const fs::path& unit,const std::string& version,const std::string& database,std::string& reason) {
    std::vector<fs::path> dependencies;
    if(!semantic_dependencies(options,root,unit,dependencies,reason))return {};
    std::sort(dependencies.begin(),dependencies.end());
    std::string key="cpprails-lint-v1\n"+version+'\n'+options.tidy_executable+'\n'+bytes(root)+'\n'+database+'\n';
    // Include configuration in every ancestor .clang-tidy as well as the unit's.
    std::set<fs::path> configs;
    for(auto parent=unit.parent_path();!parent.empty();) {
        configs.insert(parent/".clang-tidy");auto next=parent.parent_path();if(next==parent)break;parent=next;
    }
    for(const auto& dep:dependencies)key+=bytes(dep)+'\n'+digest(read(dep))+'\n';
    for(const auto& config:configs)key+=bytes(config)+'\n'+(fs::exists(config)?read(config):"<absent>")+'\n';
    return digest(key);
}
}
void run_lint(const Options& options,const fs::path& root,const std::vector<fs::path>& files,Report& report,bool required) {
    const auto start=std::chrono::steady_clock::now();
    auto db=resolve(root,options.compilation_database.value_or(options.build_dir.value_or(fs::path("build"))));
    if(fs::is_directory(db))db/="compile_commands.json";
    if(!fs::is_regular_file(db)){report.gate("clang-tidy",Status::Unverified,"compilation database missing",required);return;}
    Options dependency_options=options;
    dependency_options.compilation_database=fs::canonical(db);
    std::vector<fs::path> units;
    for(const auto& path:files){auto ext=bytes(path.extension());if(ext==".cpp"||ext==".cc"||ext==".cxx"||ext==".ixx")units.push_back(path);}
    std::sort(units.begin(),units.end());
    if(units.empty()){report.gate("clang-tidy",Status::NotApplicable,"no C++ translation units selected",false);return;}
    if(!options.lint_batch_size)throw std::runtime_error("lint batch size must be positive");
    const auto limit=options.lint_max_units?std::min<std::size_t>(units.size(),options.lint_max_units):units.size();
    std::size_t analyzed=0,cached=0,batches=0,cache_unknown=0;bool failed=false,unavailable=false;
    std::string version,database;
    if(options.lint_cache){
        const auto tidy=exec({options.tidy_executable,"--version"},root);
        const auto compiler=exec({options.clang_executable,"--version"},root);
        if(tidy.started&&tidy.exit_code==0&&tidy.output.size()<128*1024&&compiler.started&&compiler.exit_code==0&&compiler.output.size()<128*1024)
            version=tidy.output+'\n'+options.clang_executable+'\n'+compiler.output;
        database=read(db);
    }
    const auto cache_dir=root/".cpp-rails/lint-cache";
    for(std::size_t offset=0;offset<limit;offset+=options.lint_batch_size) {
        if(options.lint_budget_seconds&&std::chrono::steady_clock::now()-start>=std::chrono::seconds(options.lint_budget_seconds))break;
        const auto end=std::min(limit,offset+options.lint_batch_size);
        std::vector<std::string> args={options.tidy_executable,"-p",bytes(db.parent_path()),"--warnings-as-errors=*"};
        std::vector<std::string> keys;
        std::size_t misses=0;
        for(auto i=offset;i<end;++i) {
            std::string key,reason;
            if(options.lint_cache&&!version.empty()) {
                try{key=cache_key(dependency_options,root,units[i],version,database,reason);}catch(const std::exception& e){reason=e.what();}
                if(key.empty()){++cache_unknown;if(cache_unknown<4)report.notes.push_back("Lint cache bypass: "+reason);}
            }
            bool hit=false;
            if(!key.empty()&&fs::is_regular_file(cache_dir/(key+".pass"))) {
                try{hit=read(cache_dir/(key+".pass"))==key;}
                catch(const std::exception& error){report.notes.push_back("Unreadable cache entry; running lint: "+std::string(error.what()));}
            }
            if(hit){++cached;++analyzed;continue;}
            args.push_back(bytes(units[i]));keys.push_back(key);++misses;
        }
        if(!misses)continue;
        ++batches;const auto result=exec(args,root);
        if(!result.started){unavailable=true;break;}
        analyzed+=misses;
        // Some clang-tidy releases return zero after malformed YAML and then use
        // defaults. Such a run did not execute the requested configuration.
        const bool invalid_context=result.output.starts_with("Error parsing ")||result.output.find("\nError parsing ")!=std::string::npos||
            result.output.find("Error while trying to load a compilation database")!=std::string::npos||
            result.output.find("Error reading configuration from ")!=std::string::npos;
        if(result.exit_code!=0||invalid_context){failed=true;report.notes.push_back("clang-tidy batch "+std::to_string(batches)+": "+result.output.substr(0,1500));continue;}
        if(result.output.size()>=128*1024){unavailable=true;report.notes.push_back("clang-tidy output reached capture limit; refusing to verify incomplete evidence");continue;}
        for(const auto& key:keys)if(!key.empty()) {
            std::error_code error;fs::create_directories(cache_dir,error);
            if(!error){std::ofstream out(cache_dir/(key+".pass"),std::ios::binary);out<<key;}
        }
    }
    const auto status=failed?Status::Fail:unavailable||analyzed<units.size()?Status::Unverified:Status::Pass;
    const auto details=std::to_string(analyzed)+"/"+std::to_string(units.size())+" units; "+std::to_string(batches)+" executed batches; "+std::to_string(cached)+" dependency-validated cache hits";
    report.gate("clang-tidy",status,details,required);
    std::ostringstream data;data<<"{\"total\":"<<units.size()<<",\"analyzed\":"<<analyzed<<",\"cached\":"<<cached<<",\"batches\":"<<batches<<",\"cache_bypassed\":"<<cache_unknown<<",\"complete\":"<<(status==Status::Pass?"true":"false")<<'}';
    report.sections.push_back({"lint",data.str(),"## Lint coverage\n\n"+details+". Time budget limits scheduling; an already running batch completes. Cache is opt-in and never substitutes for unknown dependency state."});
    report.notes.push_back("clang-tidy runs without --fix. Project-controlled compiler flags and .clang-tidy affect results. Cache entries retain only successful runs.");
}
}
