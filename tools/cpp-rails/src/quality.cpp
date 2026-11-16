#include "quality.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace cpprails {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t no_match = std::numeric_limits<std::size_t>::max();
struct Token { std::string text; std::size_t line; bool literal = false; };
struct Source {
    fs::path absolute;
    std::string path;
    bool test = false;
    std::vector<Token> tokens;
    std::set<std::size_t> lines;
};
std::string utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool ends(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}
bool test_path(const fs::path& path) {
    static const std::set<std::string> directories = {"test", "tests", "testing", "unittest", "unittests",
        "fixture", "fixtures", "testdata", "test_data", "mock", "mocks"};
    for (const auto& part : path.parent_path()) if (directories.contains(lower(utf8(part)))) return true;
    const auto stem = utf8(path.stem()); const auto low = lower(stem);
    for (const auto suffix : {"_test", "_tests", "_unittest", "_unittests", ".test", "_mock", "_fixture"})
        if (ends(low, suffix)) return true;
    return low.starts_with("test_") || low.starts_with("mock_") || ends(stem, "Test") || ends(stem, "Tests");
}
bool identifier(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Literal payloads remain opaque tokens: syntax inside them cannot create signals,
// but different literal values must never be called exact duplicates.
void tokenize(Source& source, const std::string& text) {
    std::size_t pos = 0, line = 1;
    auto skip = [&](std::size_t end) {
        while (pos < end) if (text[pos++] == '\n') ++line;
    };
    auto emit = [&](std::size_t end, bool literal = false) {
        const auto start = pos; const auto first_line = line;
        while (pos < end) {
            if (!std::isspace(static_cast<unsigned char>(text[pos]))) source.lines.insert(line);
            if (text[pos++] == '\n') ++line;
        }
        source.tokens.push_back({text.substr(start, end - start), first_line, literal});
    };
    while (pos < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[pos]))) { skip(pos + 1); continue; }
        if (text.compare(pos, 2, "//") == 0) {
            auto end = text.find('\n', pos + 2);
            // Translation-phase line splices continue a line comment.
            while (end != std::string::npos && end > pos &&
                   (text[end - 1] == '\\' || (end > pos + 1 && text[end - 1] == '\r' && text[end - 2] == '\\')))
                end = text.find('\n', end + 1);
            skip(end == std::string::npos ? text.size() : end); continue;
        }
        if (text.compare(pos, 2, "/*") == 0) {
            const auto end = text.find("*/", pos + 2);
            skip(end == std::string::npos ? text.size() : end + 2); continue;
        }
        bool found_literal = false;
        for (const std::string_view prefix : {"u8R\"", "uR\"", "UR\"", "LR\"", "R\""}) {
            if (text.compare(pos, prefix.size(), prefix) != 0) continue;
            const auto delimiter_start = pos + prefix.size();
            const auto open = text.find('(', delimiter_start);
            if (open == std::string::npos || open - delimiter_start > 16) continue;
            const auto delimiter = text.substr(delimiter_start, open - delimiter_start);
            if (delimiter.find_first_of(" \t\r\n\\)") != std::string::npos) continue;
            const auto closing = ")" + delimiter + "\"";
            const auto end = text.find(closing, open + 1);
            emit(end == std::string::npos ? text.size() : end + closing.size(), true);
            found_literal = true; break;
        }
        if (found_literal) continue;
        std::size_t quote = pos;
        for (const std::string_view prefix : {"u8", "u", "U", "L"}) {
            if (text.compare(pos, prefix.size(), prefix) == 0 && pos + prefix.size() < text.size() &&
                (text[pos + prefix.size()] == '\"' || text[pos + prefix.size()] == '\'')) {
                quote = pos + prefix.size(); break;
            }
        }
        if (text[quote] == '\"' || text[quote] == '\'') {
            const char close = text[quote]; auto end = quote + 1;
            while (end < text.size()) {
                if (text[end] == '\\') { end += std::min<std::size_t>(2, text.size() - end); continue; }
                if (text[end++] == close) break;
            }
            emit(end, true); continue;
        }
        if (std::isdigit(static_cast<unsigned char>(text[pos]))) {
            auto end = pos + 1;
            while (end < text.size() && (identifier(text[end]) || text[end] == '.' ||
                (text[end] == '\'' && end + 1 < text.size() && identifier(text[end + 1])))) ++end;
            emit(end); continue;
        }
        if (identifier(text[pos])) {
            auto end = pos + 1; while (end < text.size() && identifier(text[end])) ++end;
            emit(end); continue;
        }
        const auto pair = text.substr(pos, 2);
        if (pair == "&&" || pair == "||" || pair == "::" || pair == "->" || pair == "++" ||
            pair == "--" || pair == "==" || pair == "!=" || pair == "<=" || pair == ">=") emit(pos + 2);
        else emit(pos + 1);
    }
}

void evidence(QualityDimension& dimension, const Source& source, std::size_t line, std::string message) {
    dimension.evidence.push_back({source.path, line, std::move(message)});
}
void duplicates(const std::vector<Source>& sources, QualityDimension& dimension) {
    struct Origin { std::size_t file, begin, end; };
    std::map<std::string, Origin> blocks;
    std::vector<std::size_t> reported_end(sources.size(), 0);
    for (std::size_t file = 0; file < sources.size(); ++file) {
        const auto& source = sources[file]; if (source.test) continue;
        std::vector<std::pair<std::size_t, std::vector<std::string>>> lines;
        for (const auto& token : source.tokens) {
            if (lines.empty() || lines.back().first != token.line) lines.push_back({token.line, {}});
            lines.back().second.push_back(token.text);
        }
        for (std::size_t begin = 0; begin + 6 <= lines.size(); ++begin) {
            std::string key; std::size_t tokens = 0;
            for (std::size_t k = begin; k < begin + 6; ++k) for (const auto& token : lines[k].second) {
                key += std::to_string(token.size()) + ':' + token; ++tokens;
            }
            if (tokens < 30) continue;
            const Origin here{file, lines[begin].first, lines[begin + 5].first};
            const auto [it, inserted] = blocks.emplace(std::move(key), here);
            if (inserted) continue;
            const auto previous = it->second;
            if (previous.file == file && previous.end >= here.begin) continue;
            if (reported_end[file] >= here.begin) continue;
            reported_end[file] = here.end;
            ++dimension.signals;
            evidence(dimension, source, here.begin, "Exact token block: 6 code-bearing lines, " +
                std::to_string(tokens) + " tokens; first occurrence " + sources[previous.file].path + ':' +
                std::to_string(previous.begin) + "; lexical duplication, necessity UNVERIFIED");
        }
    }
}

std::vector<std::size_t> pairs(const std::vector<Token>& tokens, std::string_view open, std::string_view close) {
    std::vector<std::size_t> matches(tokens.size(), no_match), stack;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].literal) continue;
        if (tokens[i].text == open) stack.push_back(i);
        else if (tokens[i].text == close && !stack.empty()) {
            matches[i] = stack.back(); matches[stack.back()] = i; stack.pop_back();
        }
    }
    return matches;
}
void structural(const Source& source, QualityDimension& complexity, QualityDimension& responsibility,
                QualityDimension& indirection) {
    if (source.test) return;
    const auto& tokens = source.tokens;
    const auto parens = pairs(tokens, "(", ")"); const auto braces = pairs(tokens, "{", "}");
    static const std::set<std::string> controls = {"if", "for", "while", "switch", "catch"};
    std::size_t virtuals = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].literal) continue;
        if (tokens[i].text == "virtual") ++virtuals;
        if ((tokens[i].text == "class" || tokens[i].text == "struct") &&
            (i == 0 || tokens[i - 1].text != "enum")) {
            // `template<class T>` introduces a parameter, not the next braced class.
            if (i + 1 < tokens.size() && (tokens[i + 1].text == ">" || tokens[i + 1].text == ".")) continue;
            if (i + 2 < tokens.size() && (tokens[i + 2].text == ">" || tokens[i + 2].text == "," ||
                                         tokens[i + 2].text == "=")) continue;
            auto open = i + 1;
            while (open < tokens.size() && tokens[open].text != ";" && tokens[open].text != "{") ++open;
            if (open < tokens.size() && tokens[open].text == "{" && braces[open] != no_match) {
                const auto close = braces[open]; std::size_t methods = 0, depth = 0;
                for (auto k = open + 1; k < close; ++k) {
                    if (tokens[k].literal) continue;
                    if (tokens[k].text == "{") ++depth;
                    else if (tokens[k].text == "}" && depth) --depth;
                    else if (depth == 0 && tokens[k].text == "(" && k > 0 &&
                             identifier(tokens[k - 1].text.front()) && !controls.contains(tokens[k - 1].text)) ++methods;
                }
                const auto span = tokens[close].line - tokens[open].line + 1;
                if (methods >= 20 || span > 400) {
                    ++responsibility.signals;
                    evidence(responsibility, source, tokens[i].line, "Class surface proxy: method-like=" +
                        std::to_string(methods) + ", physical span=" + std::to_string(span) +
                        "; cohesion/domains/responsibilities UNVERIFIED; facade/ABI may justify this surface");
                }
            }
        }
        if (tokens[i].text != "{" || braces[i] == no_match || i == 0) continue;
        auto before = i - 1;
        while (before && (tokens[before].text == "const" || tokens[before].text == "noexcept" ||
            tokens[before].text == "override" || tokens[before].text == "final" || tokens[before].text == "&")) --before;
        if (tokens[before].text != ")" || parens[before] == no_match || parens[before] == 0) continue;
        const auto name = parens[before] - 1;
        if (!identifier(tokens[name].text.front()) || controls.contains(tokens[name].text)) continue;
        const auto close = braces[i];
        std::size_t branches = 0, depth = 0, max_depth = 0;
        for (auto k = i + 1; k < close; ++k) {
            if (tokens[k].literal) continue;
            const auto& text = tokens[k].text;
            if (text == "if" || text == "for" || text == "while" || text == "case" ||
                text == "catch" || text == "&&" || text == "||" || text == "?") ++branches;
            if (text == "{") max_depth = std::max(max_depth, ++depth);
            else if (text == "}" && depth) --depth;
        }
        if (branches >= 10 || max_depth >= 4) {
            ++complexity.signals;
            evidence(complexity, source, tokens[name].line, "Function-like region " + tokens[name].text +
                ": branches=" + std::to_string(branches) + ", brace nesting=" + std::to_string(max_depth) +
                "; lexical proxy, semantic complexity UNVERIFIED");
        }
        i = close; // Each outer function-like body is counted once, including nested lambdas.
    }
    indirection.signals += virtuals;
    if (virtuals) evidence(indirection, source, 1, "Inventory: " + std::to_string(virtuals) +
        " virtual keyword(s); necessity, runtime dispatch, facade/PImpl/ABI contracts UNVERIFIED; no penalty");
}

void dependencies(const fs::path& root, const std::vector<Source>& sources, QualityDimension& dimension) {
    struct Edge { std::size_t target, line; };
    std::map<fs::path, std::size_t> by_path;
    for (std::size_t i = 0; i < sources.size(); ++i) if (!sources[i].test) by_path[sources[i].absolute] = i;
    std::vector<std::vector<Edge>> graph(sources.size());
    for (std::size_t file = 0; file < sources.size(); ++file) {
        const auto& source = sources[file]; if (source.test) continue;
        const auto& tokens = source.tokens; std::set<std::size_t> targets; std::size_t unresolved = 0;
        for (std::size_t i = 0; i + 2 < tokens.size(); ++i) {
            if (tokens[i].text != "#" || tokens[i + 1].text != "include" ||
                tokens[i].line != tokens[i + 1].line ||
                (i && tokens[i - 1].line == tokens[i].line)) continue;
            std::string include; const auto& path = tokens[i + 2];
            if (path.literal && path.text.starts_with('"') && path.text.ends_with('"'))
                include = path.text.substr(1, path.text.size() - 2);
            else if (path.text == "<") {
                for (auto k = i + 3; k < tokens.size() && tokens[k].line == path.line; ++k) {
                    if (tokens[k].text == ">") break;
                    include += tokens[k].text;
                }
            }
            if (include.empty()) { ++unresolved; continue; }
            const fs::path included = fs::path(std::u8string(include.begin(), include.end()));
            auto target = by_path.find((source.absolute.parent_path() / included).lexically_normal());
            if (target == by_path.end()) target = by_path.find((root / included).lexically_normal());
            if (target == by_path.end()) { ++unresolved; continue; }
            if (!targets.insert(target->second).second) continue;
            graph[file].push_back({target->second, tokens[i].line}); ++dimension.signals;
            evidence(dimension, source, tokens[i].line, "Resolved include edge -> " +
                sources[target->second].path + "; preprocessing/build dependency UNVERIFIED");
        }
        evidence(dimension, source, 1, "Supplied-source include fanout=" + std::to_string(targets.size()) +
            ", unresolved/external directives=" + std::to_string(unresolved) +
            "; configured include roots, macros and conditional activation UNVERIFIED");
    }
    // Iterative DFS avoids recursive stack growth on long include chains.
    std::vector<unsigned char> state(sources.size(), 0);
    for (std::size_t start = 0; start < sources.size(); ++start) {
        if (state[start] || sources[start].test) continue;
        std::vector<std::pair<std::size_t, std::size_t>> stack{{start, 0}}; state[start] = 1;
        while (!stack.empty()) {
            auto& [node, index] = stack.back();
            if (index == graph[node].size()) { state[node] = 2; stack.pop_back(); continue; }
            const auto edge = graph[node][index++];
            if (state[edge.target] == 1) evidence(dimension, sources[node], edge.line,
                "Lexical include cycle back-edge -> " + sources[edge.target].path +
                "; include guards/conditional activation/build impact UNVERIFIED");
            else if (state[edge.target] == 0) { state[edge.target] = 1; stack.push_back({edge.target, 0}); }
        }
    }
}

std::string test_stem(const Source& source) {
    auto stem = lower(utf8(source.absolute.stem()));
    for (const auto suffix : {"_unittests", "_unittest", "_tests", "_test", ".test", "tests", "test"})
        if (ends(stem, suffix)) { stem.resize(stem.size() - std::string_view(suffix).size()); break; }
    if (stem.starts_with("test_")) stem.erase(0, 5);
    return stem;
}
void proximity(const std::vector<Source>& sources, QualityDimension& dimension) {
    std::map<std::string, std::vector<std::string>> tests;
    for (const auto& source : sources) if (source.test) tests[test_stem(source)].push_back(source.path);
    for (const auto& source : sources) {
        if (source.test) continue;
        const auto found = tests.find(lower(utf8(source.absolute.stem())));
        if (found == tests.end()) evidence(dimension, source, 1,
            "No same-stem test in supplied scope for " + source.path + "; coverage UNVERIFIED");
        else {
            ++dimension.signals; std::string names;
            for (const auto& name : found->second) { if (!names.empty()) names += ", "; names += name; }
            evidence(dimension, source, 1, "Same-stem test path(s): " + names +
                "; name proximity only; execution/assertions/coverage UNVERIFIED");
        }
    }
}
std::string json_string(std::string_view text) {
    std::ostringstream out; out << '"';
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << c;
    }
    out << '"'; return out.str();
}
std::string markdown_cell(std::string_view text) {
    std::string result;
    for (const char c : text) {
        if (c == '\n' || c == '\r') result += ' ';
        else { if (c == '|' || c == '\\' || c == '`' || c == '<' || c == '>') result += '\\'; result += c; }
    }
    return result;
}
} // namespace

QualityReport quality_score(const fs::path& supplied_root, const std::vector<fs::path>& files) {
    QualityReport report;
    for (const auto name : {"duplication", "complexity", "responsibilities", "indirection", "dependencies", "test-proximity"})
        report.dimensions.push_back({name, "UNVERIFIED", std::nullopt, 0, {}});
    const auto root = fs::absolute(supplied_root).lexically_normal();
    std::set<fs::path> unique;
    for (const auto& path : files) unique.insert(fs::absolute(path.is_absolute() ? path : root / path).lexically_normal());
    std::vector<Source> sources;
    for (const auto& path : unique) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("quality: cannot read " + utf8(path));
        std::ostringstream bytes; bytes << input.rdbuf();
        if (input.bad() || bytes.bad()) throw std::runtime_error("quality: read failed " + utf8(path));
        const auto relative = path.lexically_relative(root);
        Source source{path, utf8(relative.empty() ? path : relative), test_path(relative), {}, {}};
        tokenize(source, bytes.str());
        report.code_lines += source.lines.size();
        (source.test ? report.test_lines : report.production_lines) += source.lines.size();
        sources.push_back(std::move(source));
    }
    report.files = sources.size();
    duplicates(sources, report.dimensions[0]);
    for (const auto& source : sources) structural(source, report.dimensions[1], report.dimensions[2], report.dimensions[3]);
    dependencies(root, sources, report.dimensions[4]); proximity(sources, report.dimensions[5]);
    if (report.production_lines) for (const auto index : {0u, 1u, 4u})
        report.dimensions[index].score = report.dimensions[index].signals * 1000.0 / report.production_lines;
    for (auto& dimension : report.dimensions) if (dimension.evidence.empty())
        dimension.evidence.push_back({"", 0, report.production_lines == 0 ?
            "No production code in supplied scope; semantic quality UNVERIFIED" :
            "No lexical candidates under this dimension's documented proxy; semantic quality UNVERIFIED"});
    return report;
}

std::string quality_json(const QualityReport& report) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(12);
    out << "{\"code_lines\":" << report.code_lines << ",\"production_lines\":" << report.production_lines
        << ",\"test_lines\":" << report.test_lines << ",\"files\":" << report.files
        << ",\"score_unit\":\"signals / production KLOC\",\"dimensions\":[";
    bool first = true;
    for (const auto& dimension : report.dimensions) {
        if (!first) out << ','; first = false;
        out << "{\"name\":" << json_string(dimension.name) << ",\"status\":" << json_string(dimension.status)
            << ",\"score\":";
        if (dimension.score) out << *dimension.score; else out << "null";
        out << ",\"signals\":" << dimension.signals << ",\"evidence\":[";
        bool first_evidence = true;
        for (const auto& item : dimension.evidence) {
            if (!first_evidence) out << ','; first_evidence = false;
            out << "{\"path\":" << json_string(item.path) << ",\"line\":" << item.line
                << ",\"message\":" << json_string(item.message) << '}';
        }
        out << "]}";
    }
    out << "]}\n"; return out.str();
}

std::string quality_markdown(const QualityReport& report) {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::fixed << std::setprecision(2);
    out << "# C++ quality evidence\n\nFiles: " << report.files << "; code lines: " << report.code_lines
        << "; production: " << report.production_lines << "; tests: " << report.test_lines
        << ".\n\nIndependent lexical indicators; no global grade. Numeric unit: signals / production KLOC. "
           "Zero candidates do not establish quality. Semantic quality and test coverage remain UNVERIFIED.\n\n"
           "| Dimension | Status | Signals | Signals / production KLOC |\n|---|---|---:|---:|\n";
    for (const auto& dimension : report.dimensions) {
        out << '|' << markdown_cell(dimension.name) << '|' << markdown_cell(dimension.status) << '|'
            << dimension.signals << '|';
        if (dimension.score) out << *dimension.score; else out << "UNVERIFIED";
        out << "|\n";
    }
    for (const auto& dimension : report.dimensions) {
        out << "\n## " << markdown_cell(dimension.name) << "\n\n";
        for (const auto& item : dimension.evidence) {
            out << "- "; if (!item.path.empty()) out << markdown_cell(item.path) << ':' << item.line << ": ";
            out << markdown_cell(item.message) << '\n';
        }
    }
    return out.str();
}
} // namespace cpprails
