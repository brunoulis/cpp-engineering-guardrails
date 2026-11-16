#include "cpp_rails.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>
#include <stdexcept>
using namespace cpprails;
#define REQUIRE(condition) do { \
  if(!(condition)) throw std::runtime_error(std::string("selftest assertion failed: ") + #condition); \
} while(false)

int main(int argc,char** argv){
  try {
    REQUIRE(status_text(Status::Unverified)=="UNVERIFIED");
    REQUIRE(make_id("R","path.cpp","code",1)==make_id("R","path.cpp","code",1));
    REQUIRE(make_id("R","path.cpp","code",1)!=make_id("R","path.cpp","code",2));
    // Preserve UTF-8 bytes (including embedded NUL) across char8_t boundaries.
    REQUIRE(utf8_bytes(u8"").empty());
    REQUIRE(utf8_bytes(u8"ni\u00f1o \U0001f697.cpp")=="ni\xc3\xb1o \xf0\x9f\x9a\x97.cpp");
    REQUIRE(utf8_bytes(std::u8string_view(u8"a\0b",3))==std::string("a\0b",3));
    const auto utf8_path=path_from_utf8("ni\xc3\xb1o \xf0\x9f\x9a\x97.cpp");
    REQUIRE(utf8_bytes(utf8_path.u8string())=="ni\xc3\xb1o \xf0\x9f\x9a\x97.cpp");
    REQUIRE(parse_options(false,{"audit","--profile","general"}).action=="audit");
    REQUIRE(parse_options(true,{"--format","json"}).check_program);
    REQUIRE(parse_options(false,{"templates","--expect-error","insert"}).expect_error=="insert");
    REQUIRE(parse_options(false,{"audit","--profile","engine"}).profile=="engine");
    bool bad=false;
    try{parse_options(false,{"audit","--does-not-exist"});}catch(const std::exception&){bad=true;}
    REQUIRE(bad);
    Report r;
    r.gate("test",Status::Unverified,"not run");
    REQUIRE(r.required_unverified() && !r.has_failure());
    REQUIRE(json(r).find("UNVERIFIED")!=std::string::npos);
    auto temp=std::filesystem::temp_directory_path()/("cpp-rails-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);
    const auto unicode_file=temp/std::filesystem::path(u8"ni\u00f1o \U0001f697.cpp");
    { std::ofstream(unicode_file)<<"void f(){auto p=new int;delete p;}\n"; }
    const auto unicode_scan=scan(temp);
    REQUIRE(unicode_scan.findings.size()==1);
    REQUIRE(unicode_scan.findings[0].path=="ni\xc3\xb1o \xf0\x9f\x9a\x97.cpp");
    Report unicode_report;unicode_report.root=utf8_bytes(temp.u8string());
    unicode_report.findings=unicode_scan.findings;
    REQUIRE(json(unicode_report).find("ni\xc3\xb1o \xf0\x9f\x9a\x97.cpp")!=std::string::npos);
    REQUIRE(sarif(unicode_report).find("ni%C3%B1o%20%F0%9F%9A%97.cpp")!=std::string::npos);
    std::filesystem::remove(unicode_file);
    auto testsource=temp/"demo.cpp";
    {
        std::ofstream out(testsource);
        out<<"// new badignored;\nconst char* text=\"new ignored\";\n"
              "void process(){ auto p = new int{3}; delete p; }\n"
              "// reinterpret_cast<int*> should be ignored\n";
    }
    // Comments and strings must not trigger lexical candidates.
    auto scanned=scan(temp);
    REQUIRE(scanned.files.size()==1);
    REQUIRE(scanned.findings.size()==1);
    REQUIRE(scanned.findings.front().rule=="CPP-RAW-ALLOC");
    // A reviewed baseline marks existing hints, not compiler/test failures.
    Report baseline;
    baseline.findings=scanned.findings;
    const auto baseline_file=temp/"baseline.txt";
    write_baseline(baseline_file,baseline);
    baseline.findings.front().existing=false;
    load_baseline(baseline_file,baseline);
    REQUIRE(baseline.findings.front().existing);
    // Expired/incomplete exception cannot quietly hide defects.
    const auto exceptions=temp/"exceptions.toml";
    {
      std::ofstream out(exceptions);
      out<<"[[exception]]\nid = \""<<scanned.findings.front().id
         <<"\"\npath = \"*.cpp\"\nowner = \"team\"\nreason = \"exception test\"\nexpires = \"2000-01-01\"\n";
    }
    apply_exceptions(exceptions,baseline);
    REQUIRE(!baseline.findings.front().suppressed);
    auto csv=temp/"perf.csv";
    {std::ofstream out(csv);out<<"benchmark,variant,run_ns\n";
        for(int i=0;i<8;++i)out<<"x,baseline,"<<100+i%3<<"\nx,candidate,"<<120+i%3<<"\n";
    }
    Report perf;
    benchmark_compare(csv,5,5.0,perf);
    REQUIRE(perf.has_failure());
    const std::string cmake=argc>1?argv[1]:"cmake";
    const auto p=exec({cmake,"--version"},temp);
    REQUIRE(p.started && p.exit_code==0);
    // Output is captured on every platform (Windows used to inherit it, which made
    // --changed silently scan nothing) and arguments with spaces/quotes survive.
    REQUIRE(p.output.find("cmake version")!=std::string::npos);
    const auto echo=exec({cmake,"-E","echo","hello world","quo\"te","back\\slash\\"},temp);
    REQUIRE(echo.started && echo.exit_code==0);
    REQUIRE(echo.output.find("hello world quo\"te back\\slash\\")!=std::string::npos);
    REQUIRE(!exec({"cpp-rails-no-such-tool-xyz"},temp).started);

    auto rules_of=[](const Scan& s){std::multiset<std::string> out;for(auto& f:s.findings)out.insert(f.rule);return out;};
    const auto ui=temp/"ui";
    std::filesystem::create_directories(ui);
    {
        std::ofstream out(ui/"panel.cpp");
        out<<"Panel::Panel(QWidget* parent) : QWidget(parent) {\n"
             "  label_ = new QLabel(tr(\"x\"), this);\n"
             "  connect(timer, &QTimer::timeout, [this] { label_->clear(); });\n"
             "  connect(button, &QPushButton::clicked, this, [this] { label_->clear(); });\n"
             "  QCoreApplication::processEvents();\n"
             "  SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);\n"
             "  std::thread([this] { work(); }).detach();\n"
             "  wchar_t small[4]; wcscpy(small, input);\n"
             "  snprintf(buf, sizeof buf, \"%d\", 1); strcpy_s(buf, 4, \"a\");\n"
             "  bool ok = std::filesystem::exists(p);\n"
             "}\n";
    }
    const auto ui_rules=rules_of(scan(ui,std::nullopt,"native-ui"));
    REQUIRE(ui_rules.count("CPP-UI-PARENT-OWNED-NEW")==1);
    REQUIRE(ui_rules.count("CPP-RAW-ALLOC")==0);
    REQUIRE(ui_rules.count("CPP-UI-CONNECT-NO-CONTEXT")==1);   // the context-object overload is not flagged
    REQUIRE(ui_rules.count("CPP-UI-NESTED-EVENT-LOOP")==1);
    REQUIRE(ui_rules.count("CPP-UI-WINDOW-USERDATA")==1);
    REQUIRE(ui_rules.count("CPP-ASYNC-CAPTURES-THIS")==1);
    REQUIRE(ui_rules.count("CPP-DETACHED-THREAD")==1);
    REQUIRE(ui_rules.count("CPP-UNBOUNDED-STRING")==1);         // wcscpy yes; snprintf/strcpy_s no
    REQUIRE(ui_rules.count("CPP-SHELL-CALL")==0);               // filesystem( is not system(
    const auto general_rules=rules_of(scan(ui,std::nullopt,"general"));
    REQUIRE(general_rules.count("CPP-RAW-ALLOC")==1);           // UI ownership idioms only relax under native-ui
    REQUIRE(general_rules.count("CPP-UI-CONNECT-NO-CONTEXT")==0);
    REQUIRE(general_rules.count("CPP-ASYNC-CAPTURES-THIS")==1);

    // --- Lessons from auditing real code (Flutter engine) -------------------------------
    // Test sources: reported as scope=test one level lower, or skipped with --exclude-tests.
    REQUIRE(is_test_path("shell/flutter_windows_engine_unittests.cc"));
    REQUIRE(is_test_path("testing/test_keyboard.cc"));
    REQUIRE(is_test_path("src/FooTest.cpp") && is_test_path("tests/a.cpp") && is_test_path("test_utils.cc"));
    REQUIRE(!is_test_path("src/latest.cpp") && !is_test_path("src/contest_engine.cc") && !is_test_path("testament.cc"));
    const auto real=temp/"real";
    std::filesystem::create_directories(real/"src");
    std::filesystem::create_directories(real/"tests");
    {
        std::ofstream out(real/"src"/"prod.cc");
        out<<"void f(SkPath path, HWND hwnd, LPARAM lp, MainWindow* self, const char* s) {\n"
             "  auto p = DlPath(path.detach());\n"                                     // SkPath::detach: not a thread
             "  std::thread worker(run);\n  worker.detach();\n"                          // declared thread
             "  worker_thread_.detach();\n"                                              // named like a thread
             "  SetWindowLongPtr(hwnd, 0, reinterpret_cast<LONG_PTR>(self));\n"          // handle idiom -> P3
             "  auto bytes = reinterpret_cast<const unsigned char*>(s);\n"               // byte view -> P3
             "  auto* w = reinterpret_cast<MainWindow*>(lp);\n"                          // object type -> P2
             "  auto gl = std::shared_ptr<ProcTable>(new ProcTable());\n"                // adopted -> P3
             "  holder.reset(new Widget);\n"                                             // adopted -> P3
             "  static Helper* helper = new Helper();\n"                                 // leaky singleton -> P3
             "  auto* raw = new Widget();\n"                                             // manual -> P2
             "}\n";
    }
    {std::ofstream out(real/"tests"/"prod_unittests.cc");out<<"void t(char* a, const char* b){ strcpy(a, b); }\n";}
    const auto real_scan=scan(real);
    auto find_all=[&](const std::string& rule,const std::string& severity){
        return std::count_if(real_scan.findings.begin(),real_scan.findings.end(),[&](const Finding& f){return f.rule==rule&&f.severity==severity;});
    };
    REQUIRE(find_all("CPP-DETACHED-THREAD","P2")==2);
    REQUIRE(find_all("CPP-REINTERPRET-CAST","P3")==2 && find_all("CPP-REINTERPRET-CAST","P2")==1);
    REQUIRE(find_all("CPP-RAW-ALLOC","P3")==3 && find_all("CPP-RAW-ALLOC","P2")==1);
    REQUIRE(find_all("CPP-UNBOUNDED-STRING","P3")==1 && find_all("CPP-UNBOUNDED-STRING","P2")==0);
    for (auto& f:real_scan.findings) REQUIRE((f.scope=="test")==(f.path.find("tests/")==0));
    const auto no_tests=scan(real,std::nullopt,"general",true);
    REQUIRE(no_tests.files.size()==1 && no_tests.tests_excluded==1);
    // Per-file cap is reported, not silent.
    {
        std::ofstream out(real/"src"/"many.cc");
        for (int i=0;i<25;++i) out<<"void g"<<i<<"(){ memcpy(a, b, "<<i<<"); }\n";
    }
    const auto capped_scan=scan(real);
    REQUIRE(capped_scan.capped==5 && capped_scan.capped_files==1);
    std::filesystem::remove(real/"src"/"many.cc");

    // Reports: summary, scope column and SARIF.
    Options audit_opt;audit_opt.root=real;
    Report audit;
    REQUIRE(dispatch(audit_opt,audit)==0);
    REQUIRE(audit.findings.front().severity=="P2" && audit.findings.front().scope=="production");  // production P2 first
    const auto md=markdown(audit);
    REQUIRE(md.find("Summary by rule")!=std::string::npos && md.find("Review first")!=std::string::npos);
    REQUIRE(json(audit).find("\"hotspots\":[{\"path\":\"src/prod.cc\"")!=std::string::npos);
    const auto sarif_text=sarif(audit);
    REQUIRE(sarif_text.find("\"version\":\"2.1.0\"")!=std::string::npos);
    REQUIRE(sarif_text.find("\"ruleId\":\"CPP-DETACHED-THREAD\"")!=std::string::npos);
    REQUIRE(sarif_text.find("\"uriBaseId\":\"SRCROOT\"")!=std::string::npos);
    REQUIRE(sarif_text.find("\"level\":\"note\"")!=std::string::npos);
    REQUIRE(parse_options(false,{"audit","--format","sarif","--exclude-tests"}).exclude_tests);

    // Topology: include roots above --root, include dirs from compile_commands.json.
    const auto repo=temp/"repo";
    std::filesystem::create_directories(repo/"lib"/"inc");
    std::filesystem::create_directories(repo/"other");
    std::filesystem::create_directories(repo/"lib"/"build");
    {std::ofstream(repo/"lib"/"a.h")<<"#pragma once\n";}
    {std::ofstream(repo/"lib"/"inc"/"z.h")<<"#pragma once\n";}
    {std::ofstream(repo/"other"/"x.h")<<"#pragma once\n";}
    {
        std::ofstream out(repo/"lib"/"b.cc");
        out<<"#include \"repo/lib/a.h\"\n#include \"repo/other/x.h\"\n#include \"z.h\"\n#include \"generated/missing.h\"\n";
    }
    {std::ofstream(repo/"lib"/"build"/"compile_commands.json")<<"[{\"directory\":\".\",\"command\":\"cl /nologo /Iinc /c b.cc\",\"file\":\"b.cc\"}]\n";}
    Options topo_opt;topo_opt.action="topology";topo_opt.root=repo/"lib";
    Report topo;
    REQUIRE(dispatch(topo_opt,topo)==0);
    auto has_note=[&](const std::string& text){
        return std::any_of(topo.notes.begin(),topo.notes.end(),[&](const std::string& n){return n.find(text)!=std::string::npos;});
    };
    REQUIRE(has_note("2 include edges"));
    REQUIRE(has_note("1 via include dirs") && has_note("1 via ancestor include roots"));
    REQUIRE(has_note("1 outside --root") && has_note("1 unresolved"));

    // ctest -C / cmake --build --config must match the configured build.
    Options with_config;with_config.config="RelWithDebInfo";
    REQUIRE(build_config(with_config,temp)=="RelWithDebInfo");
    REQUIRE(build_config(Options{},temp/"no-such-build")=="Debug");
    {std::ofstream out(temp/"CMakeCache.txt");out<<"CMAKE_BUILD_TYPE:STRING=Release\n";}
    REQUIRE(build_config(Options{},temp)=="Release");
    {std::ofstream out(temp/"CMakeCache.txt");out<<"CMAKE_BUILD_TYPE:STRING=\n";}
    REQUIRE(build_config(Options{},temp)=="Debug");
    REQUIRE(parse_options(true,{"--config","Release"}).config.value_or("")=="Release");
    bool bad_config=false;
    try{parse_options(true,{"--config","Rel;ease"});}catch(const std::exception&){bad_config=true;}
    REQUIRE(bad_config);
    std::filesystem::remove_all(temp);
    std::cout<<"cpp-rails native C++ selftest PASS\n";
  } catch(const std::exception& e) {
    std::cerr<<e.what()<<'\n';
    return 1;
  }
  return 0;
}
