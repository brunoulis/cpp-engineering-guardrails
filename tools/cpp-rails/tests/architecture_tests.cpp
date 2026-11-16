#include "architecture.hpp"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace cpprails;
static void require(bool value, const std::string &why) {
  if (!value)
    throw std::runtime_error(why);
}
static void put(const fs::path &path, const std::string &value) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << value;
}
static bool gate(const Report &r, const std::string &name, Status s) {
  for (auto &g : r.gates)
    if (g.name == name && g.status == s)
      return true;
  return false;
}
int main(int argc, char **argv) {
  const auto temp =
      fs::temp_directory_path() /
      ("cpp-rails-architecture-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(temp);
  int failed = 0;
  auto test = [&](const char *name, const std::function<void()> &body) {
    try {
      body();
      std::cout << name << ": PASS\n";
    } catch (const std::exception &e) {
      ++failed;
      std::cerr << name << ": FAIL: " << e.what() << '\n';
    }
  };
  test("literal cycle remains heuristic", [&] {
    const auto root = temp / "cycle";
    put(root / "CMakeLists.txt",
        "add_library(a STATIC a.cpp)\nadd_library(b STATIC "
        "b.cpp)\ntarget_link_libraries(a PUBLIC b)\ntarget_link_libraries(b "
        "PRIVATE a)\n");
    Options o;
    Report r;
    analyze_architecture(o, root, {}, r);
    require(gate(r, "CMake target topology", Status::Unverified),
            "fallback cannot be verified");
    require(r.required_unverified(),
            "requested target topology must return an unverified exit status");
    bool cycle = false;
    for (auto &f : r.findings)
      if (f.rule == "architecture.target-cycle") {
        cycle = true;
        require(f.severity == "P2" && f.evidence_level == "heuristic",
                "cycle overclaimed");
      }
    require(cycle, "cycle not detected");
    require(!r.sections.empty(), "missing structured topology");
  });
  test("dynamic CMake never executes", [&] {
    const auto root = temp / "dynamic";
    put(root / "CMakeLists.txt",
        "execute_process(COMMAND touch SHOULD_NOT_EXIST)\nset(T "
        "a)\nadd_library(${T} INTERFACE)\ntarget_link_libraries(${T} INTERFACE "
        "$<IF:1,b,c>)\n");
    Options o;
    Report r;
    analyze_architecture(o, root, {}, r);
    require(gate(r, "CMake target topology", Status::Unverified),
            "dynamic syntax PASS");
    require(!fs::exists(root / "SHOULD_NOT_EXIST"), "project executed");
  });
  test("codemodel exact membership cycles and unsafe references", [&] {
    const auto root = temp / "snapshot";
    const auto reply = root / "build/.cmake/api/v1/reply";
    const auto source = utf8_bytes(root.generic_u8string());
    put(reply / "index-1.json",
        R"({"objects":[{"kind":"codemodel","version":{"major":2,"minor":5},"jsonFile":"model.json"}]})");
    put(reply / "model.json",
        "{\"paths\":{\"source\":\"" + source +
            "\"},\"configurations\":[{\"name\":\"Debug\",\"targets\":[{\"id\":"
            "\"a\",\"name\":\"a\",\"jsonFile\":\"a.json\"},{\"id\":\"b\","
            "\"name\":\"b\",\"jsonFile\":\"b.json\"}]}]}");
    put(reply / "a.json",
        R"({"id":"a","name":"a","type":"STATIC_LIBRARY","fileSets":[{"visibility":"PUBLIC","baseDirectories":["include"]}],"sources":[{"path":"include/internal/a.hpp","fileSetIndex":0}],"dependencies":[{"id":"b"}]})");
    put(reply / "b.json",
        R"({"id":"b","name":"b","type":"STATIC_LIBRARY","dependencies":[{"id":"a"}]})");
    Options o;
    Report r;
    analyze_architecture(o, root, {}, r);
    require(gate(r, "CMake target topology", Status::Pass),
            "valid snapshot rejected");
    require(r.findings.size() == 2,
            "expected exact file-set exposure and target cycle");
    for (const auto &f : r.findings)
      require(f.evidence_level == "verified-structure",
              "exact structure downgraded");
    put(reply / "index-2.json",
        R"({"objects":[{"kind":"codemodel","version":{"major":2},"jsonFile":"../escape.json"}]})");
    Report unsafe;
    analyze_architecture(o, root, {}, unsafe);
    require(gate(unsafe, "CMake target topology", Status::Unverified),
            "unsafe reference accepted");
  });
  test("project policy valid denial and exception", [&] {
    const auto root = temp / "rules";
    put(root / "src/a.cpp",
        "#include \"internal/key.hpp\"\nvoid f(){ unsafe_api(); }\n");
    put(root / "rules.toml",
        "version = 1\n[[rule]]\nid = \"domain-boundary\"\nkind = "
        "\"dependency-deny\"\nfrom = \"src/**\"\nto = "
        "\"internal/**\"\n[[rule]]\nid = \"old-api\"\nkind = "
        "\"api-deny\"\npath = \"src/**\"\npattern = \"unsafe_api\"\n");
    Report r;
    apply_project_rules(root / "rules.toml", root, {root / "src/a.cpp"}, r);
    require(r.findings.size() == 2, "policy violations lost");
    require(gate(r, "project rules", Status::Fail), "denial did not fail");
    std::ofstream(root / "rules.toml", std::ios::app)
        << "[[exception]]\nrule = \"old-api\"\npath = \"src/a.cpp\"\nowner = "
           "\"team\"\nreason = \"migration\"\nreview = \"review-42\"\nexpires "
           "= \"2099-12-31\"\n";
    Report e;
    apply_project_rules(root / "rules.toml", root, {root / "src/a.cpp"}, e);
    require(e.findings.size() == 2 && e.findings[1].suppressed,
            "reviewed exception not applied");
  });
  test("invalid policy fails closed", [&] {
    const auto root = temp / "invalid";
    fs::create_directories(root);
    for (const auto &value :
         {"version = 1\ndisable_security = true\n",
          "version = 1\n[[rule]]\nid = \"a\"\nkind = \"api-deny\"\npath = "
          "\"**\"\npattern = \"x\"\nenabled = false\n",
          "version = 1\nversion = 1\n",
          "version = 1\n[[exception]]\nrule = \"security\"\npath = "
          "\"**\"\nowner = \"x\"\nreason = \"x\"\nreview = \"x\"\nexpires = "
          "\"2099-12-31\"\n"}) {
      put(root / "bad.toml", value);
      Report r;
      apply_project_rules(root / "bad.toml", root, {}, r);
      require(gate(r, "project rules", Status::Fail),
              "malformed policy accepted");
    }
  });
  test("API restriction ignores comments and strings", [&] {
    const auto root = temp / "lex";
    put(root / "src.cpp",
        "// banned_api();\nconst char* text = \"banned_api()\";\n/* "
        "banned_api(); */\nvoid banned_api2();\n");
    put(root / "rules.toml",
        "version = 1\n[[rule]]\nid = \"api\"\nkind = \"api-deny\"\npath = "
        "\"**\"\npattern = \"banned_api\"\n");
    Report r;
    apply_project_rules(root / "rules.toml", root, {root / "src.cpp"}, r);
    require(r.findings.empty(), "literal/comment false positive");
  });
  test("literal include allow layers macros and token boundaries", [&] {
    const auto root = temp / "lexical-boundaries";
    put(root / "rules.toml",
        "version = 1\n[[rule]]\nid = \"includes\"\nkind = "
        "\"dependency-allow\"\nfrom = \"**\"\nto = \"api/**\"\n[[rule]]\nid = "
        "\"upward\"\nkind = \"layer\"\nfrom = \"**\"\nto = "
        "\"ui/**\"\n[[rule]]\nid = \"api\"\nkind = \"api-deny\"\npath = "
        "\"**\"\npattern = \"banned_api\"\n");
    put(root / "ok.cpp",
        "#include \"api/types.hpp\"\nconst char* raw=R\"tag(\n#include "
        "\"ui/fake.hpp\"\nbanned_api();\n)tag\";\n// "
        "comment\\\nbanned_api();\nauto c=u8'x'; auto n=1'000;\n");
    Report ok;
    apply_project_rules(root / "rules.toml", root, {root / "ok.cpp"}, ok);
    require(ok.findings.empty() && gate(ok, "project rules", Status::Pass),
            "comment/raw literal/allowed include matched");
    put(root / "bad.cpp", "#include \"ui/widget.hpp\"\nauto c=u8'x'; auto "
                          "n=1'000; banned_api();\n");
    Report bad;
    apply_project_rules(root / "rules.toml", root, {root / "bad.cpp"}, bad);
    require(bad.findings.size() == 3 &&
                gate(bad, "project rules", Status::Fail),
            "allow/layer/numeric token violation missed");
    put(root / "macro.cpp",
        "#define HEADER \"api/types.hpp\"\n#include HEADER\n");
    Report macro;
    apply_project_rules(root / "rules.toml", root, {root / "macro.cpp"}, macro);
    require(gate(macro, "project rules", Status::Unverified),
            "macro include falsely passed");
  });
  test("exceptions cannot disable built-in security or outlive review", [&] {
    const auto root = temp / "exception-validation";
    const std::string policy =
        "version = 1\n[[rule]]\nid = \"api\"\nkind = \"api-deny\"\npath = "
        "\"**\"\npattern = \"unsafe\"\n";
    for (const auto &suffix :
         {"[[exception]]\nrule = \"SECURITY\"\npath = \"src.cpp\"\nowner = "
          "\"team\"\nreason = \"migration\"\nreview = \"42\"\nexpires = "
          "\"2099-12-31\"\n",
          "[[exception]]\nrule = \"api\"\npath = \"src.cpp\"\nowner = "
          "\"team\"\nreason = \"migration\"\nreview = \"42\"\nexpires = "
          "\"2001-01-01\"\n",
          "[[exception]]\nrule = \"api\"\npath = \"src.cpp\"\nowner = "
          "\"team\"\nreason = \"migration\"\nreview = \"42\"\nexpires = "
          "\"2099-02-29\"\n"}) {
      put(root / "rules.toml", policy + suffix);
      Report r;
      Finding security;
      security.rule = "SECURITY";
      r.findings.push_back(security);
      apply_project_rules(root / "rules.toml", root, {}, r);
      require(gate(r, "project rules", Status::Fail) &&
                  !r.findings[0].suppressed,
              "invalid exception/security suppression accepted");
    }
  });
  test("existing codemodel resolves real targets", [&] {
    require(argc > 1, "CMake executable argument required");
    const auto root = temp / "real";
    const auto build = root / "build";
    put(root / "a.cpp", "int a(){return 1;}\n");
    put(root / "b.cpp", "int b(){return 2;}\n");
    put(root / "include/internal/detail.hpp", "#pragma once\n");
    put(root / "CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.23)\nproject(topology_fixture "
        "LANGUAGES CXX)\nadd_library(a STATIC a.cpp)\nadd_library(b STATIC "
        "b.cpp)\ntarget_link_libraries(a PUBLIC b)\ntarget_sources(a PUBLIC "
        "FILE_SET HEADERS BASE_DIRS include FILES "
        "include/internal/detail.hpp)\n");
    put(build / ".cmake/api/v1/query/codemodel-v2", "");
    auto p = exec({argv[1], "-S", utf8_bytes(root.u8string()), "-B",
                   utf8_bytes(build.u8string()), "-DCMAKE_BUILD_TYPE=Debug"},
                  root);
    require(p.started && p.exit_code == 0, p.output);
    Options o;
    o.build_dir = build;
    o.config = "Debug";
    Report r;
    analyze_architecture(o, root, {}, r);
    require(gate(r, "CMake target topology", Status::Pass),
            "existing codemodel not read");
    require(r.sections[0].json.find("\"visibility\":\"unknown\"") !=
                std::string::npos,
            "invented dependency visibility");
    bool exposure = false;
    for (auto &f : r.findings)
      if (f.rule == "architecture.public-internal-header") {
        exposure = true;
        require(f.evidence_level == "verified-structure" ||
                    f.evidence.find("fileSetIndex absent") != std::string::npos,
                "exposure must cite exact membership or remain heuristic");
      }
    require(exposure, "public internal header missing");
  });
  if (failed)
    std::cerr << "Failed fixture retained at " << temp << '\n';
  else
    fs::remove_all(temp);
  return failed ? 1 : 0;
}
