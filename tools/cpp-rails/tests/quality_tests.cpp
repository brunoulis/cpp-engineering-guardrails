#include "quality.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace cpprails;
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

namespace {
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("cpp-rails-quality-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::vector<fs::path> files;
    Fixture() { fs::create_directories(root); }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    void put(const fs::path& path, const std::string& text) {
        fs::create_directories((root / path).parent_path());
        std::ofstream out(root / path); out << text; out.close();
        if (!out) throw std::runtime_error("fixture write failed");
        files.push_back(path);
    }
    QualityReport run() const { return quality_score(root, files); }
};
const QualityDimension& dimension(const QualityReport& report, const std::string& name) {
    auto it = std::find_if(report.dimensions.begin(), report.dimensions.end(),
        [&](const auto& d) { return d.name == name; });
    if (it == report.dimensions.end()) throw std::runtime_error("missing dimension " + name);
    return *it;
}
bool contains(const QualityDimension& d, const std::string& text) {
    return std::any_of(d.evidence.begin(), d.evidence.end(), [&](const auto& e) {
        return e.message.find(text) != std::string::npos;
    });
}

void empty_scope_is_unknown() {
    Fixture f; const auto r = f.run();
    REQUIRE(r.files == 0); REQUIRE(r.code_lines == 0); REQUIRE(r.dimensions.size() == 6);
    for (const auto& d : r.dimensions) {
        REQUIRE(d.status == "UNVERIFIED"); REQUIRE(!d.score);
    }
}
void duplicate_blocks_are_real_and_normalized() {
    Fixture f;
    const std::string block =
        "  int a = input + 1;\n  int b = a * 2;\n  int c = b - 3;\n"
        "  int d = c / 4;\n  int e = d + 5;\n  return e * 6;\n";
    f.put("a.cpp", "int first(int input) {\n" + block + "}\n");
    f.put("b.cpp", "int second(int input) {\n" + block + "}\n");
    const auto r = f.run(); const auto& d = dimension(r, "duplication");
    REQUIRE(d.signals > 0); REQUIRE(d.score);
    REQUIRE(std::abs(*d.score - d.signals * 1000.0 / r.production_lines) < 0.00001);
    REQUIRE(contains(d, "a.cpp")); REQUIRE(contains(d, "tokens"));
    REQUIRE(std::any_of(d.evidence.begin(), d.evidence.end(), [](const auto& e) {
        return e.path == "b.cpp" && e.line == 2;
    }));
}
void short_and_different_blocks_are_not_duplicates() {
    Fixture f;
    f.put("a.cpp", "int f() { return 1; }\n");
    f.put("b.cpp", "int g() { return 1; }\n");
    REQUIRE(dimension(f.run(), "duplication").signals == 0);
    Fixture strings;
    std::string body;
    for (int i = 0; i < 8; ++i) body += "consume(\"alpha\");\n";
    strings.put("a.cpp", body);
    for (std::size_t pos = 0; (pos = body.find("alpha", pos)) != std::string::npos; pos += 4)
        body.replace(pos, 5, "beta");
    strings.put("b.cpp", body);
    // Each file may contain repeated short lines, but no non-overlapping six-line block.
    REQUIRE(dimension(strings.run(), "duplication").signals == 0);
}
void lexer_hides_literals_comments_and_keeps_digit_separators() {
    Fixture f;
    f.put("literal.cpp", R"fixture(// if for while && ? { class Bogus {};
/* if for while && ? { */
const char* s = u8R"tag(if(x) { while(y) { class Bogus {}; } })tag";
const char* t = "if for while && ? {";
char c = '\'';
int n = 1'000;
int real(int x) {
if(x) { if(x) { if(x) { if(x) { return n; } } } }
return 0;
}
)fixture");
    const auto r = f.run(); const auto& d = dimension(r, "complexity");
    REQUIRE(d.signals == 1); REQUIRE(d.evidence.front().line == 7);
    REQUIRE(contains(d, "nesting=4"));
    REQUIRE(dimension(r, "responsibilities").signals == 0);
    REQUIRE(r.code_lines == 8);
}
void complex_functions_have_branch_evidence() {
    Fixture f; std::string source = "int complicated(int x) {\n";
    for (int i = 0; i < 11; ++i) source += "if (x > " + std::to_string(i) + ") ++x;\n";
    source += "return x;\n}\n"; f.put("complex.cpp", source);
    const auto report = f.run(); const auto& d = dimension(report, "complexity");
    REQUIRE(d.signals == 1); REQUIRE(contains(d, "branches=11"));
}
void dependency_cycles_and_fanout_have_locations() {
    Fixture f;
    f.put("a.hpp", "#include \"b.hpp\"\nstruct A {};\n");
    f.put("b.hpp", "#include \"a.hpp\"\nstruct B {};\n");
    f.put("unused.cpp", "// #include \"a.hpp\"\nconst char* s = R\"(#include \"a.hpp\")\";\n");
    const auto r = f.run(); const auto& d = dimension(r, "dependencies");
    REQUIRE(d.signals == 2); REQUIRE(contains(d, "fanout=1"));
    REQUIRE(contains(d, "cycle"));
    REQUIRE(std::any_of(d.evidence.begin(), d.evidence.end(), [](const auto& e) {
        return e.path == "a.hpp" && e.line == 1 && e.message.find("b.hpp") != std::string::npos;
    }));
}
void facade_and_pimpl_have_no_quality_penalty() {
    Fixture f;
    f.put("facade.hpp", "class Facade { public: void run(); private: class Impl; Impl* impl; };\n");
    f.put("facade.cpp", "void Facade::run() { impl->run(); }\n");
    const auto r = f.run();
    REQUIRE(dimension(r, "indirection").signals == 0);
    REQUIRE(!dimension(r, "indirection").score);
    REQUIRE(dimension(r, "responsibilities").signals == 0);
}
void virtual_dispatch_is_inventory_without_a_grade() {
    Fixture f; f.put("port.hpp", "class Port { public: virtual void run() = 0; };\n");
    const auto r = f.run(); const auto& d = dimension(r, "indirection");
    REQUIRE(d.signals == 1); REQUIRE(!d.score); REQUIRE(contains(d, "no penalty"));
}
void class_surface_is_a_review_proxy_only() {
    Fixture f; std::string source = "class Surface { public:\n";
    for (int i = 0; i < 20; ++i) source += "void method" + std::to_string(i) + "();\n";
    f.put("surface.hpp", source + "};\n");
    const auto r = f.run(); const auto& d = dimension(r, "responsibilities");
    REQUIRE(d.signals == 1); REQUIRE(contains(d, "method-like=20"));
    REQUIRE(d.status == "UNVERIFIED"); REQUIRE(!d.score);
}
void template_parameters_are_not_additional_class_bodies() {
    Fixture f; std::string source = "template<class T> class Surface { public:\n";
    for (int i = 0; i < 20; ++i) source += "void method" + std::to_string(i) + "();\n";
    f.put("surface.hpp", source + "};\n");
    REQUIRE(dimension(f.run(), "responsibilities").signals == 1);
}
void tests_are_proximity_not_coverage() {
    Fixture f;
    f.put("src/widget.cpp", "int widget() { return 2; }\n");
    f.put("tests/widget_test.cpp", "int test_widget() { return 2; }\n");
    f.put("src/latest.cpp", "int latest() { return 3; }\n");
    const auto r = f.run(); const auto& d = dimension(r, "test-proximity");
    REQUIRE(r.production_lines == 2); REQUIRE(r.test_lines == 1);
    REQUIRE(r.code_lines == 3); REQUIRE(d.signals == 1);
    REQUIRE(d.status == "UNVERIFIED"); REQUIRE(!d.score);
    REQUIRE(contains(d, "widget_test.cpp")); REQUIRE(contains(d, "coverage UNVERIFIED"));
    REQUIRE(contains(d, "latest.cpp"));
}
void report_encoding_and_input_errors() {
    Fixture f;
    f.put("name.cpp", "int a;\n");
    auto r = f.run(); r.dimensions.front().evidence.push_back({"quo\"te|name.cpp", 1, "control\ntext"}); const auto j = quality_json(r); const auto m = quality_markdown(r);
    REQUIRE(j.find("\"dimensions\"") != std::string::npos);
    REQUIRE(j.find("\"score\":null") != std::string::npos);
    REQUIRE(j.find("quo\\\"te|name.cpp") != std::string::npos);
    REQUIRE(m.find("signals / production KLOC") != std::string::npos);
    REQUIRE(m.find("quo\"te\\|name.cpp") != std::string::npos);
    bool threw = false;
    try { quality_score(f.root, {"missing.cpp"}); } catch (const std::exception&) { threw = true; }
    REQUIRE(threw);
    // Duplicate arguments cannot inflate the denominator or measurements.
    const auto dedup = quality_score(f.root, {f.files[0], f.files[0]});
    REQUIRE(dedup.files == 1); REQUIRE(dedup.code_lines == 1);
}
} // namespace
int main() {
    try {
        empty_scope_is_unknown(); duplicate_blocks_are_real_and_normalized();
        short_and_different_blocks_are_not_duplicates();
        lexer_hides_literals_comments_and_keeps_digit_separators();
        complex_functions_have_branch_evidence(); dependency_cycles_and_fanout_have_locations();
        facade_and_pimpl_have_no_quality_penalty(); virtual_dispatch_is_inventory_without_a_grade();
        class_surface_is_a_review_proxy_only(); template_parameters_are_not_additional_class_bodies();
        tests_are_proximity_not_coverage(); report_encoding_and_input_errors();
        std::cout << "quality tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "quality test failure: " << e.what() << '\n'; return 1; }
}
