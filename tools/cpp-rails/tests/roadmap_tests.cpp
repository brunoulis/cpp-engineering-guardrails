#include "cpp_rails.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace cpprails;
namespace {
void require(bool value,const std::string& why) { if(!value)throw std::runtime_error(why); }
void put(const fs::path& p,const std::string& text) {fs::create_directories(p.parent_path());std::ofstream(p)<<text;}
Finding finding(std::string id,std::string path="a.cpp") { Finding f;f.id=id;f.path=path;f.rule="CPP-RAW-ALLOC";f.severity="P2";f.confidence="suspected";f.line=1;return f; }
}
int main(int argc,char** argv) {
    // Child process used only to test lint batching, not analyzer precision.
    if(argc>1&&std::string(argv[1])=="--version") {std::cout<<"test-tidy-v1\n";return 0;}
    if(argc>1&&std::string(argv[1])=="-p") {
        std::ofstream calls("tidy-calls.txt",std::ios::app);calls<<"batch";
        for(int i=1;i<argc;++i)if(fs::path(argv[i]).extension()==".cpp")calls<<' '<<argv[i];
        calls<<'\n';return 0;
    }
    auto dir=fs::temp_directory_path()/("cpprails-roadmap-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);int failed=0;
    auto test=[&](const char* name,const std::function<void()>& body){try{body();std::cout<<name<<": PASS\n";}catch(const std::exception& e){++failed;std::cerr<<name<<": FAIL: "<<e.what()<<'\n';}};
    test("roadmap options reject invalid budgets",[&]{
        auto o=parse_options(false,{"audit","--score","--semantic","--clang","clang++","--analysis-budget","12","--rules","p.toml"});
        require(o.score&&o.semantic&&o.analysis_budget==12&&o.project_rules.has_value(),"missing feature options");
        for(auto value:{"-1","0","12junk","999999999999999999999"}) {
            bool bad=false;try{parse_options(true,{"--lint-batch-size",value});}catch(...){bad=true;}require(bad,"accepted invalid batch size");
        }
        require(parse_options(false,{"safety","contracts"}).safety,"safety route absent");
        require(parse_options(false,{"baseline","--safety"}).safety,"cannot create a baseline of safety effects");
        require(parse_options(false,{"plan"}).action=="plan","plan route absent");
    });
    test("baseline delta distinguishes resolved from unobserved",[&]{
        const auto baseline=dir/"baseline.txt";Report before;
        before.findings={finding("known"),finding("fixed"),finding("unseen","b.cpp")};write_baseline(baseline,before);
        Report now;now.root=utf8_bytes(dir.u8string());now.scanned_paths.insert("a.cpp");now.findings={finding("known"),finding("added")};load_baseline(baseline,now);
        require(now.delta.loaded&&now.delta.added==1&&now.delta.known==1,"new/known counts");
        require(now.delta.resolved.size()==1&&now.delta.resolved[0].id=="fixed"&&now.delta.unobserved==1,"partial scan falsely resolved unseen findings");
        require(json(now).find("\"unobserved\":1")!=std::string::npos,"missing machine readable delta");
    });
    test("old baseline IDs remain loadable",[&]{
        auto path=dir/"v1.txt";put(path,"# v1\nknown\ta.cpp\tCPP-RAW-ALLOC\n");Report r;r.findings={finding("known")};load_baseline(path,r);
        require(r.findings[0].existing&&r.delta.known==1,"v1 compatibility lost");
    });
    test("semantic PASS cannot resolve findings from another analyzer",[&]{
        Report before;auto f=finding("policy");f.analyzer="project-rules";before.findings={f};auto path=dir/"analyzers.txt";write_baseline(path,before);
        Report now;now.root=utf8_bytes(dir.u8string());now.scanned_paths.insert("a.cpp");now.gate("semantic",Status::Pass,"AST ran");load_baseline(path,now);
        require(now.delta.resolved.empty()&&now.delta.unobserved==1,"unexecuted policy was called resolved");
    });
    test("baseline resolution requires matching backend mode and configuration",[&]{
        Report before;auto f=finding("semantic");f.analyzer="clang-ast";f.evidence="owner\tvalue\\path";before.findings={f};before.analyzer_contexts["clang-ast"]="safety-v1";
        auto path=dir/"contexts.txt";write_baseline(path,before);Report now;now.root=utf8_bytes(dir.u8string());now.scanned_paths.insert("a.cpp");now.completed_analyzers.insert("clang-ast");now.analyzer_contexts["clang-ast"]="semantic-v1";
        load_baseline(path,now);require(now.delta.resolved.empty()&&now.delta.unobserved==1,"backend mode change falsely resolved finding");
        now.analyzer_contexts["clang-ast"]="safety-v1";load_baseline(path,now);require(now.delta.resolved.size()==1&&now.delta.resolved[0].evidence==f.evidence,"same backend context/evidence was lost");
    });
    test("incomplete scans do not resolve missing files",[&]{
        Report before;before.findings={finding("gone","deleted.cpp")};auto path=dir/"partial.txt";write_baseline(path,before);
        Report now;now.root=utf8_bytes(dir.u8string());now.scan_complete=false;load_baseline(path,now);
        require(now.delta.resolved.empty()&&now.delta.unobserved==1,"incomplete scan called deletion fixed");
        now.scan_complete=true;load_baseline(path,now);require(now.delta.resolved.size()==1,"complete scan failed to account for deleted file");
    });
    test("analyzer provenance and normalized metrics are serialized",[&]{
        Report r;r.code_lines=20;r.production_lines=10;r.test_lines=10;r.findings={finding("id")};r.findings[0].analyzer="clang-ast";r.findings[0].evidence_level="verified-structure";
        auto data=json(r);require(data.find("\"code_lines\":20")!=std::string::npos&&data.find("per_kloc")!=std::string::npos,"missing normalized metrics");
        require(data.find("\"analyzer\":\"clang-ast\"")!=std::string::npos,"lost provenance");
        require(sarif(r).find("verified-structure")!=std::string::npos,"lost SARIF provenance");
        Report empty;require(json(empty).find("\"per_kloc\":null")!=std::string::npos,"empty scope has invented rate");
    });
    test("failed requested analysis preserves reviewed baseline and nonzero status",[&]{
        auto root=dir/"baseline-failure";put(root/"a.cpp","int x;\n");put(root/"reviewed.txt","preserve reviewed evidence\n");
        Options o;o.root=root;o.action="baseline";o.output="reviewed.txt";o.semantic=true;o.clang_executable="cpprails-intentionally-missing";Report r;
        require(dispatch(o,r)==2,"baseline swallowed required UNVERIFIED analysis");
        std::ifstream in(root/"reviewed.txt");std::string text{std::istreambuf_iterator<char>(in),{}};require(text=="preserve reviewed evidence\n","failed analysis overwrote baseline");
    });
    test("incomplete scanner cannot replace reviewed baseline",[&]{
        auto root=dir/"baseline-incomplete";put(root/"oversized.cpp",std::string(2*1024*1024+1,'x'));put(root/"reviewed.txt","keep\n");
        Options o;o.root=root;o.action="baseline";o.output="reviewed.txt";Report r;require(dispatch(o,r)==2,"incomplete source inventory reported successful baseline");
        std::ifstream in(root/"reviewed.txt");std::string text{std::istreambuf_iterator<char>(in),{}};require(text=="keep\n","incomplete inventory replaced reviewed evidence");
    });
    test("scanning counts code rather than comments",[&]{
        auto root=dir/"lines";put(root/"a.cpp","// comment\n/* comment */\nint a;\n\nint b;\n");put(root/"tests/a_test.cpp","int c;\n");
        auto s=scan(root);require(s.code_lines==3&&s.production_lines==2&&s.test_lines==1,"wrong masked LOC denominator");
    });
    test("plan contains contract and reversible alternatives",[&]{
        auto root=dir/"plan";put(root/"a.cpp","void f(){ auto p=new int; delete p; }\n");Options o;o.root=root;o.action="plan";Report r;dispatch(o,r);
        auto data=json(r);for(auto key:{"minimal","structural","leave_as_is","contract","verification","risks"})require(data.find(key)!=std::string::npos,std::string("missing plan field ")+key);
    });
    test("lint handles more than thirty units and explicit limits",[&]{
        auto root=dir/"lint";put(root/"build/compile_commands.json","[]");
        for(int i=0;i<35;++i)put(root/("u"+std::to_string(i)+".cpp"),"int x;\n");
        Options o;o.check_program=true;o.action="check";o.root=root;o.no_build=true;o.no_tests=true;o.lint=true;o.lint_batch_size=10;o.tidy_executable=utf8_bytes(fs::absolute(path_from_utf8(argv[0])).u8string());
        Report r;dispatch(o,r);bool pass=false;for(auto& g:r.gates)if(g.name=="clang-tidy")pass=g.status==Status::Pass;require(pass,"old project cap or failed batching");
        std::ifstream in(root/"tidy-calls.txt");std::string line;int batches=0;while(std::getline(in,line))++batches;require(batches==4,"batch count mismatch");
        o.lint_max_units=12;Report partial;dispatch(o,partial);bool unverified=false;for(auto& g:partial.gates)if(g.name=="clang-tidy")unverified=g.status==Status::Unverified;require(unverified,"partial lint reported complete");
        require(json(partial).find("\"analyzed\":12")!=std::string::npos,"missing analyzed count");
    });
    std::error_code ec;fs::remove_all(dir,ec);return failed?1:0;
}
