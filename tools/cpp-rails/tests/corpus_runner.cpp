#include "cpp_rails.hpp"
#include "quality.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace {
using namespace cpprails;
struct Label {
    std::string id, detector, rule, path;
    bool positive = false;
    std::string profile, provenance, rationale;
};
struct Counts {
    std::size_t tp = 0, fp = 0, fn = 0, tn = 0;
    void observe(bool expected, bool detected) {
        if (expected && detected) ++tp;
        else if (detected) ++fp;
        else if (expected) ++fn;
        else ++tn;
    }
    std::optional<double> precision() const {
        if (tp + fp == 0) return std::nullopt;
        return static_cast<double>(tp) / (tp + fp);
    }
    std::optional<double> recall() const {
        if (tp + fn == 0) return std::nullopt;
        return static_cast<double>(tp) / (tp + fn);
    }
};
struct Bounds {
    std::size_t min_tp = 0, max_fp = 0, max_fn = 0;
    bool accepts(const Counts& counts) const {
        return counts.tp >= min_tp && counts.fp <= max_fp && counts.fn <= max_fn;
    }
};
using RuleKey = std::tuple<std::string, std::string, std::string>;
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result; std::size_t start = 0;
    while (true) {
        auto tab = line.find('\t', start);
        result.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
        if (tab == std::string::npos) return result;
        start = tab + 1;
    }
}
std::vector<Label> manifest(std::istream& input) {
    std::vector<Label> result; std::string line; std::size_t number = 0;
    std::set<std::string> ids;
    std::set<std::tuple<std::string, std::string, std::string, std::string>> units;
    bool header = false;
    while (std::getline(input, line)) {
        ++number; if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.starts_with('#')) continue;
        if (!header) {
            if (line != "case_id\tdetector\trule\tpath\tlabel\tprofile\tprovenance\trationale")
                throw std::runtime_error("invalid corpus manifest header");
            header = true; continue;
        }
        const auto row = fields(line);
        auto invalid = [&]() { throw std::runtime_error("invalid corpus label at line " + std::to_string(number)); };
        if (row.size() != 8 || std::any_of(row.begin(), row.end(), [](const auto& v) { return v.empty(); })) invalid();
        if (row[1] != "lexical" && row[1] != "quality") invalid();
        if (row[1] == "quality" && row[2] != "duplication" && row[2] != "complexity") invalid();
        if (row[1] == "lexical" && !row[2].starts_with("CPP-")) invalid();
        if (row[4] != "positive" && row[4] != "negative") invalid();
        if (row[5] != "general" && row[5] != "native-ui") invalid();
        const auto path = path_from_utf8(row[3]);
        if (path.is_absolute() || path.has_root_name() || row[3].find('\\') != std::string::npos) invalid();
        for (const auto& part : path) if (part == ".." || part == ".") invalid();
        if (!ids.insert(row[0]).second || !units.emplace(row[1], row[2], row[3], row[5]).second) invalid();
        result.push_back({row[0], row[1], row[2], row[3], row[4] == "positive", row[5], row[6], row[7]});
    }
    if (!input.eof() || !header || result.empty()) throw std::runtime_error("empty or unreadable corpus manifest");
    return result;
}
std::map<RuleKey, Bounds> policy(std::istream& input) {
    std::map<RuleKey, Bounds> result; std::string line; bool header = false;
    auto integer = [](const std::string& text) {
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("invalid corpus bound integer");
        const auto value = std::stoull(text);
        if (value > std::numeric_limits<std::size_t>::max()) throw std::runtime_error("corpus bound overflow");
        return static_cast<std::size_t>(value);
    };
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.starts_with('#')) continue;
        if (!header) {
            if (line != "detector\trule\tprofile\tmin_tp\tmax_fp\tmax_fn") throw std::runtime_error("invalid corpus bounds header");
            header = true; continue;
        }
        const auto row = fields(line);
        if (row.size() != 6 || std::any_of(row.begin(), row.end(), [](const auto& field) { return field.empty(); }))
            throw std::runtime_error("invalid corpus bound row");
        if (!result.emplace(RuleKey{row[0], row[1], row[2]}, Bounds{integer(row[3]), integer(row[4]), integer(row[5])}).second)
            throw std::runtime_error("duplicate corpus rule bounds");
    }
    if (!input.eof() || !header || result.empty()) throw std::runtime_error("empty or unreadable corpus bounds");
    return result;
}
std::string quote(std::string_view text) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char c : text) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { result += "\\u00"; result += digits[c >> 4]; result += digits[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + '"';
}
void ratio(std::ostream& out, std::optional<double> value) { if (value) out << *value; else out << "null"; }
std::string outcome(const Label& label, bool detected) {
    return label.positive ? (detected ? "TP" : "FN") : (detected ? "FP" : "TN");
}
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("corpus self-test: " + message);
}
void self_test() {
    Counts counts;
    counts.observe(true, true); counts.observe(false, true);
    counts.observe(true, false); counts.observe(false, false);
    require(counts.tp == 1 && counts.fp == 1 && counts.fn == 1 && counts.tn == 1, "confusion counts");
    require(counts.precision() && *counts.precision() == 0.5, "expected precision 0.5");
    require(counts.recall() && *counts.recall() == 0.5, "expected recall 0.5");
    Counts undefined; require(!undefined.precision() && !undefined.recall(), "undefined ratios are null");
    const Bounds allowed_noise{1, 1, 1};
    require(allowed_noise.accepts(counts), "known labeled noise must be allowed");
    Counts removed_detector; removed_detector.observe(true, false); removed_detector.observe(false, false);
    require(!allowed_noise.accepts(removed_detector), "removing every positive detector must fail bounds");
    Counts extra_false_positive = counts; extra_false_positive.observe(false, true);
    require(!allowed_noise.accepts(extra_false_positive), "new false positive must fail bounds");
    Counts extra_false_negative = counts; extra_false_negative.observe(true, false);
    require(!allowed_noise.accepts(extra_false_negative), "new false negative must fail bounds");
    undefined.observe(false, true);
    require(undefined.precision() && *undefined.precision() == 0.0 && !undefined.recall(), "zero versus undefined");
    const std::string header = "case_id\tdetector\trule\tpath\tlabel\tprofile\tprovenance\trationale\n";
    const std::string valid = "a\tquality\tcomplexity\ta.cpp\tpositive\tgeneral\tsynthetic\tbranch candidate\n";
    std::istringstream good(header + valid); const auto rows = manifest(good);
    require(rows.size() == 1 && rows.front().positive, "valid positive label");
    require(outcome(rows.front(), false) == "FN", "false negative classification");
    Label negative = rows.front(); negative.positive = false;
    require(outcome(negative, true) == "FP", "false positive classification");
    for (const auto& bad : {std::string{}, header, header + valid + valid,
        header + "a\tquality\tcomplexity\t../a.cpp\tpositive\tgeneral\tsynthetic\tbad path\n",
        header + "a\tquality\tcomplexity\ta.cpp\tunknown\tgeneral\tsynthetic\tbad label\n",
        header + "a\tsemantic\tcomplexity\ta.cpp\tpositive\tgeneral\tsynthetic\tunsupported\n"}) {
        bool rejected = false;
        try { std::istringstream input(bad); (void)manifest(input); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid manifest accepted");
    }
    std::ostringstream json; ratio(json, Counts{}.recall()); require(json.str() == "null", "null JSON ratio");
    require(quote("a\"\n") == "\"a\\\"\\u000a\"", "JSON escaping");
    const std::string bound_header = "detector\trule\tprofile\tmin_tp\tmax_fp\tmax_fn\n";
    const std::string bound_row = "quality\tcomplexity\tgeneral\t1\t1\t1\n";
    std::istringstream bound_input(bound_header + bound_row);
    require(policy(bound_input).at({"quality", "complexity", "general"}).accepts(counts), "bound policy parse");
    for (const auto& bad : {bound_header, bound_header + bound_row + bound_row,
        bound_header + "quality\tcomplexity\tgeneral\t-1\t0\t0\n"}) {
        bool rejected = false;
        try { std::istringstream input(bad); (void)policy(input); } catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid bounds accepted");
    }
    std::cout << "corpus metric and manifest self-tests passed\n";
}
struct Result { std::string json; bool accepted = true; };
Result run(const fs::path& input_root, bool check) {
    const auto root = fs::weakly_canonical(input_root);
    std::ifstream labels_file(root / "manifest.tsv");
    if (!labels_file) throw std::runtime_error("cannot open corpus manifest");
    const auto labels = manifest(labels_file);
    for (const auto& label : labels) {
        const auto file = fs::weakly_canonical(root / path_from_utf8(label.path));
        const auto [root_end, ignored] = std::mismatch(root.begin(), root.end(), file.begin(), file.end());
        (void)ignored;
        if (root_end != root.end() || !fs::is_regular_file(file)) throw std::runtime_error("invalid corpus file: " + label.path);
    }
    using Prediction = std::pair<std::string, std::string>;
    std::map<std::string, std::set<Prediction>> lexical;
    std::vector<fs::path> sources;
    for (const auto& label : labels) {
        if (label.detector == "lexical" && !lexical.contains(label.profile)) {
            const auto scanned = scan(root, std::nullopt, label.profile);
            if (scanned.skipped || scanned.capped) throw std::runtime_error("incomplete lexical corpus scan");
            auto& predictions = lexical[label.profile];
            for (const auto& finding : scanned.findings) predictions.emplace(finding.rule, finding.path);
            if (sources.empty()) sources = scanned.files;
        }
    }
    // A quality-only manifest still discovers the exact same source inventory.
    if (sources.empty()) {
        const auto scanned = scan(root);
        if (scanned.skipped) throw std::runtime_error("incomplete quality source inventory");
        sources = scanned.files;
    }
    const auto quality = quality_score(root, sources); std::set<Prediction> quality_predictions;
    for (const auto& dimension : quality.dimensions) {
        if (dimension.name != "duplication" && dimension.name != "complexity") continue;
        if (!dimension.signals) continue;
        for (const auto& item : dimension.evidence) if (!item.path.empty())
            quality_predictions.emplace(dimension.name, item.path);
    }
    std::map<RuleKey, Counts> rules;
    std::ostringstream cases; bool first = true;
    for (const auto& label : labels) {
        const Prediction key{label.rule, label.path};
        const bool detected = label.detector == "quality" ? quality_predictions.contains(key) : lexical.at(label.profile).contains(key);
        rules[{label.detector, label.rule, label.profile}].observe(label.positive, detected);
        if (!first) cases << ','; first = false;
        cases << "{\"id\":" << quote(label.id) << ",\"detector\":" << quote(label.detector)
            << ",\"rule\":" << quote(label.rule) << ",\"path\":" << quote(label.path)
            << ",\"profile\":" << quote(label.profile)
            << ",\"label\":" << quote(label.positive ? "positive" : "negative")
            << ",\"detected\":" << (detected ? "true" : "false") << ",\"outcome\":" << quote(outcome(label, detected))
            << ",\"provenance\":" << quote(label.provenance) << ",\"rationale\":" << quote(label.rationale) << '}';
    }
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(8);
    out << "{\"schema_version\":1,\"unit\":\"detector/rule/path/profile\",\"labels\":" << labels.size()
        << ",\"limitations\":\"Small manually labeled corpus; lexical review candidates, not defect proof; "
           "unlabeled rule/path pairs excluded; external validity and semantic-rule precision UNVERIFIED\",\"rules\":[";
    first = true;
    for (const auto& [key, counts] : rules) {
        if (!first) out << ','; first = false;
        out << "{\"detector\":" << quote(std::get<0>(key)) << ",\"rule\":" << quote(std::get<1>(key))
            << ",\"profile\":" << quote(std::get<2>(key)) << ",\"tp\":" << counts.tp << ",\"fp\":" << counts.fp
            << ",\"fn\":" << counts.fn << ",\"tn\":" << counts.tn << ",\"precision\":";
        ratio(out, counts.precision()); out << ",\"recall\":"; ratio(out, counts.recall()); out << '}';
    }
    out << "],\"cases\":[" << cases.str() << "],\"regression_check\":{\"requested\":" << (check ? "true" : "false");
    bool accepted = true;
    std::ostringstream checks;
    if (check) {
        std::ifstream bounds_file(root / "bounds.tsv");
        if (!bounds_file) throw std::runtime_error("cannot open corpus bounds");
        const auto bounds = policy(bounds_file);
        if (bounds.size() != rules.size()) throw std::runtime_error("bounds must cover exactly every measured detector/rule/profile");
        first = true;
        for (const auto& [key, counts] : rules) {
            const auto found = bounds.find(key);
            if (found == bounds.end()) throw std::runtime_error("missing bounds for measured rule/profile");
            const auto& bound = found->second; const bool pass = bound.accepts(counts);
            accepted = accepted && pass;
            if (!first) checks << ','; first = false;
            checks << "{\"detector\":" << quote(std::get<0>(key)) << ",\"rule\":" << quote(std::get<1>(key))
                << ",\"profile\":" << quote(std::get<2>(key)) << ",\"min_tp\":" << bound.min_tp
                << ",\"max_fp\":" << bound.max_fp << ",\"max_fn\":" << bound.max_fn
                << ",\"status\":" << quote(pass ? "PASS" : "FAIL") << '}';
        }
    }
    out << ",\"status\":" << quote(check ? (accepted ? "PASS" : "FAIL") : "UNVERIFIED")
        << ",\"rules\":[" << checks.str() << "]}}\n";
    return {out.str(), accepted};
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") { self_test(); return 0; }
        const bool check = argc > 1 && std::string_view(argv[1]) == "--check";
        const int root_arg = check ? 2 : 1;
        if (argc <= root_arg || argc > root_arg + 2)
            throw std::runtime_error("usage: cpp-rails-corpus [--check] CORPUS_ROOT [OUTPUT.json] | --self-test");
        const auto output = run(cpprails::path_from_utf8(argv[root_arg]), check);
        if (argc == root_arg + 2) {
            std::ofstream file(cpprails::path_from_utf8(argv[root_arg + 1]), std::ios::binary); file << output.json;
            if (!file) throw std::runtime_error("cannot write corpus output");
        }
        std::cout << output.json; return output.accepted ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << "corpus error: " << e.what() << '\n'; return 2; }
}
