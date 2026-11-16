#include "cpp_rails.hpp"
#include "detail.hpp"
#include <algorithm>
#include <map>
#include <sstream>
namespace cpprails {
using detail::trim; using detail::read_file; using detail::write_file;
namespace {
std::string encode(const std::string& text) {
    std::string result;for(char c:text){if(c=='\\')result+="\\\\";else if(c=='\t')result+="\\t";else if(c=='\n')result+="\\n";else if(c=='\r')result+="\\r";else result+=c;}return result;
}
std::string decode(const std::string& text) {
    std::string result;for(std::size_t i=0;i<text.size();++i){if(text[i]=='\\'&&i+1<text.size()){char c=text[++i];result+=c=='t'?'\t':c=='n'?'\n':c=='r'?'\r':c;}else result+=text[i];}return result;
}
std::string source_policy_key(const ScanPolicy& policy) {
    std::set<std::string> patterns;
    for(auto pattern:policy.excludes) {
        std::replace(pattern.begin(),pattern.end(),'\\','/');
        while(pattern.starts_with("./"))pattern.erase(0,2);
        while(!pattern.empty()&&pattern.back()=='/')pattern.pop_back();
        if(!pattern.empty())patterns.insert(std::move(pattern));
    }
    std::string value;
    for(const auto& pattern:patterns)value+=std::to_string(pattern.size())+":"+pattern;
    return make_id("source-policy-v1",policy.include_generated?"generated-included":"generated-excluded",value,0);
}
}
void load_baseline(const fs::path& file,Report& report) {
    const auto data=read_file(file);
    std::istringstream in(data);
    std::string line,baseline_profile,baseline_source_policy;std::map<std::string,Finding> entries;
    std::map<std::string,std::string> contexts;
    while(std::getline(in,line)) {
        line=trim(line);if(line.starts_with("# profile\t")){baseline_profile=line.substr(10);continue;}
        if(line.starts_with("# source-policy\t")){baseline_source_policy=line.substr(16);continue;}
        if(line.starts_with("# analyzer\t")) {
            const auto tab=line.find('\t',11);if(tab!=std::string::npos)contexts[decode(line.substr(11,tab-11))]=decode(line.substr(tab+1));continue;
        }
        if(line.empty()||line[0]=='#')continue;
        std::istringstream columns(line);Finding f;
        std::getline(columns,f.id,'\t');std::getline(columns,f.path,'\t');std::getline(columns,f.rule,'\t');
        std::string analyzer,level,severity;
        if(std::getline(columns,analyzer,'\t')&&!analyzer.empty())f.analyzer=analyzer;
        if(std::getline(columns,level,'\t')&&!level.empty())f.evidence_level=level;
        if(std::getline(columns,severity,'\t')&&!severity.empty())f.severity=severity;
        std::string number,evidence;
        if(std::getline(columns,number,'\t')&&!number.empty())f.line=std::stoull(number);
        if(std::getline(columns,evidence,'\t'))f.evidence=decode(evidence);
        entries.emplace(f.id,std::move(f));
    }
    report.delta={};report.delta.loaded=true;std::set<std::string> seen;
    for(auto& f:report.findings) {
        f.existing=entries.count(f.id)!=0;seen.insert(f.id);
        if(f.existing)++report.delta.known;else ++report.delta.added;
    }
    for(const auto& [id,f]:entries)if(!seen.count(id)) {
        const bool lexical=f.analyzer=="lexical";
        const auto context=report.analyzer_contexts.find(f.analyzer), previous=contexts.find(f.analyzer);
        const bool backend_complete=report.completed_analyzers.count(f.analyzer)&&context!=report.analyzer_contexts.end()&&previous!=contexts.end()&&context->second==previous->second;
        const bool file_observed=report.scanned_paths.count(f.path)!=0||(f.analyzer=="cmake-codemodel"&&backend_complete);
        // Once a file is deleted its former leading generated banner cannot be
        // inspected. Resolve deletion only when both runs recorded the same scope.
        const bool same_source_policy=!baseline_source_policy.empty()&&baseline_source_policy==source_policy_key(report.scan_policy);
        const bool deleted=report.scan_complete&&same_source_policy&&!f.path.empty()&&scan_path_exclusion(f.path,report.scan_policy).empty()
            &&!fs::exists(path_from_utf8(report.root)/path_from_utf8(f.path));
        const bool same_profile=baseline_profile.empty()||baseline_profile==report.profile;
        const bool observable=(file_observed||deleted)&&report.findings_capped==0&&(lexical||backend_complete)&&same_profile;
        if(observable)report.delta.resolved.push_back(f);else {++report.delta.unobserved;report.delta.unobserved_entries.push_back(f);}
    }
    report.notes.push_back("Baseline: "+std::to_string(report.delta.added)+" new, "+std::to_string(report.delta.known)+" known, "+
        std::to_string(report.delta.resolved.size())+" disappeared in observed scope, "+std::to_string(report.delta.unobserved)+" not observed. Disappearance is not a correctness proof.");
}
void write_baseline(const fs::path& file,const Report& report) {
    std::vector<const Finding*> sorted;
    for(auto& f:report.findings)sorted.push_back(&f);
    std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a->id<b->id;});
    std::ostringstream out;out<<"# cpp-rails baseline v2 — manually review; id/path/rule/analyzer/level/severity/line/evidence\n# profile\t"<<report.profile<<'\n';
    out<<"# source-policy\t"<<source_policy_key(report.scan_policy)<<'\n';
    for(const auto& [analyzer,context]:report.analyzer_contexts)out<<"# analyzer\t"<<encode(analyzer)<<'\t'<<encode(context)<<'\n';
    for(auto f:sorted) {
        for(const auto& field:{f->id,f->path,f->rule,f->analyzer,f->evidence_level,f->severity})
            if(field.find_first_of("\t\r\n")!=std::string::npos)throw std::runtime_error("baseline fields cannot contain tabs or line breaks");
        out<<f->id<<'\t'<<f->path<<'\t'<<f->rule<<'\t'<<f->analyzer<<'\t'<<f->evidence_level<<'\t'<<f->severity<<'\t'<<f->line<<'\t'<<encode(f->evidence)<<'\n';
    }
    write_file(file,out.str());
}

}
