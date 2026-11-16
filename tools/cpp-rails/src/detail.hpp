#pragma once
#include "cpp_rails.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace cpprails::detail {
inline std::string trim(std::string text) {
    auto a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    auto b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}
inline std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + utf8_bytes(path.u8string()));
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
inline void write_file(const fs::path& path, const std::string& data) {
    auto dir = path.parent_path();
    if (!dir.empty()) fs::create_directories(dir);
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + utf8_bytes(path.u8string()));
    out << data;
    if (!out) throw std::runtime_error("I/O failure writing " + utf8_bytes(path.u8string()));
}
inline std::string json_quote(const std::string& s) {
    std::ostringstream result;
    result << '"';
    for (unsigned char c : s) {
        if (c == '\\') result << "\\\\";
        else if (c == '"') result << "\\\"";
        else if (c == '\n') result << "\\n";
        else if (c == '\r') result << "\\r";
        else if (c == '\t') result << "\\t";
        else if (c < 32) result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        else result << static_cast<char>(c);
    }
    result << '"';
    return result.str();
}
inline std::string table_safe(std::string s) {
    for (std::size_t p=0; (p=s.find('|',p))!=std::string::npos; p+=2) s.replace(p,1,"\\|");
    std::replace(s.begin(),s.end(),'\n',' ');
    std::replace(s.begin(),s.end(),'\r',' ');
    return s;
}

}
