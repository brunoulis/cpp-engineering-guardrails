#include "architecture.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cpprails {
namespace {
std::string bytes(const fs::path &p) {
  return utf8_bytes(p.generic_u8string());
}
std::string read(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  if (!in)
    throw std::runtime_error("cannot read " + bytes(p));
  std::string out;
  char data[8192];
  while (in.read(data, sizeof data) || in.gcount()) {
    out.append(data, static_cast<std::size_t>(in.gcount()));
    if (out.size() > 16 * 1024 * 1024)
      throw std::runtime_error("architecture input exceeds 16 MiB: " +
                               bytes(p));
  }
  if (!in.eof())
    throw std::runtime_error("read failed: " + bytes(p));
  return out;
}
std::string trim(std::string s) {
  const auto a = s.find_first_not_of(" \t\r\n"),
             b = s.find_last_not_of(" \t\r\n");
  return a == s.npos ? "" : s.substr(a, b - a + 1);
}
std::string quote(const std::string &s) {
  std::string out = "\"";
  const char *hex = "0123456789abcdef";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 32) {
      out += "\\u00";
      out += hex[c >> 4];
      out += hex[c & 15];
    } else
      out += static_cast<char>(c);
  }
  return out + '"';
}
// Small bounded JSON reader for CMake's File API. No evaluation, shell
// expansion or dependencies.
struct Json {
  enum Type { Null, Bool, Number, String, Array, Object } type = Null;
  std::string text;
  std::vector<Json> array;
  std::map<std::string, Json> object;
  const Json &at(const std::string &key) const {
    auto it = object.find(key);
    if (type != Object || it == object.end())
      throw std::runtime_error("missing JSON object member: " + key);
    return it->second;
  }
  const Json &optional(const std::string &key) const {
    static const Json empty;
    auto it = object.find(key);
    return it == object.end() ? empty : it->second;
  }
  const std::vector<Json> &list() const {
    if (type != Array)
      throw std::runtime_error("expected JSON array");
    return array;
  }
  std::string str() const {
    if (type != String)
      throw std::runtime_error("expected JSON string");
    return text;
  }
  unsigned number() const {
    if (type != Number || text.empty() ||
        text.find_first_not_of("0123456789") != text.npos)
      throw std::runtime_error("expected JSON unsigned integer");
    return static_cast<unsigned>(std::stoul(text));
  }
};
class JsonReader {
  const std::string &s;
  std::size_t pos = 0;
  [[noreturn]] void bad() const {
    throw std::runtime_error("malformed CMake JSON at byte " +
                             std::to_string(pos));
  }
  void ws() {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])))
      ++pos;
  }
  unsigned hex4() {
    unsigned n = 0;
    for (int i = 0; i < 4; ++i) {
      if (pos == s.size())
        bad();
      char c = s[pos++];
      n *= 16;
      if (c >= '0' && c <= '9')
        n += c - '0';
      else if (c >= 'a' && c <= 'f')
        n += c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        n += c - 'A' + 10;
      else
        bad();
    }
    return n;
  }
  static void utf8(std::string &out, unsigned n) {
    if (n < 128)
      out += static_cast<char>(n);
    else if (n < 2048) {
      out += static_cast<char>(192 | (n >> 6));
      out += static_cast<char>(128 | (n & 63));
    } else if (n < 65536) {
      out += static_cast<char>(224 | (n >> 12));
      out += static_cast<char>(128 | ((n >> 6) & 63));
      out += static_cast<char>(128 | (n & 63));
    } else {
      out += static_cast<char>(240 | (n >> 18));
      out += static_cast<char>(128 | ((n >> 12) & 63));
      out += static_cast<char>(128 | ((n >> 6) & 63));
      out += static_cast<char>(128 | (n & 63));
    }
  }
  std::string string() {
    if (pos == s.size() || s[pos++] != '"')
      bad();
    std::string out;
    while (pos < s.size()) {
      unsigned char c = s[pos++];
      if (c == '"')
        return out;
      if (c < 32)
        bad();
      if (c != '\\') {
        out += static_cast<char>(c);
        continue;
      }
      if (pos == s.size())
        bad();
      switch (s[pos++]) {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case '/':
        out += '/';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        auto n = hex4();
        if (n >= 0xd800 && n <= 0xdbff) {
          if (pos + 2 > s.size() || s.substr(pos, 2) != "\\u")
            bad();
          pos += 2;
          auto low = hex4();
          if (low < 0xdc00 || low > 0xdfff)
            bad();
          n = 0x10000 + ((n - 0xd800) << 10) + (low - 0xdc00);
        } else if (n >= 0xdc00 && n <= 0xdfff)
          bad();
        utf8(out, n);
        break;
      }
      default:
        bad();
      }
    }
    bad();
  }
  Json value(unsigned depth) {
    if (depth > 64)
      bad();
    ws();
    if (pos == s.size())
      bad();
    Json j;
    char c = s[pos];
    if (c == '"') {
      j.type = Json::String;
      j.text = string();
      return j;
    }
    if (c == '{' || c == '[') {
      const bool obj = c == '{';
      ++pos;
      j.type = obj ? Json::Object : Json::Array;
      ws();
      if (pos < s.size() && s[pos] == (obj ? '}' : ']')) {
        ++pos;
        return j;
      }
      while (true) {
        std::string key;
        if (obj) {
          ws();
          key = string();
          ws();
          if (pos == s.size() || s[pos++] != ':')
            bad();
        }
        auto child = value(depth + 1);
        if (obj) {
          if (!j.object.emplace(key, std::move(child)).second)
            bad();
        } else
          j.array.push_back(std::move(child));
        ws();
        if (pos == s.size())
          bad();
        char next = s[pos++];
        if (next == (obj ? '}' : ']'))
          return j;
        if (next != ',')
          bad();
      }
    }
    for (auto word : {"null", "true", "false"}) {
      const std::string w = word;
      if (s.compare(pos, w.size(), w) == 0) {
        pos += w.size();
        j.type = w == "null" ? Json::Null : Json::Bool;
        j.text = w;
        return j;
      }
    }
    const auto start = pos;
    if (c == '-')
      ++pos;
    if (pos == s.size())
      bad();
    if (s[pos] == '0')
      ++pos;
    else {
      if (s[pos] < '1' || s[pos] > '9')
        bad();
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
        ++pos;
    }
    if (pos < s.size() && s[pos] == '.') {
      ++pos;
      const auto begin = pos;
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
        ++pos;
      if (begin == pos)
        bad();
    }
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
      ++pos;
      if (pos < s.size() && (s[pos] == '+' || s[pos] == '-'))
        ++pos;
      const auto begin = pos;
      while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
        ++pos;
      if (begin == pos)
        bad();
    }
    j.type = Json::Number;
    j.text = s.substr(start, pos - start);
    return j;
  }

public:
  explicit JsonReader(const std::string &text) : s(text) {}
  Json parse() {
    auto j = value(0);
    ws();
    if (pos != s.size())
      bad();
    return j;
  }
};
Json json_file(const fs::path &p) {
  const auto s = read(p);
  return JsonReader(s).parse();
}
fs::path reply_file(const fs::path &reply, const std::string &file) {
  const auto p = path_from_utf8(file);
  if (p.has_parent_path() || p.is_absolute() || p.extension() != ".json")
    throw std::runtime_error("unsafe File API reference");
  const auto path = fs::weakly_canonical(reply / p);
  if (path.parent_path() != fs::weakly_canonical(reply))
    throw std::runtime_error("File API reference escapes reply directory");
  return path;
}
struct Edge {
  std::string from, to, visibility = "unknown", evidence;
};
struct Target {
  std::string id, name, type, path;
  std::vector<std::string> sources, public_headers, public_header_candidates;
};
struct Graph {
  std::map<std::string, Target> targets;
  std::vector<Edge> edges;
  std::string analyzer = "cmake-literal", level = "heuristic", config, evidence;
  bool verified = false, dynamic = false;
};
bool internal(const std::string &path) {
  static const std::regex r(R"((^|/)(internal|detail|private)(/|$))");
  return std::regex_search(path, r);
}
Finding finding(const std::string &rule, const std::string &path,
                const std::string &message, const std::string &evidence,
                const Graph &graph) {
  Finding f;
  f.rule = rule;
  f.path = path;
  f.message = message;
  f.evidence = evidence;
  f.severity = "P2";
  f.confidence = graph.verified ? "confirmed-structure" : "suspected";
  f.analyzer = graph.analyzer;
  f.evidence_level = graph.level;
  f.recommendation =
      "Review the boundary against the project's intended architecture; "
      "structural evidence does not prove a runtime defect.";
  f.id = make_id(rule, path, evidence, 0);
  return f;
}
Graph codemodel(const Options &o, const fs::path &root) {
  Graph g;
  auto build = o.build_dir.value_or(fs::path("build"));
  if (build.is_relative())
    build = root / build;
  const auto reply = build / ".cmake/api/v1/reply";
  if (!fs::is_directory(reply))
    throw std::runtime_error("existing CMake File API codemodel reply missing");
  fs::path index, error_index;
  for (const auto &f : fs::directory_iterator(reply)) {
    const auto name = bytes(f.path().filename());
    if (name.starts_with("index-") && f.path().extension() == ".json" &&
        (index.empty() || name > bytes(index.filename())))
      index = f.path();
    if (name.starts_with("error-") && f.path().extension() == ".json" &&
        (error_index.empty() ||
         fs::last_write_time(f.path()) > fs::last_write_time(error_index)))
      error_index = f.path();
  }
  if (index.empty())
    throw std::runtime_error("File API index missing");
  if (!error_index.empty() &&
      fs::last_write_time(error_index) >= fs::last_write_time(index))
    throw std::runtime_error("CMake File API reports a generation error newer "
                             "than the successful snapshot");
  const auto idx = json_file(index);
  const Json *ref = nullptr;
  for (const auto &item : idx.at("objects").list())
    if (item.at("kind").str() == "codemodel" &&
        item.at("version").at("major").number() == 2) {
      ref = &item;
      break;
    }
  if (!ref)
    throw std::runtime_error("codemodel v2 absent in current File API index");
  const auto modelpath = reply_file(reply, ref->at("jsonFile").str());
  const auto model = json_file(modelpath);
  const auto source = path_from_utf8(model.at("paths").at("source").str());
  if (fs::weakly_canonical(source) != fs::weakly_canonical(root))
    throw std::runtime_error("codemodel source root differs from --root");
  const auto &configurations = model.at("configurations").list();
  const Json *config = nullptr;
  for (const auto &item : configurations) {
    if (o.config && item.at("name").str() == *o.config) {
      config = &item;
      break;
    }
  }
  if (!config && !o.config && configurations.size() == 1)
    config = &configurations.front();
  if (!config)
    throw std::runtime_error(
        "select an existing File API configuration with --config");
  g.config = config->at("name").str();
  std::vector<std::pair<std::string, Json>> target_json;
  const auto &target_refs = config->optional("targets");
  const std::vector<Json> empty_targets;
  const auto &target_list =
      target_refs.type == Json::Null ? empty_targets : target_refs.list();
  if (target_list.size() > 4096)
    throw std::runtime_error("target topology limit exceeded (4096 targets)");
  for (const auto &item : target_list) {
    const auto path = reply_file(reply, item.at("jsonFile").str());
    auto data = json_file(path);
    Target t;
    t.id = data.at("id").str();
    t.name = data.at("name").str();
    t.type = data.at("type").str();
    t.path = relative_path(path, root);
    if (t.id != item.at("id").str() || t.name != item.at("name").str())
      throw std::runtime_error("inconsistent File API target reference");
    const auto &sets = data.optional("fileSets");
    if (sets.type != Json::Null && sets.type != Json::Array)
      throw std::runtime_error("invalid File API fileSets array");
    if (sets.type == Json::Array)
      for (const auto &set : sets.list()) {
        const auto visibility = set.at("visibility").str();
        if (visibility != "PUBLIC" && visibility != "PRIVATE" &&
            visibility != "INTERFACE")
          throw std::runtime_error("invalid File API file-set visibility");
      }
    const auto &sources = data.optional("sources");
    const auto &source_list =
        sources.type == Json::Null ? empty_targets : sources.list();
    for (const auto &src : source_list) {
      auto p = src.at("path").str();
      t.sources.push_back(p);
      const auto &n = src.optional("fileSetIndex");
      if (n.type != Json::Null) {
        const auto i = n.number();
        if (sets.type != Json::Array || i >= sets.array.size())
          throw std::runtime_error("invalid fileSetIndex");
        const auto visibility = sets.array[i].at("visibility").str();
        if (visibility == "PUBLIC" || visibility == "INTERFACE")
          t.public_headers.push_back(p);
      }
    }
    if (sets.type == Json::Array)
      for (const auto &src : source_list)
        if (src.optional("fileSetIndex").type == Json::Null) {
          const auto source_path = src.at("path").str();
          const auto ext = path_from_utf8(source_path).extension();
          if (ext != ".h" && ext != ".hpp" && ext != ".hh" && ext != ".hxx")
            continue;
          for (const auto &set : sets.list()) {
            const auto visibility = set.at("visibility").str();
            if (visibility != "PUBLIC" && visibility != "INTERFACE")
              continue;
            for (const auto &base : set.at("baseDirectories").list()) {
              const auto relative =
                  path_from_utf8(source_path)
                      .lexically_relative(path_from_utf8(base.str()));
              if (!relative.empty() && !relative.is_absolute() &&
                  *relative.begin() != "..")
                t.public_header_candidates.push_back(source_path);
            }
          }
        }
    if (!g.targets.emplace(t.id, t).second)
      throw std::runtime_error("duplicate File API target id");
    target_json.emplace_back(t.id, std::move(data));
  }
  for (const auto &[id, data] : target_json) {
    const auto &deps = data.optional("dependencies");
    if (deps.type == Json::Null)
      continue;
    for (const auto &dep : deps.list()) {
      const auto to = dep.at("id").str();
      if (!g.targets.count(to))
        throw std::runtime_error("unresolved File API target dependency id");
      if (g.edges.size() >= 65536)
        throw std::runtime_error(
            "target topology limit exceeded (65536 edges)");
      g.edges.push_back(
          {id, to, "unknown", g.targets.at(id).path + ":dependencies"});
    }
  }
  g.verified = true;
  g.level = "verified-structure";
  g.analyzer = "cmake-codemodel";
  g.evidence =
      relative_path(index, root) + " -> " + relative_path(modelpath, root);
  return g;
}
// Restricted literal fallback: comments/quotes are parsed, but CMake variables,
// control flow, macros, functions, genex and command side effects are
// deliberately never evaluated.
struct Command {
  std::string name;
  std::vector<std::string> args;
};
std::vector<Command> commands(const std::string &text, bool &dynamic) {
  std::string clean = text;
  bool quoted = false, escaped = false;
  for (std::size_t i = 0; i < clean.size(); ++i) {
    char c = clean[i];
    if (!quoted && c == '#') {
      const auto end = clean.find('\n', i);
      for (auto k = i; k < (end == clean.npos ? clean.size() : end); ++k)
        clean[k] = ' ';
      continue;
    }
    if (c == '"' && !escaped)
      quoted = !quoted;
    escaped = c == '\\' && !escaped;
  }
  std::vector<Command> out;
  std::size_t p = 0;
  while (p < clean.size()) {
    while (p < clean.size() &&
           std::isspace(static_cast<unsigned char>(clean[p])))
      ++p;
    const auto begin = p;
    while (
        p < clean.size() &&
        (std::isalnum(static_cast<unsigned char>(clean[p])) || clean[p] == '_'))
      ++p;
    auto name = clean.substr(begin, p - begin);
    while (p < clean.size() &&
           std::isspace(static_cast<unsigned char>(clean[p])))
      ++p;
    if (name.empty() || p == clean.size() || clean[p] != '(') {
      dynamic = true;
      if (p < clean.size())
        ++p;
      continue;
    }
    ++p;
    Command cmd;
    cmd.name = name;
    std::transform(
        cmd.name.begin(), cmd.name.end(), cmd.name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string token;
    quoted = false;
    escaped = false;
    unsigned depth = 1;
    bool active = false;
    while (p < clean.size() && depth) {
      char c = clean[p++];
      if (escaped) {
        token += c;
        escaped = false;
        active = true;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        dynamic = true;
        continue;
      }
      if (c == '"') {
        quoted = !quoted;
        active = true;
        continue;
      }
      if (!quoted && (c == '(' || c == ')')) {
        if (c == '(') {
          ++depth;
          dynamic = true;
          token += c;
        } else if (--depth)
          token += c;
        else if (active) {
          cmd.args.push_back(token);
          token.clear();
          active = false;
        }
        continue;
      }
      if (!quoted &&
          (std::isspace(static_cast<unsigned char>(c)) || c == ';')) {
        if (active) {
          cmd.args.push_back(token);
          token.clear();
          active = false;
        }
        continue;
      }
      token += c;
      active = true;
    }
    if (depth || quoted)
      dynamic = true;
    for (const auto &arg : cmd.args)
      if (arg.find('$') != arg.npos || arg.find('[') != arg.npos ||
          arg.find(']') != arg.npos)
        dynamic = true;
    out.push_back(std::move(cmd));
  }
  return out;
}
bool literal(const std::string &s) {
  return !s.empty() && s.find_first_of("$[];\\") == s.npos;
}
Graph fallback(const fs::path &root) {
  Graph g;
  const auto cmake = root / "CMakeLists.txt";
  if (!fs::is_regular_file(cmake)) {
    g.evidence = "root CMakeLists.txt missing";
    return g;
  }
  g.evidence = "CMakeLists.txt (literal, unevaluated)";
  const auto cmds = commands(read(cmake), g.dynamic);
  std::set<std::string> safe = {"cmake_minimum_required",
                                "project",
                                "add_library",
                                "add_executable",
                                "target_link_libraries",
                                "add_dependencies",
                                "target_sources"};
  for (const auto &cmd : cmds) {
    if (!safe.count(cmd.name))
      g.dynamic = true;
    if ((cmd.name == "add_library" || cmd.name == "add_executable") &&
        !cmd.args.empty() && literal(cmd.args[0])) {
      Target t;
      t.id = t.name = cmd.args[0];
      t.type = cmd.name == "add_library" ? "LIBRARY" : "EXECUTABLE";
      t.path = "CMakeLists.txt";
      for (std::size_t i = 1; i < cmd.args.size(); ++i)
        if (literal(cmd.args[i]) && cmd.args[i].find('.') != std::string::npos)
          t.sources.push_back(cmd.args[i]);
      g.targets.emplace(t.id, t);
      if (g.targets.size() > 4096)
        throw std::runtime_error(
            "literal topology limit exceeded (4096 targets)");
    }
  }
  for (const auto &cmd : cmds) {
    if (cmd.args.size() < 2 || !g.targets.count(cmd.args[0]))
      continue;
    if (cmd.name == "target_link_libraries" || cmd.name == "add_dependencies") {
      std::string visibility = "unknown";
      for (std::size_t i = 1; i < cmd.args.size(); ++i) {
        const auto &a = cmd.args[i];
        if (a == "PUBLIC" || a == "PRIVATE" || a == "INTERFACE") {
          visibility = a;
          continue;
        }
        if (g.targets.count(a)) {
          if (g.edges.size() >= 65536)
            throw std::runtime_error(
                "literal topology limit exceeded (65536 edges)");
          g.edges.push_back({cmd.args[0], a, visibility,
                             "CMakeLists.txt: literal " + cmd.name});
        }
      }
    }
  }
  return g;
}
std::vector<std::vector<std::string>> cycles(const Graph &g) {
  std::map<std::string, std::vector<std::string>> edges, reverse;
  for (const auto &e : g.edges) {
    edges[e.from].push_back(e.to);
    reverse[e.to].push_back(e.from);
  }
  // Iterative Kosaraju traversal keeps long dependency chains off the native
  // stack.
  std::set<std::string> seen;
  std::vector<std::string> order;
  for (const auto &[id, target] : g.targets) {
    std::vector<std::pair<std::string, bool>> pending{{id, false}};
    while (!pending.empty()) {
      auto [v, finish] = pending.back();
      pending.pop_back();
      if (finish) {
        order.push_back(v);
        continue;
      }
      if (!seen.insert(v).second)
        continue;
      pending.emplace_back(v, true);
      for (const auto &w : edges[v])
        if (!seen.count(w))
          pending.emplace_back(w, false);
    }
  }
  seen.clear();
  std::vector<std::vector<std::string>> result;
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    if (seen.count(*it))
      continue;
    std::vector<std::string> pending{*it}, group;
    while (!pending.empty()) {
      auto v = pending.back();
      pending.pop_back();
      if (!seen.insert(v).second)
        continue;
      group.push_back(v);
      for (const auto &w : reverse[v])
        if (!seen.count(w))
          pending.push_back(w);
    }
    if (group.size() > 1 || std::find(edges[*it].begin(), edges[*it].end(),
                                      *it) != edges[*it].end())
      result.push_back(std::move(group));
  }
  return result;
}
std::string joined(const std::vector<std::string> &v) {
  std::string s;
  for (auto &x : v) {
    if (!s.empty())
      s += ", ";
    s += x;
  }
  return s;
}
bool glob(const std::string &pattern, const std::string &value) {
  // Dynamic programming avoids regex backtracking for project-controlled globs.
  std::vector<bool> previous(value.size() + 1), next(value.size() + 1);
  previous[0] = true;
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    const char c = pattern[i];
    std::fill(next.begin(), next.end(), false);
    if (c == '*') {
      const bool recursive = i + 1 < pattern.size() && pattern[i + 1] == '*';
      if (recursive)
        ++i;
      next[0] = previous[0];
      for (std::size_t n = 1; n <= value.size(); ++n)
        next[n] =
            previous[n] || (next[n - 1] && (recursive || value[n - 1] != '/'));
    } else
      for (std::size_t n = 1; n <= value.size(); ++n)
        next[n] = previous[n - 1] &&
                  (c == value[n - 1] || (c == '?' && value[n - 1] != '/'));
    previous.swap(next);
  }
  return previous[value.size()];
}
struct Source {
  std::string code, comments_removed;
  std::vector<std::pair<std::size_t, std::string>> includes;
  bool unresolved_include = false;
};
Source source_text(const std::string &text) {
  Source s;
  s.code = s.comments_removed = text;
  auto mask = [&](std::size_t a, std::size_t b, bool comments) {
    for (auto i = a; i < b; ++i)
      if (text[i] != '\n') {
        s.code[i] = ' ';
        if (comments)
          s.comments_removed[i] = ' ';
      }
  };
  for (std::size_t i = 0; i < text.size();) {
    if (text.compare(i, 2, "//") == 0) {
      auto end = text.find('\n', i);
      while (end != text.npos && end > i &&
             (text[end - 1] == '\\' ||
              (text[end - 1] == '\r' && end > i + 1 && text[end - 2] == '\\')))
        end = text.find('\n', end + 1);
      if (end == text.npos)
        end = text.size();
      mask(i, end, true);
      i = end;
      continue;
    }
    if (text.compare(i, 2, "/*") == 0) {
      auto end = text.find("*/", i + 2);
      end = end == text.npos ? text.size() : end + 2;
      mask(i, end, true);
      i = end;
      continue;
    }
    if (text.compare(i, 2, "R\"") == 0) {
      const auto open = text.find('(', i + 2);
      if (open != text.npos && open - i <= 18) {
        const auto close = ")" + text.substr(i + 2, open - i - 2) + "\"";
        auto end = text.find(close, open + 1);
        end = end == text.npos ? text.size() : end + close.size();
        mask(i, end, false);
        i = end;
        continue;
      }
    }
    if (text[i] == '\'' && i > 0 && i + 1 < text.size() &&
        std::isalnum(static_cast<unsigned char>(text[i - 1])) &&
        std::isalnum(static_cast<unsigned char>(text[i + 1]))) {
      auto start = i;
      while (start > 0 &&
             (std::isalnum(static_cast<unsigned char>(text[start - 1])) ||
              text[start - 1] == '.' || text[start - 1] == '\''))
        --start;
      if (start < i && std::isdigit(static_cast<unsigned char>(text[start]))) {
        ++i;
        continue;
      }
    }
    if (text[i] == '"' || text[i] == '\'') {
      const auto begin = i;
      char q = text[i++];
      while (i < text.size()) {
        char c = text[i++];
        if (c == '\\' && i < text.size())
          ++i;
        else if (c == q)
          break;
      }
      mask(begin, i, false);
      continue;
    }
    ++i;
  }
  static const std::regex include(
      R"re(^\s*#\s*include\s*[<"]([^>"\r\n]+)[>"])re");
  static const std::regex directive(R"(^\s*#\s*include\b)");
  std::istringstream in(s.comments_removed), code_in(s.code);
  std::string line, code_line;
  std::size_t n = 0;
  while (std::getline(in, line)) {
    ++n;
    std::getline(code_in, code_line);
    if (!std::regex_search(code_line, directive))
      continue;
    std::smatch m;
    if (std::regex_search(line, m, include))
      s.includes.emplace_back(n, m[1].str());
    else
      s.unresolved_include = true;
  }
  return s;
}
struct Rule {
  std::string id, kind, from, to, path, pattern;
};
struct Exception {
  std::string rule, path, owner, reason, review, expires;
};
struct Policy {
  std::vector<Rule> rules;
  std::vector<Exception> exceptions;
};
std::string policy_string(const std::string &v) {
  if (v.size() < 2 || v.front() != '"' || v.back() != '"')
    throw std::runtime_error("policy values must be quoted strings");
  auto s = v.substr(1, v.size() - 2);
  if (trim(s).empty() || s.find_first_of("\"\\\r\n\t") != s.npos ||
      s.find('\0') != s.npos || s.size() > 1024)
    throw std::runtime_error("invalid policy string (no escapes)");
  return s;
}
void keys(const std::map<std::string, std::string> &table,
          std::initializer_list<const char *> expected) {
  std::set<std::string> allowed;
  for (auto key : expected)
    allowed.insert(key);
  if (table.size() != allowed.size())
    throw std::runtime_error("missing or unknown policy key");
  for (auto &[key, v] : table)
    if (!allowed.count(key))
      throw std::runtime_error("unknown policy key: " + key);
}
bool valid_date(const std::string &v) {
  if (!std::regex_match(v, std::regex(R"(\d{4}-\d{2}-\d{2})")))
    return false;
  const std::chrono::year_month_day date{
      std::chrono::year(std::stoi(v.substr(0, 4))),
      std::chrono::month(static_cast<unsigned>(std::stoi(v.substr(5, 2)))),
      std::chrono::day(static_cast<unsigned>(std::stoi(v.substr(8, 2))))};
  return date.ok() &&
         std::chrono::sys_days(date) >= std::chrono::floor<std::chrono::days>(
                                            std::chrono::system_clock::now());
}
Policy parse_policy(const std::string &text) {
  Policy p;
  std::string section;
  std::map<std::string, std::string> table;
  bool version = false;
  std::set<std::string> ids;
  auto flush = [&]() {
    if (section.empty())
      return;
    if (section == "rule") {
      if (!table.count("kind"))
        throw std::runtime_error("missing rule kind");
      Rule r;
      r.kind = table.at("kind");
      if (r.kind == "api-deny") {
        keys(table, {"id", "kind", "path", "pattern"});
        r.path = table.at("path");
        r.pattern = table.at("pattern");
        if (!std::regex_match(
                r.pattern,
                std::regex(
                    R"([A-Za-z_][A-Za-z_0-9]*(::[A-Za-z_][A-Za-z_0-9]*)*)")))
          throw std::runtime_error(
              "API pattern must be an identifier or qualified identifier");
      } else if (r.kind == "dependency-deny" || r.kind == "dependency-allow" ||
                 r.kind == "layer") {
        keys(table, {"id", "kind", "from", "to"});
        r.from = table.at("from");
        r.to = table.at("to");
      } else
        throw std::runtime_error("unknown rule kind");
      r.id = table.at("id");
      if (!std::regex_match(r.id,
                            std::regex(R"([A-Za-z0-9][A-Za-z0-9_.-]{0,99})")) ||
          !ids.insert(r.id).second)
        throw std::runtime_error("invalid/duplicate project rule id");
      p.rules.push_back(r);
      if (p.rules.size() > 256)
        throw std::runtime_error("project policy limit exceeded (256 rules)");
    } else {
      keys(table, {"rule", "path", "owner", "reason", "review", "expires"});
      Exception e{table.at("rule"),   table.at("path"),   table.at("owner"),
                  table.at("reason"), table.at("review"), table.at("expires")};
      if (!valid_date(e.expires))
        throw std::runtime_error("exception date invalid or expired");
      if (e.path.find_first_of("*?") != e.path.npos ||
          e.path.starts_with('/') || e.path.find("..") != e.path.npos)
        throw std::runtime_error(
            "exception path must be exact root-relative path");
      p.exceptions.push_back(e);
      if (p.exceptions.size() > 1024)
        throw std::runtime_error(
            "project policy limit exceeded (1024 exceptions)");
    }
    table.clear();
  };
  std::istringstream in(text);
  std::string line;
  std::size_t n = 0;
  while (std::getline(in, line)) {
    ++n;
    bool q = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '"')
        q = !q;
      if (line[i] == '#' && !q) {
        line.resize(i);
        break;
      }
    }
    line = trim(line);
    if (line.empty())
      continue;
    if (line == "[[rule]]" || line == "[[exception]]") {
      flush();
      section = line == "[[rule]]" ? "rule" : "exception";
      continue;
    }
    const auto equal = line.find('=');
    if (equal == line.npos)
      throw std::runtime_error("invalid policy at line " + std::to_string(n));
    auto key = trim(line.substr(0, equal)),
         value = trim(line.substr(equal + 1));
    if (section.empty()) {
      if (key != "version" || value != "1" || version)
        throw std::runtime_error(
            "unknown/duplicate top-level key or unsupported version");
      version = true;
    } else if (!table.emplace(key, policy_string(value)).second)
      throw std::runtime_error("duplicate policy key: " + key);
  }
  flush();
  if (!version || p.rules.empty())
    throw std::runtime_error(
        "policy requires version = 1 and at least one rule");
  for (auto &e : p.exceptions)
    if (!ids.count(e.rule))
      throw std::runtime_error("exception refers to undeclared project rule; "
                               "built-in/security suppression forbidden");
  return p;
}
} // namespace

void analyze_architecture(const Options &options, const fs::path &root,
                          const std::vector<fs::path> &, Report &report) {
  Graph g;
  std::string unavailable;
  try {
    g = codemodel(options, root);
  } catch (const std::exception &e) {
    unavailable = e.what();
    try {
      g = fallback(root);
    } catch (const std::exception &fallback_error) {
      g.evidence = fallback_error.what();
      g.dynamic = true;
    }
  }
  report.gate(
      "CMake target topology", g.verified ? Status::Pass : Status::Unverified,
      g.verified
          ? "Read existing codemodel v2; configuration=" + g.config + "; " +
                g.evidence
          : unavailable +
                "; literal fallback is heuristic; CMake was not executed",
      true);
  report.notes.push_back(
      "Target dependencies in codemodel are build dependencies; link "
      "visibility is unknown. File-set visibility is available only with "
      "codemodel >= 2.5. Snapshot freshness and build behavior remain "
      "unverified.");
  if (g.dynamic)
    report.notes.push_back("Dynamic or unsupported CMake syntax was not "
                           "evaluated; fallback target/edge inventory may be "
                           "incomplete or contain inactive declarations.");
  std::ostringstream j, m;
  j << "{\"analyzer\":" << quote(g.analyzer)
    << ",\"evidence_level\":" << quote(g.level)
    << ",\"configuration\":" << quote(g.config)
    << ",\"evidence\":" << quote(g.evidence) << ",\"targets\":[";
  m << "Target topology: " << g.targets.size() << " targets, " << g.edges.size()
    << " dependency edges (" << g.level << ").\n";
  bool first = true;
  std::map<std::string, std::vector<std::string>> reverse;
  for (auto &e : g.edges)
    reverse[e.to].push_back(e.from);
  for (auto &[id, t] : g.targets) {
    std::set<std::string> seen;
    std::vector<std::string> pending = reverse[id];
    while (!pending.empty()) {
      auto v = pending.back();
      pending.pop_back();
      if (v == id || !seen.insert(v).second)
        continue;
      pending.insert(pending.end(), reverse[v].begin(), reverse[v].end());
    }
    if (!first)
      j << ',';
    first = false;
    j << "{\"id\":" << quote(id) << ",\"name\":" << quote(t.name)
      << ",\"type\":" << quote(t.type)
      << ",\"recompilation_fan_out_heuristic\":" << seen.size()
      << ",\"sources\":[";
    for (std::size_t i = 0; i < t.sources.size(); ++i) {
      if (i)
        j << ',';
      j << quote(t.sources[i]);
    }
    j << "],\"public_headers\":[";
    for (std::size_t i = 0; i < t.public_headers.size(); ++i) {
      if (i)
        j << ',';
      j << quote(t.public_headers[i]);
      if (internal(t.public_headers[i]))
        report.findings.push_back(
            finding("architecture.public-internal-header", t.public_headers[i],
                    "Header with an internal/detail/private path is exposed by "
                    "a PUBLIC/INTERFACE file set",
                    t.path + ": fileSets visibility", g));
    }
    j << "]}";
    m << "- " << t.name << ": " << seen.size()
      << " transitive dependent targets (recompilation estimate only).\n";
  }
  j << "],\"edges\":[";
  first = true;
  for (auto &e : g.edges) {
    if (!first)
      j << ',';
    first = false;
    j << "{\"from\":" << quote(g.targets.at(e.from).name)
      << ",\"to\":" << quote(g.targets.at(e.to).name)
      << ",\"visibility\":" << quote(e.visibility)
      << ",\"evidence\":" << quote(e.evidence) << '}';
    m << "- " << g.targets.at(e.from).name << " -> " << g.targets.at(e.to).name
      << " (" << e.visibility << ").\n";
  }
  j << "]}";
  for (auto &c : cycles(g)) {
    std::vector<std::string> names;
    for (auto &id : c)
      names.push_back(g.targets.at(id).name);
    std::sort(names.begin(), names.end());
    report.findings.push_back(
        finding("architecture.target-cycle", "CMakeLists.txt",
                "Target dependency cycle requires review (static-library "
                "cycles may be intentional)",
                joined(names), g));
  }
  for (const auto &[id, t] : g.targets)
    for (const auto &path : t.public_header_candidates)
      if (internal(path)) {
        Graph candidate;
        candidate.analyzer = g.analyzer;
        candidate.level = "heuristic";
        report.findings.push_back(finding(
            "architecture.public-internal-header", path,
            "Internal header source lies under a PUBLIC/INTERFACE file-set "
            "base directory; exact file-set membership is unavailable",
            t.path +
                ": fileSets baseDirectories, source path; fileSetIndex absent",
            candidate));
      }
  report.notes.push_back(
      "Recompilation fan-out counts transitive dependent targets; no compiler "
      "dependency files, actual header use, incremental rebuild measurements "
      "or time estimates are inferred.");
  report.sections.push_back({"target-topology", j.str(), m.str()});
}

void apply_project_rules(const fs::path &file, const fs::path &root,
                         const std::vector<fs::path> &files, Report &report) {
  Policy p;
  try {
    p = parse_policy(read(file));
  } catch (const std::exception &e) {
    report.gate("project rules", Status::Fail,
                "Policy rejected: " + std::string(e.what()) +
                    "; no policy exemptions applied");
    return;
  }
  std::size_t violations = 0, excepted = 0, evaluated = 0;
  bool unresolved = false;
  std::map<std::pair<std::string, std::string>, std::size_t> emitted;
  Graph provenance;
  provenance.analyzer = "project-rules";
  provenance.level = "heuristic";
  auto emit = [&](const Rule &rule, const std::string &path, std::size_t line,
                  const std::string &evidence) {
    const Exception *exemption = nullptr;
    for (const auto &e : p.exceptions)
      if (e.rule == rule.id && e.path == path) {
        exemption = &e;
        break;
      }
    if (exemption)
      ++excepted;
    else
      ++violations;
    if (++emitted[{rule.id, path}] > 100) {
      ++report.findings_capped;
      return;
    }
    auto f = finding("project." + rule.id, path,
                     "Explicit project constraint matched: " + rule.kind,
                     evidence, provenance);
    f.line = line;
    f.id = make_id(f.rule, path, evidence, static_cast<unsigned>(line));
    f.recommendation = "Resolve the declared constraint or add an exact-path, "
                       "owned, justified, reviewed, unexpired exception in the "
                       "explicit project policy.";
    if (exemption) {
      const auto &e = *exemption;
      f.suppressed = true;
      report.notes.push_back("Project exception " + rule.id + " at " + path +
                             ": owner=" + e.owner + "; reason=" + e.reason +
                             "; review=" + e.review + "; expires=" + e.expires);
    }
    report.findings.push_back(std::move(f));
  };
  try {
    for (const auto &path : files) {
      const auto rel = relative_path(path, root);
      const auto canonical_relative =
          fs::weakly_canonical(path).lexically_relative(
              fs::weakly_canonical(root));
      if (rel.starts_with("../") || canonical_relative.empty() ||
          canonical_relative.is_absolute() ||
          *canonical_relative.begin() == "..")
        throw std::runtime_error("rule source outside root");
      const auto source = source_text(read(path));
      ++evaluated;
      for (auto &r : p.rules) {
        if (r.kind == "api-deny") {
          if (!glob(r.path, rel))
            continue;
          std::string expression = "\\b";
          for (std::size_t i = 0; i < r.pattern.size(); ++i) {
            if (i + 1 < r.pattern.size() && r.pattern.substr(i, 2) == "::") {
              expression += "\\s*::\\s*";
              ++i;
            } else
              expression += r.pattern[i];
          }
          expression += "\\b";
          std::regex re(expression);
          std::size_t previous_offset = 0, line = 1;
          for (auto it = std::sregex_iterator(source.code.begin(),
                                              source.code.end(), re);
               it != std::sregex_iterator(); ++it) {
            auto offset = static_cast<std::size_t>(it->position());
            line += static_cast<std::size_t>(
                std::count(source.code.begin() + previous_offset,
                           source.code.begin() + offset, '\n'));
            previous_offset = offset;
            emit(r, rel, line, "identifier " + r.pattern);
          }
        } else if (glob(r.from, rel)) {
          if (source.unresolved_include)
            unresolved = true;
          for (auto &[line, include] : source.includes) {
            const bool match = glob(r.to, include);
            if (r.kind == "dependency-allow" ? !match : match)
              emit(r, rel, line, "#include " + include);
          }
        }
      }
    }
  } catch (const std::exception &e) {
    report.gate("project rules", Status::Fail,
                "Evaluation failed: " + std::string(e.what()));
    return;
  }
  report.gate(
      "project rules",
      violations   ? Status::Fail
      : unresolved ? Status::Unverified
                   : Status::Pass,
      std::to_string(p.rules.size()) + " explicit lexical constraints; " +
          std::to_string(evaluated) + " selected files; " +
          std::to_string(violations) + " violations; " +
          std::to_string(excepted) + " exact reviewed exceptions" +
          (unresolved ? "; macro/continued include directives unresolved"
                      : ""));
  report.notes.push_back(
      "Project dependency/layer rules match literal include spellings; API "
      "restrictions match identifier tokens outside comments/strings. "
      "Preprocessor conditions, aliases, macro expansion, overloads and "
      "semantic call resolution remain unverified; rules never suppress "
      "built-in security findings.");
}
} // namespace cpprails
