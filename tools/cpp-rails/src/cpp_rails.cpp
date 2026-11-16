#include "cpp_rails.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <tuple>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <poll.h>
#endif

namespace cpprails {
std::string utf8_bytes(std::u8string_view text) {
    return {text.begin(),text.end()};
}
fs::path path_from_utf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(),text.end()));
}

namespace {
std::string trim(std::string text) {
    auto a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    auto b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}
std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size()-2);
    return s;
}
std::string lower(std::string v) {
    std::transform(v.begin(),v.end(),v.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return v;
}
std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + utf8_bytes(path.u8string()));
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write_file(const fs::path& path, const std::string& data) {
    auto dir = path.parent_path();
    if (!dir.empty()) fs::create_directories(dir);
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + utf8_bytes(path.u8string()));
    out << data;
    if (!out) throw std::runtime_error("I/O failure writing " + utf8_bytes(path.u8string()));
}
fs::path resolve(const fs::path& root, const fs::path& path) {
    return path.is_absolute() ? path : root / path;
}
std::string json_quote(const std::string& s) {
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
std::string table_safe(std::string s) {
    for (std::size_t p=0; (p=s.find('|',p))!=std::string::npos; p+=2) s.replace(p,1,"\\|");
    std::replace(s.begin(),s.end(),'\n',' ');
    std::replace(s.begin(),s.end(),'\r',' ');
    return s;
}
std::string evidence_excerpt(std::string s) {
    s=trim(std::move(s));
    if (s.size()>200) s.resize(200);
    return s;
}
std::string today_utc() {
    auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[16]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}
bool valid_date(const std::string& text) {
    if (text.size()!=10 || text[4]!='-' || text[7]!='-') return false;
    for (std::size_t i=0;i<text.size();++i)
        if (i!=4 && i!=7 && (text[i]<'0'||text[i]>'9')) return false;
    const int y=std::stoi(text.substr(0,4)), m=std::stoi(text.substr(5,2)), d=std::stoi(text.substr(8,2));
    if (y<1970||m<1||m>12||d<1) return false;
    const bool leap = (y%4==0 && (y%100!=0||y%400==0));
    const int days[]={31,leap?29:28,31,30,31,30,31,31,30,31,30,31};
    return d<=days[m-1];
}
bool glob(const std::string& pattern,const std::string& text) {
    std::size_t i=0,j=0,mark=std::string::npos,last=0;
    while (j<text.size()) {
        if (i<pattern.size() && (pattern[i]==text[j] || pattern[i]=='?')) {++i;++j;}
        else if (i<pattern.size() && pattern[i]=='*') {mark=i++;last=j;}
        else if (mark!=std::string::npos) {i=mark+1;j=++last;}
        else return false;
    }
    while (i<pattern.size() && pattern[i]=='*') ++i;
    return i==pattern.size();
}
std::string gate_status_json(Status value) {return json_quote(status_text(value));}
std::string first_line(const std::string& s) {
    auto pos=s.find('\n'); return trim(s.substr(0,pos));
}
std::string process_info(const Process& p) {
    if (!p.started) return "tool could not be started";
    return "exit=" + std::to_string(p.exit_code) + " " + evidence_excerpt(first_line(p.output));
}
void record_command(Report& r, const std::string& name, const Process& p, bool mandatory, bool unsupported_unverified = false) {
    if (!p.started) {r.gate(name,Status::Unverified,process_info(p),mandatory);return;}
    if (unsupported_unverified) {r.gate(name, Status::Unverified, process_info(p), mandatory);return;}
    r.gate(name,p.exit_code==0 ? Status::Pass : Status::Fail,process_info(p),mandatory);
}
} // namespace

std::string status_text(Status status) {
    switch(status){ case Status::Pass:return "PASS";case Status::Fail:return "FAIL";
        case Status::Unverified:return "UNVERIFIED";case Status::NotApplicable:return "N/A";}
    return "UNVERIFIED";
}
void Report::gate(std::string name,Status status,std::string details,bool required) {
    gates.push_back({std::move(name),status,std::move(details),required});
}
bool Report::has_failure() const {return std::any_of(gates.begin(),gates.end(),[](auto& g){return g.status==Status::Fail;});}
bool Report::required_unverified() const {return std::any_of(gates.begin(),gates.end(),[](auto& g){return g.required && g.status==Status::Unverified;});}
bool Report::any_unverified() const {return std::any_of(gates.begin(),gates.end(),[](auto& g){return g.status==Status::Unverified;});}
std::size_t Report::new_unsuppressed_p2() const {
    return std::count_if(findings.begin(),findings.end(),[](auto& f){return !f.existing && !f.suppressed && f.severity=="P2";});
}

#ifdef _WIN32
// Quote one argument so the MSVC CRT (CommandLineToArgvW rules) parses it back verbatim.
std::string quote_windows_arg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') quoted.append(backslashes * 2 + 1, '\\');
        else quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(c);
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}
#endif

Process exec(const std::vector<std::string>& args, const fs::path& cwd, bool separate_error) {
    Process result;
    if (args.empty()) return result;
    constexpr std::size_t output_cap=128*1024;
#ifdef _WIN32
    // CreateProcess with an explicitly quoted argv (no cmd.exe interpreter) and a
    // pipe for stdout+stderr, so evidence and Git output are captured like on POSIX.
    std::string command_line;
    for (auto& a:args) {
        if (!command_line.empty()) command_line.push_back(' ');
        command_line += quote_windows_arg(a);
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength=sizeof(sa);
    sa.bInheritHandle=TRUE;
    HANDLE read_end=nullptr, write_end=nullptr;
    if (!CreatePipe(&read_end,&write_end,&sa,0)) return result;
    SetHandleInformation(read_end,HANDLE_FLAG_INHERIT,0);
    HANDLE error_read=nullptr,error_write=nullptr;
    if(separate_error) {
        if(!CreatePipe(&error_read,&error_write,&sa,0)){CloseHandle(read_end);CloseHandle(write_end);return result;}
        SetHandleInformation(error_read,HANDLE_FLAG_INHERIT,0);
    }
    HANDLE null_input=CreateFileA("NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    STARTUPINFOW si{};
    si.cb=sizeof(si);
    si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdInput=null_input;
    si.hStdOutput=write_end;
    si.hStdError=separate_error?error_write:write_end;
    PROCESS_INFORMATION pi{};
    auto mutable_cmd=path_from_utf8(command_line).wstring();
    const auto dir=cwd.wstring();
    const BOOL created=CreateProcessW(nullptr,mutable_cmd.data(),nullptr,nullptr,TRUE,0,nullptr,
                                      dir.empty()?nullptr:dir.c_str(),&si,&pi);
    CloseHandle(write_end);
    if(error_write)CloseHandle(error_write);
    if (null_input!=INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (!created) {CloseHandle(read_end);if(error_read)CloseHandle(error_read);return result;}
    result.started=true;
    std::array<char,8192> buffer{};
    if(separate_error) {
        // Drain both streams while the child runs. Each stored stream is bounded;
        // continue draining after the cap so a verbose child cannot deadlock.
        std::array<HANDLE,2> pipes{read_end,error_read};
        std::array<std::string*,2> outputs{&result.output,&result.error_output};
        while(pipes[0]||pipes[1]) {
            bool progressed=false;
            for(std::size_t i=0;i<2;++i)if(pipes[i]) {
                DWORD available=0,count=0;
                if(!PeekNamedPipe(pipes[i],nullptr,0,nullptr,&available,nullptr)) {
                    CloseHandle(pipes[i]);pipes[i]=nullptr;continue;
                }
                if(available&&ReadFile(pipes[i],buffer.data(),static_cast<DWORD>(std::min<std::size_t>(available,buffer.size())),&count,nullptr)) {
                    progressed=true;auto& output=*outputs[i];
                    if(output.size()<output_cap)output.append(buffer.data(),std::min<std::size_t>(count,output_cap-output.size()));
                }
            }
            if(!progressed&&(pipes[0]||pipes[1]))Sleep(1);
        }
    } else for (;;) {
        DWORD count=0;
        if (!ReadFile(read_end,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr) || count==0) break;
        if (result.output.size()<output_cap)
            result.output.append(buffer.data(),std::min<std::size_t>(count,output_cap-result.output.size()));
    }
    if(!separate_error)CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess,INFINITE);
    DWORD code=0;
    if (GetExitCodeProcess(pi.hProcess,&code)) result.exit_code=static_cast<int>(code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    int fds[2];
    if (pipe(fds)!=0) return result;
    int errors[2]={-1,-1};
    if(separate_error&&pipe(errors)!=0){close(fds[0]);close(fds[1]);return result;}
    pid_t pid=fork();
    if (pid<0) {close(fds[0]);close(fds[1]);if(separate_error){close(errors[0]);close(errors[1]);}return result;}
    if (pid==0) {
        close(fds[0]);
        dup2(fds[1],STDOUT_FILENO);
        dup2(separate_error?errors[1]:fds[1],STDERR_FILENO);
        if(separate_error){close(errors[0]);close(errors[1]);}
        close(fds[1]);
        if (chdir(cwd.c_str())!=0) _exit(126);
        std::vector<char*> argv;
        argv.reserve(args.size()+1);
        for (auto& a:args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0],argv.data());
        _exit(127);
    }
    close(fds[1]);result.started=true;
    if(separate_error)close(errors[1]);
    std::array<char,8192> buffer{};
    if(separate_error) {
        std::array<pollfd,2> pipes{{{fds[0],POLLIN,0},{errors[0],POLLIN,0}}};
        std::array<std::string*,2> outputs{&result.output,&result.error_output};
        while(pipes[0].fd>=0||pipes[1].fd>=0) {
            if(poll(pipes.data(),pipes.size(),-1)<0){if(errno==EINTR)continue;break;}
            for(std::size_t i=0;i<2;++i)if(pipes[i].fd>=0&&pipes[i].revents) {
                const auto count=read(pipes[i].fd,buffer.data(),buffer.size());
                if(count<0&&errno==EINTR)continue;
                if(count<=0){close(pipes[i].fd);pipes[i].fd=-1;continue;}
                auto& output=*outputs[i];
                if(output.size()<output_cap)output.append(buffer.data(),std::min(static_cast<std::size_t>(count),output_cap-output.size()));
            }
        }
        for(const auto& pipe:pipes)if(pipe.fd>=0)close(pipe.fd);
    } else for (;;) {
        ssize_t count=read(fds[0],buffer.data(),buffer.size());
        if (count==0) break;
        if (count<0) {if (errno==EINTR) continue;break;}
        if (result.output.size()<output_cap) {
            result.output.append(buffer.data(),std::min(static_cast<std::size_t>(count), output_cap-result.output.size()));
        }
    }
    if(!separate_error)close(fds[0]);int status=0;
    if (waitpid(pid,&status,0)==pid) {
        result.exit_code=WIFEXITED(status)?WEXITSTATUS(status): (WIFSIGNALED(status)?128+WTERMSIG(status):-1);
    }
    if (result.exit_code==127 && result.output.empty()) result.started=false;
#endif
    return result;
}

std::string usage(bool check_program) {
    std::ostringstream o;
    o << (check_program ? "cpp-rails-check" : "cpp-rails-audit") << " — C++23 native engineering guardrails\n";
    o << (check_program ? "Usage: cpp-rails-check [options]\n" :
          "Usage: cpp-rails-audit [audit|triage|topology|baseline|templates|perf compare|perf analyze] [options]\n");
    o << "  --root PATH           C++ project root (default '.')\n"
      << "  --profile NAME        general|low-latency|library-sdk|legacy-code|engine|service|native-ui|ffi|prototype\n"
      << "  --profile-file PATH   Explicit, reviewed TOML profile (never auto-load from target)\n"
      << "  --format FORMAT       markdown|json|sarif (SARIF 2.1.0 for code-scanning UIs)\n"
      << "  --exclude-tests       Skip test/fixture/mock sources (otherwise reported as scope=test, one level lower)\n"
      << "  --include-dir PATH    Extra include root for topology (repeatable; compile_commands.json -I flags are read too)\n"
      << "  --output PATH         Report file; baseline inventory for 'baseline'\n"
      << "  --changed             Scan Git diff versus HEAD + untracked files\n"
      << "  --baseline PATH       Compare reviewed heuristic baseline\n"
      << "  --exception-file PATH Reviewed exceptions (P2/P3 only, expires)\n"
      << "  --strict              Missing optional verification also returns 2\n"
      << "  --source PATH         Standalone translation unit for templates/perf remarks\n"
      << "  --compiler EXE        Compiler executable/path (default 'c++')\n"
      << "  --std VALUE           C++ standard flag value (default c++20)\n"
      << "  --results PATH        CSV/TSV samples: benchmark,variant,run_ns\n"
      << "  --max-regression N    Maximum median regression (%)\n"
      << "  --min-samples N       Minimum repeated samples per variant\n"
      << "  --check               Compile templates --source (does not prove generic correctness)\n"
      << "  --expect-error TEXT    Expected compiler error fragment in negative fixture\n"
      << "  --build-dir PATH      Existing CMake build dir (default build)\n"
      << "  --config NAME         CMake configuration for build/ctest (default: CMAKE_BUILD_TYPE, else Debug)\n"
      << "  --configure           Configure project if build directory not configured\n"
      << "  --no-build, --no-tests Explicitly skip required project gates (report UNVERIFIED)\n"
      << "  --lint                Run clang-tidy (requires compilation database)\n"
      << "  --sanitizer MODE      asan-ubsan|tsan (separate isolated builds)\n"
      << "  --compiler-matrix L   Comma-separated supported compiler paths\n"
      << "  --abi-old PATH --abi-new PATH  Compare public binaries using abidiff\n"
      << "  --lifetime            Experimental lifetime warnings; needs --source\n"
      << "  --help                Show this help\n";
    return o.str();
}
Options parse_options(bool check_program,const std::vector<std::string>& args) {
    Options opt;opt.check_program=check_program;opt.root=fs::path(".");
    std::size_t i=0;
    if (check_program) opt.action="check";
    else if (i<args.size() && !args[i].empty() && args[i][0]!='-') {
        opt.action=args[i++];
        if (opt.action=="perf" && i<args.size() && !args[i].empty() && args[i][0]!='-') opt.subaction=args[i++];
    }
    const std::set<std::string> actions={"audit","triage","topology","baseline","templates","perf"};
    if (!check_program && !actions.count(opt.action)) throw std::runtime_error("unsupported action: "+opt.action);
    if (opt.action=="perf" && opt.subaction!="compare" && opt.subaction!="analyze")
        throw std::runtime_error("perf requires 'compare' or 'analyze'");
    auto next=[&](const std::string& name) {
        if (++i>=args.size() || args[i].empty()) throw std::runtime_error("missing value for "+name);
        return args[i];
    };
    for (;i<args.size();++i) {
        const auto a=args[i];
        if (a=="--help"||a=="-h") opt.help=true;
        else if(a=="--root") opt.root=path_from_utf8(next(a));
        else if(a=="--profile") opt.profile=next(a);
        else if(a=="--profile-file") opt.profile_file=path_from_utf8(next(a));
        else if(a=="--format") opt.format=next(a);
        else if(a=="--output") opt.output=path_from_utf8(next(a));
        else if(a=="--changed") opt.changed=true;
        else if(a=="--baseline") opt.baseline=path_from_utf8(next(a));
        else if(a=="--exception-file") opt.exception_file=path_from_utf8(next(a));
        else if(a=="--strict") opt.strict=true;
        else if(a=="--source") opt.source=path_from_utf8(next(a));
        else if(a=="--compiler") opt.compiler=next(a);
        else if(a=="--std") opt.cpp_standard=next(a);
        else if(a=="--results") opt.results=path_from_utf8(next(a));
        else if(a=="--max-regression") opt.max_regression=std::stod(next(a));
        else if(a=="--min-samples") opt.min_samples=static_cast<unsigned>(std::stoul(next(a)));
        else if(a=="--check") opt.compile_checks=true;
        else if(a=="--expect-error") opt.expect_error=next(a);
        else if(a=="--build-dir") opt.build_dir=path_from_utf8(next(a));
        else if(a=="--config") opt.config=next(a);
        else if(a=="--configure") opt.configure=true;
        else if(a=="--no-build") opt.no_build=true;
        else if(a=="--no-tests") opt.no_tests=true;
        else if(a=="--lint") opt.lint=true;
        else if(a=="--sanitizer") opt.sanitizer=next(a);
        else if(a=="--compiler-matrix") opt.compiler_matrix=next(a);
        else if(a=="--abi-old") opt.abi_old=path_from_utf8(next(a));
        else if(a=="--abi-new") opt.abi_new=path_from_utf8(next(a));
        else if(a=="--lifetime") opt.lifetime=true;
        else if(a=="--exclude-tests") opt.exclude_tests=true;
        else if(a=="--include-dir") opt.include_dirs.push_back(path_from_utf8(next(a)));
        else throw std::runtime_error("unknown argument "+a);
    }
    if (!opt.help) {
        if (opt.format!="markdown"&&opt.format!="json"&&opt.format!="sarif") throw std::runtime_error("format must be markdown, json or sarif");
        if (opt.profile!="general" && opt.profile!="low-latency" && opt.profile!="library-sdk" && opt.profile!="legacy-code"
            && opt.profile!="engine" && opt.profile!="service" && opt.profile!="native-ui" && opt.profile!="ffi" && opt.profile!="prototype")
            throw std::runtime_error("unsupported profile");
        if (opt.cpp_standard!="c++11"&&opt.cpp_standard!="c++14"&&opt.cpp_standard!="c++17"&&opt.cpp_standard!="c++20"&&opt.cpp_standard!="c++23"&&opt.cpp_standard!="c++26")
            throw std::runtime_error("invalid --std C++ standard");
        if (opt.sanitizer!="" && opt.sanitizer!="asan-ubsan" && opt.sanitizer!="tsan") throw std::runtime_error("unsupported sanitizer");
        if (opt.min_samples && *opt.min_samples<2) throw std::runtime_error("minimum benchmark samples must be >=2");
        if (opt.max_regression && (!std::isfinite(*opt.max_regression)||*opt.max_regression<0))
            throw std::runtime_error("max regression must be finite and >=0");
        if (opt.action=="baseline" && opt.changed) throw std::runtime_error("baseline cannot use --changed");
        if (opt.config && (opt.config->empty() || opt.config->find_first_not_of(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos))
            throw std::runtime_error("--config must be an alphanumeric CMake configuration name");
    }
    return opt;
}

namespace {
struct RuleTally {std::size_t production_p2=0,production_p3=0,test=0;};
std::map<std::string,RuleTally> tally_by_rule(const Report& r) {
    std::map<std::string,RuleTally> out;
    for (auto& f:r.findings) {
        if (f.suppressed) continue;
        auto& t=out[f.rule];
        if (f.scope=="test") ++t.test;
        else if (f.severity=="P2") ++t.production_p2;
        else ++t.production_p3;
    }
    return out;
}
// Production files with the most unsuppressed hints: where a human reviewer should start.
std::vector<std::pair<std::string,std::size_t>> hotspots(const Report& r,std::size_t limit=10) {
    std::map<std::string,std::size_t> counts;
    for (auto& f:r.findings) if (!f.suppressed && f.scope!="test" && f.severity=="P2") ++counts[f.path];
    std::vector<std::pair<std::string,std::size_t>> ranked(counts.begin(),counts.end());
    std::stable_sort(ranked.begin(),ranked.end(),[](auto& a,auto& b){return a.second>b.second;});
    if (ranked.size()>limit) ranked.resize(limit);
    return ranked;
}
std::string uri_escape(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c:s) {
        if (std::isalnum(c)||c=='/'||c=='-'||c=='_'||c=='.'||c=='~'||c==':') o<<c;
        else o<<'%'<<std::uppercase<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<int>(c)<<std::nouppercase<<std::dec;
    }
    return o.str();
}
} // namespace

std::string markdown(const Report& r) {
    std::ostringstream out;
    const auto production=std::count_if(r.findings.begin(),r.findings.end(),[](auto& f){return f.scope!="test";});
    out << "# cpp-engineering-guardrails — " << r.action << "\n\n"
        << "- Root: `" << r.root << "`\n- Profile: `" << r.profile << "`\n"
        << "- Files scanned: " << r.files_scanned << "; skipped: " << r.files_skipped << "\n"
        << "- Heuristic findings: " << r.findings.size() << " (" << production << " production, "
        << (r.findings.size()-static_cast<std::size_t>(production)) << " test; "
        << r.new_unsuppressed_p2() << " new, unsuppressed P2)"
        << (r.findings_capped?"; "+std::to_string(r.findings_capped)+" more not listed (per-file limit)":"") << "\n\n";
    const auto tallies=tally_by_rule(r);
    if (!tallies.empty()) {
        out << "## Summary by rule\n\n| Rule | Production P2 | Production P3 | Test (P3) |\n|---|---|---|---|\n";
        std::vector<std::pair<std::string,RuleTally>> rows(tallies.begin(),tallies.end());
        std::stable_sort(rows.begin(),rows.end(),[](auto& a,auto& b){return a.second.production_p2>b.second.production_p2;});
        for (auto& [rule,t]:rows) out << "| " << rule << " | " << t.production_p2 << " | " << t.production_p3 << " | " << t.test << " |\n";
        const auto hot=hotspots(r);
        if (!hot.empty()) {
            out << "\n**Review first (production files with most P2 hints):** ";
            for (std::size_t i=0;i<hot.size();++i) out << (i?", ":"") << '`' << table_safe(hot[i].first) << "` (" << hot[i].second << ")";
            out << "\n";
        }
        out << "\n";
    }
    out << "## Verification gates\n\n| Gate | Status | Required | Evidence |\n|---|---|---|---|\n";
    if (r.gates.empty()) out << "| — | UNVERIFIED | no | No external verification requested |\n";
    for (auto& g:r.gates) out << "| " << table_safe(g.name) << " | **" << status_text(g.status) << "** | "
        << (g.required?"yes":"no") << " | " << table_safe(g.details.substr(0,450)) << " |\n";
    out << "\n## Findings (review candidates, NOT proven defects)\n\n| Priority | Rule / ID | Location | Scope | Confidence | State |\n|---|---|---|---|---|---|\n";
    std::size_t shown=0;
    for (auto& f:r.findings) {
        if (++shown>200) break;
        out << "| " << f.severity << " | " << table_safe(f.rule) << "/`" << f.id << "` | `"
            << table_safe(f.path) << "`:" << f.line << " | " << f.scope << " | " << f.confidence << " | "
            << (f.suppressed?"suppressed":(f.existing?"baseline":"new")) << " |\n";
    }
    if (r.findings.size()>200) out << "\nFirst 200 of " << r.findings.size() << " findings shown; use JSON for all.\n";
    out << "\n### Review instructions (first 25)\n";
    for (std::size_t j=0;j<std::min<std::size_t>(r.findings.size(),25);++j) {
        auto& f=r.findings[j];
        out << "- **"<<f.id<<"** "<<table_safe(f.message)<<" (`"<<table_safe(f.path)<<"`:"<<f.line<<"). Evidence `"
            <<table_safe(f.evidence)<<"`. "<<table_safe(f.recommendation)<<"\n";
    }
    out << "\n## Notes and limitations\n";
    for (auto& note:r.notes) out << "- " << note << "\n";
    out << "\nScanner hints are not semantic proof. Missing tool checks are UNVERIFIED, not green. This report does not certify correctness, memory safety or ABI stability.\n";
    return out.str();
}
std::string json(const Report& r) {
    std::ostringstream out;
    out<<"{\"action\":"<<json_quote(r.action)<<",\"root\":"<<json_quote(r.root)
       <<",\"profile\":"<<json_quote(r.profile)<<",\"files_scanned\":"<<r.files_scanned
       <<",\"files_skipped\":"<<r.files_skipped<<",\"findings_capped\":"<<r.findings_capped<<",\"summary\":{\"by_rule\":{";
    {
        bool first=true;
        for (auto& [rule,t]:tally_by_rule(r)) {
            if(!first)out<<',';first=false;
            out<<json_quote(rule)<<":{\"production_p2\":"<<t.production_p2<<",\"production_p3\":"<<t.production_p3<<",\"test\":"<<t.test<<'}';
        }
        out<<"},\"hotspots\":[";
        const auto hot=hotspots(r);
        for (std::size_t i=0;i<hot.size();++i) {if(i)out<<',';out<<"{\"path\":"<<json_quote(hot[i].first)<<",\"p2\":"<<hot[i].second<<'}';}
        out<<"]},\"gates\":[";
    }
    for (std::size_t i=0;i<r.gates.size();++i) {
        if (i) out<<',';
        const auto& g=r.gates[i];
        out<<"{\"name\":"<<json_quote(g.name)<<",\"status\":"<<gate_status_json(g.status)
           <<",\"details\":"<<json_quote(g.details)<<",\"required\":"<<(g.required?"true":"false")<<'}';
    }
    out<<"],\"findings\":[";
    for (std::size_t i=0;i<r.findings.size();++i) {
        if (i) out<<',';
        const auto& f=r.findings[i];
        out<<"{\"id\":"<<json_quote(f.id)<<",\"rule\":"<<json_quote(f.rule)
           <<",\"severity\":"<<json_quote(f.severity)<<",\"confidence\":"<<json_quote(f.confidence)
           <<",\"path\":"<<json_quote(f.path)<<",\"line\":"<<f.line
           <<",\"message\":"<<json_quote(f.message)<<",\"evidence\":"<<json_quote(f.evidence)
           <<",\"recommendation\":"<<json_quote(f.recommendation)
           <<",\"scope\":"<<json_quote(f.scope)
           <<",\"existing\":"<<(f.existing?"true":"false")
           <<",\"suppressed\":"<<(f.suppressed?"true":"false")<<'}';
    }
    out<<"],\"notes\":[";
    for (std::size_t i=0;i<r.notes.size();++i){if(i)out<<',';out<<json_quote(r.notes[i]);}
    out<<"]}\n";return out.str();
}

// SARIF 2.1.0 for GitHub code scanning / VS Code SARIF viewer. Heuristic hints map to
// "warning" (P2) or "note" (P3); gates are attached as run properties, never as results.
std::string sarif(const Report& r) {
    std::map<std::string,const Finding*> rule_examples;
    for (auto& f:r.findings) rule_examples.emplace(f.rule,&f);
    std::ostringstream out;
    out<<"{\"$schema\":\"https://json.schemastore.org/sarif-2.1.0.json\",\"version\":\"2.1.0\",\"runs\":[{"
       <<"\"tool\":{\"driver\":{\"name\":\"cpp-rails\",\"version\":\"1.3.0\",\"rules\":[";
    bool first=true;
    for (auto& [rule,f]:rule_examples) {
        if(!first)out<<',';first=false;
        out<<"{\"id\":"<<json_quote(rule)<<",\"shortDescription\":{\"text\":"<<json_quote(f->message)
           <<"},\"help\":{\"text\":"<<json_quote(f->recommendation)<<"}}";
    }
    std::string root_uri=utf8_bytes(path_from_utf8(r.root).generic_u8string());
    if (root_uri.empty() || root_uri.back()!='/') root_uri.push_back('/');
    if (root_uri.front()!='/') root_uri="/"+root_uri;
    out<<"]}},\"originalUriBaseIds\":{\"SRCROOT\":{\"uri\":"<<json_quote("file://"+uri_escape(root_uri))<<"}},\"results\":[";
    first=true;
    for (auto& f:r.findings) {
        if(!first)out<<',';first=false;
        out<<"{\"ruleId\":"<<json_quote(f.rule)<<",\"level\":"<<json_quote(f.severity=="P2"?"warning":"note")
           <<",\"message\":{\"text\":"<<json_quote(f.message+". "+f.recommendation)<<"}"
           <<",\"locations\":[{\"physicalLocation\":{\"artifactLocation\":{\"uri\":"<<json_quote(uri_escape(f.path))
           <<",\"uriBaseId\":\"SRCROOT\"},\"region\":{\"startLine\":"<<std::max<std::size_t>(f.line,1)<<"}}}]"
           <<",\"partialFingerprints\":{\"cppRailsId/v1\":"<<json_quote(f.id)<<"}"
           <<",\"properties\":{\"priority\":"<<json_quote(f.severity)<<",\"scope\":"<<json_quote(f.scope)
           <<",\"confidence\":"<<json_quote(f.confidence)<<",\"baseline\":"<<(f.existing?"true":"false")<<'}';
        if (f.suppressed) out<<",\"suppressions\":[{\"kind\":\"external\",\"justification\":\"reviewed exception\"}]";
        out<<'}';
    }
    out<<"],\"properties\":{\"profile\":"<<json_quote(r.profile)<<",\"gates\":[";
    for (std::size_t i=0;i<r.gates.size();++i) {
        if(i)out<<',';
        out<<"{\"name\":"<<json_quote(r.gates[i].name)<<",\"status\":"<<json_quote(status_text(r.gates[i].status))
           <<",\"required\":"<<(r.gates[i].required?"true":"false")<<'}';
    }
    out<<"]}}]}\n";
    return out.str();
}

std::string relative_path(const fs::path& file,const fs::path& root) {
    auto rel=file.lexically_relative(root);
    return utf8_bytes((rel.empty()?file:rel).generic_u8string());
}
std::string make_id(const std::string& rule,const std::string& path,const std::string& evidence,unsigned occurrence) {
    const auto source=rule+"|"+path+"|"+trim(evidence)+"|"+std::to_string(occurrence);
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:source){hash^=c;hash*=1099511628211ULL;}
    std::ostringstream out;out<<rule<<'-'<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;
    return out.str();
}
std::set<std::string> changed_paths(const fs::path& root,std::vector<std::string>* diagnostics) {
    auto git=[&](const std::vector<std::string>& args,const fs::path& cwd) {
        auto result=exec(args,cwd,true);
        if(!result.error_output.empty()&&diagnostics)diagnostics->push_back("Git diagnostic: "+result.error_output);
        return result;
    };
    const auto top=git({"git","rev-parse","--show-toplevel"},root);
    if(!top.started||top.exit_code!=0) throw std::runtime_error("--changed requires a Git working tree");
    auto top_text=top.output;
    while(!top_text.empty()&&(top_text.back()=='\n'||top_text.back()=='\r'))top_text.pop_back();
    const auto repo=fs::canonical(path_from_utf8(top_text));
    const auto scope=fs::canonical(root);
    auto tracked=git({"git","diff","--name-only","--no-renames","-z","HEAD","--"},repo);
    auto added=git({"git","ls-files","-z","--others","--exclude-standard"},repo);
    if (!tracked.started||tracked.exit_code!=0||!added.started||added.exit_code!=0)
        throw std::runtime_error("--changed requires Git and working HEAD; unable to get complete change set");
    std::set<std::string> found;
    for (auto& text:{tracked.output,added.output}) {
        if(!text.empty()&&text.back()!='\0')throw std::runtime_error("incomplete NUL-delimited Git paths");
        std::istringstream in(text);std::string name;
        while(std::getline(in,name,'\0')) {
            const auto relative=(repo/path_from_utf8(name)).lexically_normal().lexically_relative(scope);
            if(relative.empty()||*relative.begin()=="..")continue; // explicitly outside --root
            found.insert(utf8_bytes(relative.generic_u8string())); // retain deletions; scanner only reads existing files
        }
    }
    if (tracked.output.size()>=128*1024||added.output.size()>=128*1024)
        throw std::runtime_error("--changed output too large; refusing incomplete Git result");
    return found;
}
namespace {
const std::set<std::string> excluded={".git",".svn","target","build","_build",".cpp-rails","node_modules",
    "vendor","third_party","external","_deps","generated",".cache","out","dist","cmake-build-debug","cmake-build-release"};
bool is_source(const fs::path& p) {
    const auto ext=lower(utf8_bytes(p.extension().u8string()));
    return std::set<std::string>{".cpp",".cc",".cxx",".c",".h",".hpp",".hh",".hxx",".ipp",".tpp",".ixx"}.count(ext)!=0;
}
bool is_header(const fs::path& p) {
    const auto ext=lower(utf8_bytes(p.extension().u8string()));
    return ext==".h"||ext==".hpp"||ext==".hh"||ext==".hxx";
}
std::string mask_code(const std::string& src) {
    enum class State { Code, SingleComment, MultiComment, String, Char, Raw };
    State state=State::Code;
    std::string out(src.size(),' ');
    std::string terminator;
    for (std::size_t i=0;i<src.size();++i) {
        auto c=src[i], next=(i+1<src.size()?src[i+1]:'\0');
        if (c=='\n') {out[i]='\n';if (state==State::SingleComment)state=State::Code;continue;}
        if (state==State::Code) {
            // Consume a preprocessing number as one token. Apostrophes within it
            // are digit separators, not character delimiters (hex/binary/exponents too).
            if((i==0||(!std::isalnum(static_cast<unsigned char>(src[i-1]))&&src[i-1]!='_')) &&
               (std::isdigit(static_cast<unsigned char>(c)) || (c=='.'&&std::isdigit(static_cast<unsigned char>(next))))) {
                out[i]=c;
                while(i+1<src.size()) {
                    const auto n=src[i+1];const auto prev=src[i];
                    if(std::isalnum(static_cast<unsigned char>(n))||n=='_'||n=='.'||
                       (n=='\''&&i+2<src.size()&&(std::isalnum(static_cast<unsigned char>(src[i+2]))||src[i+2]=='_'))||
                       ((n=='+'||n=='-')&&(prev=='e'||prev=='E'||prev=='p'||prev=='P'))) {++i;out[i]=src[i];}
                    else break;
                }
                continue;
            }
            if(c=='/'&&next=='/') {state=State::SingleComment;++i;continue;}
            if(c=='/'&&next=='*') {state=State::MultiComment;++i;continue;}
            if(c=='"') {state=State::String;continue;}
            if(c=='\'') {state=State::Char;continue;}
            // Raw string literal R"delim( ... )delim" : suppress content before rule matching.
            if(c=='R'&&next=='"') {
                auto paren=src.find('(',i+2);
                if(paren!=std::string::npos && paren-i<=18) {
                    const auto delimiter=src.substr(i+2,paren-i-2);
                    if(delimiter.find_first_of(" \\\t\r\n)")==std::string::npos){
                        terminator=")"+delimiter+"\"";state=State::Raw;
                        i=paren;continue;
                    }
                }
            }
            out[i]=c;
        } else if (state==State::Raw) {
            if(c==')' && src.compare(i,terminator.size(),terminator)==0){i+=terminator.size()-1;state=State::Code;}
        } else if(state==State::MultiComment) {
            if(c=='*'&&next=='/') {++i;state=State::Code;}
        } else if(state==State::String || state==State::Char) {
            if(c=='\\'&&next!='\0'){if(next=='\n')out[i+1]='\n';++i;continue;}
            if((state==State::String&&c=='"')||(state==State::Char&&c=='\''))state=State::Code;
        }
    }
    return out;
}
struct Pattern {
    const char *id, *priority, *message, *remediation;
};
struct LineContext {
    bool header=false, in_requires=false, native_ui=false;
    bool thread_temporary_recent=false;              // a std::thread temporary started a few lines above
    const std::set<std::string>* thread_names=nullptr; // identifiers declared as std::thread/jthread in this file
};
// Test sources are reported separately and one level lower: real-code audits showed
// 38-61% of hints in unit tests/fixtures, which drowned production candidates.
bool path_is_test(const std::string& rel) {
    static const std::set<std::string> dirs={"test","tests","testing","unittest","unittests","mock","mocks",
        "fixture","fixtures","testdata","test_data","benchmark_test"};
    std::istringstream parts(rel);
    std::string part;
    std::vector<std::string> components;
    while (std::getline(parts,part,'/')) components.push_back(part);
    if (components.empty()) return false;
    for (std::size_t i=0;i+1<components.size();++i) if (dirs.count(lower(components[i]))) return true;
    const auto stem=utf8_bytes(path_from_utf8(components.back()).stem().u8string());
    const auto low=lower(stem);
    auto ends=[&](const std::string& s){return low.size()>s.size() && low.compare(low.size()-s.size(),s.size(),s)==0;};
    if (ends("_test")||ends("_tests")||ends("_unittest")||ends("_unittests")||ends("_mock")||ends("_mocks")
        ||ends("_fake")||ends("_fixture")||ends(".test")) return true;
    if (low.rfind("test_",0)==0 || low.rfind("mock_",0)==0) return true;
    // CamelCase suites (FooTest.cpp, FooTests.cpp); lower-case "latest.cpp" is not a test.
    auto camel=[&](const std::string& s){return stem.size()>s.size() && stem.compare(stem.size()-s.size(),s.size(),s)==0;};
    return camel("Test") || camel("Tests");
}
bool detaches_thread(const std::string& line,const LineContext& ctx) {
    static const std::regex call(R"((\)|\w+)\s*(\.|->)\s*detach\s*\()");
    for (std::sregex_iterator it(line.begin(),line.end(),call),end;it!=end;++it) {
        const auto target=(*it)[1].str();
        if (target==")") {
            if (ctx.thread_temporary_recent || line.find("thread")!=std::string::npos) return true;
            continue;
        }
        if (lower(target).find("thread")!=std::string::npos) return true;
        if (ctx.thread_names && ctx.thread_names->count(target)) return true;
    }
    return false;
}
std::vector<Pattern> rules(const std::string& line,const LineContext& ctx) {
    std::vector<Pattern> found;
    if (trim(line).empty() || trim(line).front()=='#') return found;
    auto has=[&](const std::string& s){return line.find(s)!=std::string::npos;};
    static const std::regex manual_alloc(R"(\b(new\s+|delete\s*(\[\s*\])?\s+))");
    // `new T(..., this|parent)` is the idiomatic Qt object-tree ownership transfer.
    static const std::regex parent_owned_new(R"(\bnew\s+[A-Za-z_][\w:<>]*\s*\([^;]*\b(this|parent)\s*\))");
    // `new` handed straight to an owning wrapper, e.g. shared_ptr<T>(new T) or p.reset(new T).
    static const std::regex adopted_new(
        R"(\b(unique_ptr|shared_ptr|scoped_refptr|sk_sp|RefPtr|ComPtr|QScopedPointer|QSharedPointer|reset|[Aa]dopt\w*)\s*(<[^;]*?>)?\s*(\w+\s*)?[({]\s*new\b)");
    // Function-local `static T* x = new T;` is a deliberate never-destroyed singleton.
    static const std::regex leaky_singleton(R"(\bstatic\s+(const\s+)?[\w:<>]+\s*\*\s*(const\s+)?\w+\s*=\s*new\b)");
    if ((has("new")||has("delete")) && std::regex_search(line,manual_alloc) && !has("operator new")) {
        if (ctx.native_ui && std::regex_search(line,parent_owned_new))
            found.push_back({"CPP-UI-PARENT-OWNED-NEW","P3","Allocation handed to a parent object (likely Qt object tree)", "Verify the type derives from QObject and the parent outlives every use; otherwise treat as manual ownership"});
        else if (std::regex_search(line,adopted_new))
            found.push_back({"CPP-RAW-ALLOC","P3","`new` immediately adopted by an owning wrapper", "Prefer make_unique/make_shared when the constructor is accessible; verify the wrapper's deleter matches the allocation"});
        else if (std::regex_search(line,leaky_singleton))
            found.push_back({"CPP-RAW-ALLOC","P3","Intentionally leaked function-local singleton", "Confirm no destructor side effects are required at exit and initialization is thread-safe (C++11 magic statics)"});
        else
            found.push_back({"CPP-RAW-ALLOC","P2","Manual allocation/deallocation needs ownership and exception-safety review", "Assess RAII and ownership; preserve ABI and destruction order"});
    }
    static const std::regex async_captures_this(
        R"(\b(std::thread|std::jthread|std::async|QtConcurrent::run|QThreadPool|CreateThread|_beginthreadex)\b[^;]*\[(\s*[=&]\s*[,\]]|[^\]]*\bthis\b))");
    if ((has("thread")||has("async")||has("Concurrent")||has("Thread")) && std::regex_search(line,async_captures_this))
        found.push_back({"CPP-ASYNC-CAPTURES-THIS","P2","Background task captures `this` (explicitly or via [=]/[&])", "Prove the object outlives the task (join, cancellation token or weak handle); touch UI objects only from the UI thread"});
    if (ctx.native_ui) {
        // Qt's 3-argument connect(sender, signal, functor) has no context object: the
        // connection survives the receiver and the lambda may run on a dangling `this`.
        static const std::regex connect_no_context(R"(\bconnect\s*\(([^,]*),([^,]*),\s*\[)");
        if (has("connect") && std::regex_search(line,connect_no_context))
            found.push_back({"CPP-UI-CONNECT-NO-CONTEXT","P2","Signal connected to a lambda without a context/receiver object", "Use connect(sender, signal, context, lambda) so the connection dies with the receiver and runs on its thread"});
        if (has("processEvents("))
            found.push_back({"CPP-UI-NESTED-EVENT-LOOP","P2","Nested event processing allows re-entrancy and deletion of objects in use", "Prefer async continuation; if unavoidable, guard re-entry and use QPointer/weak handles after the call"});
        if (has("GWLP_USERDATA") && has("SetWindowLongPtr"))
            found.push_back({"CPP-UI-WINDOW-USERDATA","P3","Raw object pointer stored in window user data", "Identify owned vs borrowed lifetime: the slot does not transfer ownership. External/stack owners must outlive callbacks; invalidate the slot during teardown. Delete in WM_NCDESTROY only for an explicitly self-owned heap object, once, with no surviving references"});
    }
    if(has("reinterpret_cast<")) {
        // Casts to byte/void pointers (aliasing-permitted) or integer handles (LPARAM,
        // intptr_t, C-API user data) are idioms; casts to other object types are the risk.
        static const std::regex any_cast(R"(reinterpret_cast\s*<)");
        static const std::regex benign_cast(
            R"(reinterpret_cast\s*<\s*((const\s+)?(volatile\s+)?(void|char|unsigned\s+char|signed\s+char|std::byte|(std::)?u?int8_t)\s*(const\s*)?\*+\s*(const\s*)?|(std::)?(u?intptr_t|LONG_PTR|ULONG_PTR|LPARAM|WPARAM|LRESULT|DWORD_PTR|UINT_PTR|INT_PTR))\s*>)");
        const auto total=std::distance(std::sregex_iterator(line.begin(),line.end(),any_cast),std::sregex_iterator());
        const auto benign=std::distance(std::sregex_iterator(line.begin(),line.end(),benign_cast),std::sregex_iterator());
        if (benign>=total)
            found.push_back({"CPP-REINTERPRET-CAST","P3","Cast to byte/void pointer or integer handle", "Verify the round trip restores the original type and the object is still alive"});
        else
            found.push_back({"CPP-REINTERPRET-CAST","P2","Potential alignment, strict-aliasing, representation or lifetime hazard", "Document and test invariant, layout, alignment and required casts"});
    }
    if(has("detach") && detaches_thread(line,ctx))
        found.push_back({"CPP-DETACHED-THREAD","P2","Detached thread needs explicit lifetime/shutdown proof", "Prefer scoped join/cancellation when appropriate; test shutdown"});
    if(has("memcpy(")||has("std::memcpy("))
        found.push_back({"CPP-MEMCPY","P2","Raw byte copy requires type, size and overlap review", "Check object lifetime, representation and bounds; prefer typed operations where suitable"});
    if(ctx.header&&has("using namespace std;"))
        found.push_back({"CPP-HEADER-USING","P2","Header-wide using namespace can pollute downstream consumers", "Use qualified names or narrow using declarations"});
    static const std::regex catch_all(R"(catch\s*\(\s*\.\.\.\s*\)\s*\{\s*\})");
    if(has("...") && std::regex_search(line,catch_all))
        found.push_back({"CPP-EMPTY-CATCH","P2","Empty catch-all can hide broken error semantics", "Handle, propagate or justify deliberate failure suppression"});
    if(ctx.in_requires && has("std::inserter("))
        found.push_back({"CPP-INSERT-CONCEPT","P2","Requires-expression may only validate adapter construction, not actual insertion", "Constrain container.insert and compile concrete positive and negative usages"});
    static const std::regex unbounded_string(
        R"(\b(strcpy|strcat|wcscpy|wcscat|_mbscpy|_mbscat|_tcscpy|_tcscat|lstrcpy[AW]?|lstrcat[AW]?|sprintf|vsprintf|wsprintf[AW]?|gets)\s*\()");
    if((has("cpy")||has("cat")||has("printf")||has("gets")) && std::regex_search(line,unbounded_string))
        found.push_back({"CPP-UNBOUNDED-STRING","P2","Unbounded C string operation needs strict capacity verification", "Use bounded/typed operation; verify input lengths and terminator"});
    static const std::regex shell_call(R"(\b(std::)?(system|_wsystem|popen|_popen)\s*\()");
    if((has("system")||has("popen")) && std::regex_search(line,shell_call))
        found.push_back({"CPP-SHELL-CALL","P2","Shell command invocation requires injection and trust-boundary review", "Prefer argv-based subprocess calls; ensure trusted executable and arguments"});
    if(has("volatile ") && (has("bool ")||has("int ")))
        found.push_back({"CPP-VOLATILE-SHARED","P3","volatile is not a C++ synchronization primitive", "Check for shared-state intent; use atomic or mutex only when justified"});
    return found;
}
constexpr unsigned per_file_rule_cap=20;
} // namespace
bool is_test_path(const std::string& relative) {return path_is_test(relative);}
Scan scan(const fs::path& root,const std::optional<std::set<std::string>>& changed,const std::string& profile,bool exclude_tests) {
    Scan scan_result;
    const bool native_ui=profile=="native-ui";
    if (native_ui) scan_result.notes.push_back("native-ui rules enabled: connect without context, nested event loops, window user data, parent-owned allocation (P3)");
    std::error_code ec;
    fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
    if(ec) throw std::runtime_error("cannot scan source tree: "+ec.message());
    while(it!=end) {
        const auto path=it->path();
        const bool symlink=it->is_symlink(ec);
        if(ec){++scan_result.skipped;ec.clear();it.increment(ec);continue;}
        if(symlink) {++scan_result.skipped;it.disable_recursion_pending();it.increment(ec);continue;}
        if(it->is_directory(ec)) {
            if(excluded.count(utf8_bytes(path.filename().u8string()))) it.disable_recursion_pending();
        } else if(it->is_regular_file(ec) && is_source(path)) {
            const auto rel=relative_path(path,root);
            if (exclude_tests && path_is_test(rel)) ++scan_result.tests_excluded;
            else if (!changed || changed->count(rel)) {
                auto len=fs::file_size(path,ec);
                if(!ec && len<=2*1024*1024) scan_result.files.push_back(path);
                else ++scan_result.skipped;
            }
        }
        ec.clear();it.increment(ec);
        if(ec){scan_result.notes.push_back("scan incomplete at "+utf8_bytes(path.u8string())+": "+ec.message());ec.clear();}
    }
    std::sort(scan_result.files.begin(),scan_result.files.end());
    for(auto& p:scan_result.files) {
        std::string raw;
        try {raw=read_file(p);}catch(const std::exception& e){++scan_result.skipped;scan_result.notes.push_back(e.what());continue;}
        const auto masked=mask_code(raw);
        const auto filename=relative_path(p,root);
        const bool test_file=path_is_test(filename);
        // Pre-pass: identifiers declared as threads, so `.detach()` on e.g. SkPath is not a thread hint.
        std::set<std::string> thread_names;
        static const std::regex thread_decl(R"(\bj?thread\s+(\w+)\s*[({;=])");
        if (masked.find("thread")!=std::string::npos) for (std::sregex_iterator decl(masked.begin(),masked.end(),thread_decl),last;decl!=last;++decl) thread_names.insert((*decl)[1].str());
        static const std::regex thread_temporary(R"(\bj?thread\s*[({])");
        LineContext ctx;
        ctx.header=is_header(p);ctx.native_ui=native_ui;ctx.thread_names=&thread_names;
        std::istringstream lines(masked), originals(raw);
        std::string line,original;
        std::size_t line_number=0,last_requires_line=0,last_thread_temporary=0;
        std::map<std::pair<std::string,std::string>,unsigned> same_line_occurrences;
        std::map<std::string,unsigned> capped;
        bool file_capped=false;
        while(std::getline(lines,line)) {
            std::getline(originals,original);++line_number;
            if(line.find("requires")!=std::string::npos)last_requires_line=line_number;
            if(line.find("thread")!=std::string::npos && std::regex_search(line,thread_temporary))last_thread_temporary=line_number;
            ctx.in_requires=last_requires_line && line_number-last_requires_line<=12;
            ctx.thread_temporary_recent=last_thread_temporary && line_number-last_thread_temporary<=30;
            for(const auto& pat:rules(line,ctx)) {
                if(capped[pat.id]>=per_file_rule_cap) {++scan_result.capped;file_capped=true;continue;}
                ++capped[pat.id];
                const auto evidence=evidence_excerpt(original);
                auto& count=same_line_occurrences[{pat.id,evidence}];++count;
                std::string severity=pat.priority;
                if (test_file && severity=="P2") severity="P3";
                scan_result.findings.push_back({make_id(pat.id,filename,evidence,count),pat.id,severity,
                    "suspected",filename,pat.message,evidence,pat.remediation,line_number,false,false,
                    test_file?"test":"production"});
            }
        }
        if (file_capped) ++scan_result.capped_files;
    }
    if (scan_result.tests_excluded)
        scan_result.notes.push_back(std::to_string(scan_result.tests_excluded)+" test/fixture files excluded by --exclude-tests");
    if (scan_result.capped)
        scan_result.notes.push_back(std::to_string(scan_result.capped)+" additional hints not listed: limit of "
            +std::to_string(per_file_rule_cap)+" per rule per file reached in "+std::to_string(scan_result.capped_files)+" file(s)");
    return scan_result;
}

Profile load_profile(const Options& opt,const fs::path& root,Report& report) {
    Profile p;
    if(opt.profile=="legacy-code"||opt.profile=="prototype")p.require_tests=false;
    if(opt.profile=="library-sdk")p.require_abi=true;
    if(opt.profile=="low-latency")p.require_benchmark=true;
    // Only explicit reviewed profile can alter defaults; never trust target-repository policy implicitly.
    if(opt.profile_file) {
        const auto file=resolve(root,*opt.profile_file);
        auto source=read_file(file);
        std::string section,line;
        std::istringstream in(source);
        while(std::getline(in,line)) {
            line=trim(line.substr(0,line.find('#')));
            if(line.empty())continue;
            if(line.front()=='['){section=line;continue;}
            auto eq=line.find('=');if(eq==std::string::npos)continue;
            auto key=trim(line.substr(0,eq)),value=unquote(line.substr(eq+1));
            const bool enabled=value=="true";
            if(section=="[gates]" && (value=="true"||value=="false")) {
                if(key=="require_build")p.require_build=enabled;
                else if(key=="require_tests")p.require_tests=enabled;
                else if(key=="require_lint")p.require_lint=enabled;
                else if(key=="require_asan_ubsan")p.require_asan_ubsan=enabled;
                else if(key=="require_tsan")p.require_tsan=enabled;
                else if(key=="require_abi")p.require_abi=enabled;
                else if(key=="require_benchmark")p.require_benchmark=enabled;
            } else if(section=="[review]" && key=="block_heuristic_p2" && (value=="true"||value=="false")) p.block_heuristic_p2=enabled;
            else if(section=="[performance]" && key=="min_samples_per_variant")p.min_samples=std::stoul(value);
            else if(section=="[performance]" && key=="max_median_regression_percent")p.max_regression_pct=std::stod(value);
        }
        report.notes.push_back("Reviewed explicit policy loaded: "+utf8_bytes(file.u8string()));
    } else report.notes.push_back("Built-in policy "+opt.profile+"; target repository policies are NOT auto-loaded");
    if(p.min_samples<2||!std::isfinite(p.max_regression_pct)||p.max_regression_pct<0)
        throw std::runtime_error("invalid performance limits in profile");
    return p;
}
void load_baseline(const fs::path& file,Report& report) {
    const auto data=read_file(file);
    std::istringstream in(data);
    std::string line;std::set<std::string> ids;
    while(std::getline(in,line)) {
        line=trim(line);if(line.empty()||line[0]=='#')continue;
        ids.insert(line.substr(0,line.find('\t')));
    }
    for(auto& f:report.findings)f.existing=ids.count(f.id)!=0;
    report.notes.push_back("Reviewed baseline loaded: "+std::to_string(ids.size())+" IDs (does NOT suppress failing tests)");
}
void write_baseline(const fs::path& file,const Report& report) {
    std::vector<const Finding*> sorted;
    for(auto& f:report.findings)sorted.push_back(&f);
    std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a->id<b->id;});
    std::ostringstream out;out<<"# cpp-rails heuristic baseline v1 — manually review before adopting\n";
    for(auto f:sorted)out<<f->id<<'\t'<<f->path<<'\t'<<f->rule<<'\n';
    write_file(file,out.str());
}
void apply_exceptions(const fs::path& file,Report& report) {
    if (!fs::exists(file)) return;
    struct Exception {std::string id,path,owner,reason,expires;};
    const auto content=read_file(file);
    std::istringstream in(content);std::string line;Exception cur;bool has=false;
    std::vector<Exception> exceptions;
    while(std::getline(in,line)) {
        line=trim(line.substr(0,line.find('#')));
        if(line=="[[exception]]") {if(has)exceptions.push_back(cur);cur={};has=true;continue;}
        auto eq=line.find('=');if(eq==std::string::npos||!has)continue;
        auto key=trim(line.substr(0,eq)),val=unquote(line.substr(eq+1));
        if(key=="id")cur.id=val;else if(key=="path")cur.path=val;
        else if(key=="owner")cur.owner=val;else if(key=="reason")cur.reason=val;
        else if(key=="expires")cur.expires=val;
    }
    if(has)exceptions.push_back(cur);
    const auto today=today_utc();
    for(auto& e:exceptions) {
        if(e.id.empty()||e.path.empty()||e.owner.empty()||e.reason.empty()||!valid_date(e.expires)) {
            report.notes.push_back("Rejected incomplete suppression (id/path/owner/reason/valid expiry required)");continue;
        }
        if(e.expires<today){report.notes.push_back("Rejected expired suppression "+e.id);continue;}
        bool matched=false;
        for(auto& f:report.findings) {
            if(f.id==e.id&&glob(e.path,f.path)&&(f.severity=="P2"||f.severity=="P3")) {
                f.suppressed=true;matched=true;
            }
        }
        if(!matched)report.notes.push_back("Suppression has no matching P2/P3 finding: "+e.id);
    }
}

// Multi-config generators (Visual Studio, Xcode, Ninja Multi-Config) need the same
// configuration for `cmake --build --config` and `ctest -C`; single-config generators ignore it.
std::string build_config(const Options& opt,const fs::path& build_dir) {
    if (opt.config) return *opt.config;
    std::ifstream cache(build_dir/"CMakeCache.txt");
    std::string line;
    while (std::getline(cache,line)) {
        if (line.rfind("CMAKE_BUILD_TYPE:",0)!=0) continue;
        const auto eq=line.find('=');
        if (eq==std::string::npos) break;
        const auto value=trim(line.substr(eq+1));
        if (!value.empty()) return value;
        break;
    }
    return "Debug";  // CMake's default configuration for multi-config builds
}

namespace {
struct Pair {std::vector<double> baseline,candidate;};
double median(std::vector<double> values) {
    std::sort(values.begin(),values.end());
    auto n=values.size();
    return n%2 ? values[n/2] : (values[n/2-1]+values[n/2])/2.0;
}
double mad(const std::vector<double>& values,double med) {
    std::vector<double> deviations;deviations.reserve(values.size());
    for(double v:values)deviations.push_back(std::fabs(v-med));
    return median(std::move(deviations));
}
std::vector<std::string> split(const std::string& text,char separator) {
    std::istringstream in(text);std::string item;std::vector<std::string> out;
    while(std::getline(in,item,separator))out.push_back(trim(item));
    return out;
}
std::vector<std::string> positive_compiler_flags(const Options& opt) {
    const auto lower_name=lower(utf8_bytes(fs::path(opt.compiler).filename().u8string()));
    if (lower_name=="cl"||lower_name=="cl.exe")return {opt.compiler,"/nologo","/std:"+opt.cpp_standard,"/Zs"};
    return {opt.compiler,"-std="+opt.cpp_standard,"-Wall","-Wextra","-fsyntax-only"};
}
void template_gate(const Options& opt,const fs::path& root,Report& report) {
    if(!opt.compile_checks) {
        report.notes.push_back("Template scan is a heuristic. Use templates --check --source with concrete instantiations.");
        return;
    }
    if(!opt.source) {
        report.gate("template instantiation",Status::Unverified,"--check needs --source path to an actual positive/negative instantiation");
        return;
    }
    const auto source=resolve(root,*opt.source);
    if (!fs::is_regular_file(source)) throw std::runtime_error("missing template instantiation source: "+utf8_bytes(source.u8string()));
    auto args=positive_compiler_flags(opt);args.push_back(utf8_bytes(source.u8string()));
    auto result=exec(args,root);
    if (!result.started)report.gate("template instantiation",Status::Unverified,"compiler not available: "+opt.compiler);
    else if (!opt.expect_error.empty()) {
        bool expected=result.exit_code!=0 && result.output.find(opt.expect_error)!=std::string::npos;
        report.gate("template negative compile",expected?Status::Pass:Status::Fail,
            process_info(result)+"; expected diagnostic fragment: "+opt.expect_error);
    } else record_command(report,"template positive compile",result,true);
    report.notes.push_back("Syntax-only compile is concrete for this translation unit; it does not prove generic semantic correctness across all types");
}
void compiler_remarks(const Options& opt,const fs::path& root,Report& report) {
    if (!opt.source) {report.gate("compiler optimization remarks",Status::Unverified,"--source is required");return;}
    const auto source=resolve(root,*opt.source);
    if (!fs::is_regular_file(source))throw std::runtime_error("source not found: "+utf8_bytes(source.u8string()));
    const auto comp=lower(utf8_bytes(fs::path(opt.compiler).filename().u8string()));
    if(comp=="cl.exe"||comp=="cl"){
        report.gate("compiler optimization remarks",Status::Unverified,"MSVC standalone optimization remarks are not automated; use /Qvec-report or toolchain profiler");return;
    }
    const auto scratch=fs::temp_directory_path() / ("cpp-rails-asm-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s");
    std::vector<std::string> args={opt.compiler,"-std="+opt.cpp_standard,"-O3","-S",utf8_bytes(source.u8string()),"-o",utf8_bytes(scratch.u8string())};
    if(comp.find("clang")!=std::string::npos){
        args.insert(args.begin()+3,{"-Rpass=loop-vectorize","-Rpass-missed=loop-vectorize","-Rpass-analysis=loop-vectorize"});
    } else {
        args.insert(args.begin()+3,{"-fopt-info-vec-optimized","-fopt-info-vec-missed"});
    }
    const auto r=exec(args,root);
    std::error_code ec;fs::remove(scratch,ec);
    record_command(report,"compiler optimization remarks",r,true);
    if (!r.output.empty())report.notes.push_back("Compiler remarks (first 1500 chars): "+r.output.substr(0,1500));
    report.notes.push_back("Standalone compilation may differ from real project flags/includes; compare assembly and measurements in representative builds");
}
// -I / /I / -iquote / -isystem directories from a compilation database (best effort).
std::vector<fs::path> compile_db_include_dirs(const fs::path& database,const fs::path& root) {
    std::vector<fs::path> dirs;
    std::error_code ec;
    if (!fs::is_regular_file(database,ec)) return dirs;
    auto text=read_file(database);
    std::string unescaped;unescaped.reserve(text.size());
    for (std::size_t i=0;i<text.size();++i) {
        if (text[i]=='\\' && i+1<text.size() && (text[i+1]=='\\'||text[i+1]=='"')) {unescaped.push_back(text[++i]);continue;}
        unescaped.push_back(text[i]);
    }
    static const std::regex flag(R"((?:^|[\s"\[,])(?:-I|/I|-iquote|-isystem)\s*"?([^"\s,\]]+))");
    std::set<std::string> seen;
    for (std::sregex_iterator it(unescaped.begin(),unescaped.end(),flag),end;it!=end && dirs.size()<64;++it) {
        const fs::path dir=resolve(root,fs::path((*it)[1].str())).lexically_normal();
        if (seen.insert(utf8_bytes(dir.generic_u8string())).second && fs::is_directory(dir,ec)) dirs.push_back(dir);
    }
    return dirs;
}
} // namespace

void report_topology(const fs::path& root,const std::vector<fs::path>& files,const std::vector<fs::path>& include_dirs,Report& report) {
    std::map<std::string,std::set<std::string>> graph;
    std::map<std::string,std::size_t> fan_in;
    const std::regex re(R"re(^\s*#\s*include\s*"([^"]+)")re");
    // Projects often include from a repository root above --root ("flutter/shell/x.h"):
    // ancestors of root are tried as include roots, and only targets inside root become edges.
    std::vector<fs::path> ancestors;
    for (auto a=root.parent_path();!a.empty() && ancestors.size()<10;a=a.parent_path()) {
        ancestors.push_back(a);
        if (a==a.parent_path()) break;
    }
    std::size_t direct=0,via_include_dir=0,via_ancestor=0,outside=0,unresolved=0;
    std::error_code ec;
    for(auto& f:files) {
        std::istringstream in(read_file(f));std::string line;const auto name=relative_path(f,root);
        graph[name];
        while(std::getline(in,line)) {
            std::smatch m;
            if(!std::regex_search(line,m,re))continue;
            const fs::path include=m[1].str();
            std::optional<fs::path> target;int kind=0;
            auto attempt=[&](const fs::path& base,int k){
                if (target) return;
                auto candidate=(base/include).lexically_normal();
                if (fs::is_regular_file(candidate,ec)) {target=candidate;kind=k;}
            };
            attempt(f.parent_path(),0);attempt(root,0);
            for (auto& d:include_dirs) attempt(d,1);
            for (auto& a:ancestors) attempt(a,2);
            if (!target) {++unresolved;continue;}
            auto dep=relative_path(*target,root);
            if (dep.rfind("..",0)==0 || !is_source(*target)) {++outside;continue;}
            (kind==0?direct:kind==1?via_include_dir:via_ancestor)++;
            if (graph[name].insert(dep).second) fan_in[dep]++;
        }
    }
    std::vector<std::pair<std::string,std::size_t>> ranked(fan_in.begin(),fan_in.end());
    std::sort(ranked.begin(),ranked.end(),[](const auto& x,const auto& y){return x.second>y.second;});
    const auto edge_count=std::accumulate(graph.begin(),graph.end(),std::size_t(0),[](auto total,const auto& entry){return total+entry.second.size();});
    report.notes.push_back("Project-local include topology: "+std::to_string(graph.size())+" source/header nodes, "+std::to_string(edge_count)+" include edges. Not a semantic module/dependency graph.");
    report.notes.push_back("Quoted include resolution: "+std::to_string(direct)+" relative/root, "+std::to_string(via_include_dir)
        +" via include dirs ("+std::to_string(include_dirs.size())+" searched), "+std::to_string(via_ancestor)+" via ancestor include roots, "
        +std::to_string(outside)+" outside --root, "+std::to_string(unresolved)+" unresolved"
        +(unresolved?" (add --include-dir or a compile_commands.json for generated/external headers)":""));
    for(std::size_t i=0;i<std::min<std::size_t>(8,ranked.size());++i)
        report.notes.push_back("Highly included: "+ranked[i].first+" fan-in="+std::to_string(ranked[i].second));
    // Directed DFS on include relationships: a cycle is diagnostic input, not proof of bad architecture.
    std::map<std::string,int> state;
    unsigned cycles=0;
    auto visit=[&](auto&& self,const std::string& node)->void {
        if(cycles>=8)return;
        state[node]=1;
        for(auto& dep:graph[node]) {
            if(state[dep]==1) {++cycles;report.notes.push_back("Potential include cycle: "+node+" -> "+dep);}
            else if(state[dep]==0)self(self,dep);
        }
        state[node]=2;
    };
    for(auto& [node,_]:graph) if(state[node]==0)visit(visit,node);
    if (!cycles)report.notes.push_back("No direct project-local include cycle found in analyzed files; other build dependencies may still exist");
}

namespace {
// CTest's dashboard Test.xml is available in CMake 3.16. Run from the build
// directory (the newer --test-dir and --output-junit options are not required).
// No dashboard submit step is requested. Only structured statuses establish PASS.
void ctest_gate(const fs::path& build,const std::string& config,const std::string& name,bool required,Report& report) {
    auto result_file=[&]() -> fs::path {
        if(!fs::is_regular_file(build/"Testing"/"TAG"))return {};
        const auto tag=first_line(read_file(build/"Testing"/"TAG"));
        if(!std::regex_match(tag,std::regex("[0-9]{8}-[0-9]{4}")))throw std::runtime_error("invalid CTest TAG");
        return build/"Testing"/tag/"Test.xml";
    };
    try {
        const auto old=result_file();
        if(!old.empty())fs::remove(old); // never accept evidence from an earlier invocation
        const auto process=exec({"ctest","-T","Test","--no-compress-output","-C",config,"--output-on-failure"},build);
        const auto file=result_file();
        if(!process.started||file.empty()||!fs::is_regular_file(file)) {
            report.gate(name,process.started&&process.exit_code!=0?Status::Fail:Status::Unverified,
                "fresh structured test results unavailable; "+process_info(process),required);return;
        }
        const auto xml=read_file(file);
        unsigned passed=0,failed=0,skipped=0,disabled=0,unknown=0,listed=0;
        for(std::size_t p=0;(p=xml.find("<Test>",p))!=std::string::npos;p+=6)++listed;
        for(std::size_t p=0;(p=xml.find("<Test Status=\"",p))!=std::string::npos;) {
            const auto end=xml.find("</Test>",p);
            if(end==std::string::npos){++unknown;break;}
            const auto item=xml.substr(p,end-p);p=end+7;
            if(item.find("<Test Status=\"passed\">")==0)++passed;
            else if(item.find("<Test Status=\"failed\">")==0)++failed;
            else if(item.find("<Test Status=\"notrun\">")==0) {
                // These are CTest XML schema values, not localized console text.
                const auto completion=item.find("name=\"Completion Status\"");
                const auto value=completion==std::string::npos?std::string::npos:item.find("<Value>",completion);
                const auto close=value==std::string::npos?std::string::npos:item.find("</Value>",value);
                const auto reason=close==std::string::npos?"":item.substr(value+7,close-value-7);
                if(reason=="Disabled")++disabled;
                else if(reason=="SKIP_RETURN_CODE"||reason=="SKIP_REGULAR_EXPRESSION_MATCHED")++skipped;
                else ++unknown;
            } else ++unknown;
        }
        const bool complete=xml.find("</Testing>")!=std::string::npos&&xml.find("</Site>")!=std::string::npos
            && listed==passed+failed+skipped+disabled+unknown;
        const auto status=failed||process.exit_code!=0?Status::Fail:
            complete&&passed>0&&skipped==0&&disabled==0&&unknown==0?Status::Pass:Status::Unverified;
        report.gate(name,status,"passed="+std::to_string(passed)+", failed="+std::to_string(failed)+
            ", skipped="+std::to_string(skipped)+", disabled="+std::to_string(disabled)+", unverified="+std::to_string(unknown)+
            "; "+(complete?"structured results":"incomplete structured results")+"; exit="+std::to_string(process.exit_code)+
            "; evidence="+utf8_bytes(file.u8string()),required);
    } catch(const std::exception& e) {report.gate(name,Status::Unverified,std::string("cannot verify fresh CTest evidence: ")+e.what(),required);}
}
void sanitizer_gate(const Options& opt,const fs::path& root,const std::string& mode,Report& report) {
    // Sanitizers must instrument both compilation and linkage, and use isolated build trees.
    if(!fs::is_regular_file(root/"CMakeLists.txt")){
        report.gate(mode,Status::Unverified,"CMake project is required; run your project's sanitizer workflow manually");return;
    }
    const auto work=root/".cpp-rails"/("build-"+mode);
    const std::string flags=(mode=="tsan"?"-fsanitize=thread":"-fsanitize=address,undefined")+std::string(" -fno-omit-frame-pointer -g -O1");
    const auto config=exec({"cmake","-S",utf8_bytes(root.u8string()),"-B",utf8_bytes(work.u8string()),
        "-DCMAKE_CXX_COMPILER="+opt.compiler,"-DCMAKE_CXX_FLAGS="+flags,"-DCMAKE_EXE_LINKER_FLAGS="+flags,
        "-DCMAKE_SHARED_LINKER_FLAGS="+flags,"-DCMAKE_BUILD_TYPE=Debug","-DBUILD_TESTING=ON"},root);
    record_command(report,mode+" configure",config,true);
    if(!config.started || config.exit_code!=0) {
        report.gate(mode+" tests",Status::Unverified,"configure failed; instrumented tests were not run");return;
    }
    const auto build=exec({"cmake","--build",utf8_bytes(work.u8string()),"--config","Debug","--parallel","2"},root);
    record_command(report,mode+" build",build,true);
    if(!build.started || build.exit_code!=0){report.gate(mode+" tests",Status::Unverified,"instrumented build failed; tests were not run");return;}
    ctest_gate(work,"Debug",mode+" tests",true,report);
}
void cmake_gate(const Options& opt,const fs::path& root,const Profile& profile,Report& report) {
    const auto build=resolve(root,opt.build_dir.value_or(fs::path("build")));
    if(opt.no_build) {
        report.gate("CMake build",Status::Unverified,"operator explicitly skipped build",profile.require_build);
    } else if (profile.require_build || fs::exists(build/"CMakeCache.txt")) {
        if (!fs::exists(build/"CMakeCache.txt")) {
            if(!opt.configure) {
                report.gate("CMake build",Status::Unverified,"CMake build tree unconfigured; use --configure or configure manually");
                if(profile.require_tests&&!opt.no_tests)report.gate("CTest",Status::Unverified,"no configured build tree");
                return;
            }
            if(!fs::exists(root/"CMakeLists.txt")) {
                report.gate("CMake configure",Status::Unverified,"target project does not use CMake");return;
            }
            std::vector<std::string> configure_args={"cmake","-S",utf8_bytes(root.u8string()),"-B",utf8_bytes(build.u8string()),"-DCMAKE_EXPORT_COMPILE_COMMANDS=ON","-DBUILD_TESTING=ON"};
            if(opt.config) configure_args.push_back("-DCMAKE_BUILD_TYPE="+*opt.config);
            const auto conf=exec(configure_args,root);
            record_command(report,"CMake configure",conf,true);
            if(!conf.started||conf.exit_code!=0) {
                report.gate("CMake build",Status::Unverified,"configuration failed");
                if(profile.require_tests)report.gate("CTest",Status::Unverified,"configuration failed");
                return;
            }
        }
        const auto b=exec({"cmake","--build",utf8_bytes(build.u8string()),"--config",build_config(opt,build),"--parallel","2"},root);
        record_command(report,"CMake build",b,profile.require_build);
        if(!b.started||b.exit_code!=0){
            if(profile.require_tests&&!opt.no_tests)report.gate("CTest",Status::Unverified,"build failed, tests not run");
            return;
        }
    }
    if(opt.no_tests) {
        report.gate("CTest",Status::Unverified,"operator explicitly skipped tests",profile.require_tests);return;
    }
    if(profile.require_tests || fs::exists(build/"CTestTestfile.cmake")) {
        if(!fs::exists(build/"CMakeCache.txt")) {
            report.gate("CTest",Status::Unverified,"no configured build tree",profile.require_tests);return;
        }
        ctest_gate(build,build_config(opt,build),"CTest",profile.require_tests,report);
    }
}
void lint_gate(const Options& opt,const fs::path& root,const std::vector<fs::path>& files,Report& report,bool required) {
    const auto build=resolve(root,opt.build_dir.value_or(fs::path("build")));
    if(!fs::is_regular_file(build/"compile_commands.json")) {
        report.gate("clang-tidy",Status::Unverified,"compilation database missing; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON",required);return;
    }
    std::vector<fs::path> units;
    for(auto& path:files)if(!is_header(path)&&lower(utf8_bytes(path.extension().u8string()))!=".c")units.push_back(path);
    if (units.empty()) {
        report.gate("clang-tidy",Status::NotApplicable,"no C++ translation units in selected scope",false);return;
    }
    if(units.size()>30) {
        report.gate("clang-tidy",Status::Unverified,"scope has "+std::to_string(units.size())+" translation units; CLI limit is 30 to avoid surprise long-running repository scan",required);return;
    }
    std::vector<std::string> argv={"clang-tidy","-p",utf8_bytes(build.u8string()),"--warnings-as-errors=*"};
    for(auto& f:units)argv.push_back(utf8_bytes(f.u8string()));
    const auto r=exec(argv,root);record_command(report,"clang-tidy",r,required);
    report.notes.push_back("clang-tidy runs without --fix; project .clang-tidy policy and compiler database may alter warnings");
}
void abi_gate(const Options& opt,const fs::path& root,Report& report,bool required) {
    if(!opt.abi_old || !opt.abi_new){
        report.gate("ABI compatibility",Status::Unverified,"need both --abi-old and --abi-new for libabigail comparison",required);return;
    }
    const auto old=resolve(root,*opt.abi_old),newer=resolve(root,*opt.abi_new);
    if(!fs::is_regular_file(old)||!fs::is_regular_file(newer)) {
        report.gate("ABI compatibility",Status::Unverified,"ABI library path(s) do not exist",required);return;
    }
    const auto r=exec({"abidiff",utf8_bytes(old.u8string()),utf8_bytes(newer.u8string())},root);
    record_command(report,"ABI compatibility",r,required);
    report.notes.push_back("abidiff is platform/toolchain dependent and may require debug symbols; public source/API/consumer compatibility still needs separate verification");
}
void compiler_matrix_gate(const Options& opt,const fs::path& root,Report& report) {
    if(opt.compiler_matrix.empty())return;
    const auto compilers=split(opt.compiler_matrix,',');
    if(compilers.empty()||compilers.size()>8)throw std::runtime_error("compiler matrix needs 1..8 compiler names");
    if(!fs::exists(root/"CMakeLists.txt")) {
        report.gate("compiler matrix",Status::Unverified,"target project does not use CMake");return;
    }
    for(auto& c:compilers) {
        if(c.empty())throw std::runtime_error("compiler matrix contains blank compiler");
        std::string safe;
        for(unsigned char ch:c)safe.push_back(std::isalnum(ch)?ch:'_');
        const auto work=root/".cpp-rails"/("matrix-"+safe);
        auto conf=exec({"cmake","-S",utf8_bytes(root.u8string()),"-B",utf8_bytes(work.u8string()),"-DCMAKE_CXX_COMPILER="+c,"-DBUILD_TESTING=ON"},root);
        record_command(report,c+" matrix configure",conf,true);
        if(!conf.started||conf.exit_code!=0)continue;
        const auto config=build_config(opt,work);
        auto build=exec({"cmake","--build",utf8_bytes(work.u8string()),"--config",config,"--parallel","2"},root);
        record_command(report,c+" matrix build",build,true);
        if(!build.started||build.exit_code!=0)continue;
        ctest_gate(work,config,c+" matrix tests",true,report);
    }
}
void lifetime_gate(const Options& opt,const fs::path& root,Report& report) {
    if(!opt.lifetime)return;
    if(!opt.source) {
        report.gate("lifetime warnings",Status::Unverified,"--lifetime needs --source; this is not a whole-project proof");return;
    }
    const auto source=resolve(root,*opt.source);
    if(!fs::is_regular_file(source)) {
        report.gate("lifetime warnings",Status::Unverified,"source does not exist");return;
    }
    auto r=exec({opt.compiler,"-std="+opt.cpp_standard,"-fsyntax-only","-Wall","-Wextra","-Wdangling","-Werror=dangling",utf8_bytes(source.u8string())},root);
    if(!r.started)report.gate("lifetime warnings",Status::Unverified,"compiler not available");
    else if(r.output.find("unknown warning")!=std::string::npos||r.output.find("unrecognized command-line option")!=std::string::npos)
        report.gate("lifetime warnings",Status::Unverified,"compiler does not support requested diagnostic");
    else record_command(report,"lifetime warnings",r,false);
    report.notes.push_back("Lifetime diagnostics are incomplete; no single compiler flag proves absence of dangling values");
}
} // namespace
void benchmark_compare(const fs::path& file,unsigned min_samples,double max_pct,Report& report) {
    auto content=read_file(file);
    std::istringstream in(content);std::string line;
    while(std::getline(in,line))if(!trim(line).empty() && trim(line)[0]!='#')break;
    if(in.fail() && line.empty())throw std::runtime_error("empty benchmark CSV/TSV");
    const char sep=line.find('\t')!=std::string::npos?'\t':',';
    auto columns=split(line,sep);
    auto index=[&](const std::string& k){
        auto it=std::find(columns.begin(),columns.end(),k);
        if(it==columns.end())throw std::runtime_error("benchmark file missing "+k);
        return static_cast<std::size_t>(it-columns.begin());
    };
    const auto b=index("benchmark"),v=index("variant"),n=index("run_ns");
    std::map<std::string,Pair> groups;
    unsigned row=1;
    while(std::getline(in,line)) {
        ++row;if(trim(line).empty()||trim(line)[0]=='#')continue;
        auto cells=split(line,sep);
        if(cells.size()<=std::max({b,v,n}))throw std::runtime_error("malformed row "+std::to_string(row));
        double value=0;
        try {std::size_t parsed=0;value=std::stod(cells[n],&parsed);if(parsed!=cells[n].size())throw std::runtime_error("trailing chars");}
        catch(...){throw std::runtime_error("invalid run_ns at row "+std::to_string(row));}
        if (!std::isfinite(value)||value<=0)throw std::runtime_error("run_ns must be finite and positive at row "+std::to_string(row));
        if(cells[b].empty())throw std::runtime_error("blank benchmark at row "+std::to_string(row));
        if(cells[v]=="baseline")groups[cells[b]].baseline.push_back(value);
        else if(cells[v]=="candidate")groups[cells[b]].candidate.push_back(value);
        else throw std::runtime_error("invalid variant at row "+std::to_string(row));
    }
    if(groups.empty())throw std::runtime_error("no benchmark samples found");
    bool failed=false,inconclusive=false;
    for(auto& [name,pair]:groups) {
        if(pair.baseline.size()<min_samples || pair.candidate.size()<min_samples) {
            inconclusive=true;report.notes.push_back(name+": insufficient repeats ("+std::to_string(pair.baseline.size())+" baseline, "+std::to_string(pair.candidate.size())+" candidate)");continue;
        }
        const auto bm=median(pair.baseline),cm=median(pair.candidate);
        const auto bmad=mad(pair.baseline,bm),cmad=mad(pair.candidate,cm);
        const auto change=(cm/bm-1)*100;
        const bool exceeds_noise=std::fabs(cm-bm)>3*(bmad+cmad+std::numeric_limits<double>::epsilon());
        std::ostringstream note;
        note<<std::fixed<<std::setprecision(3)<<name<<": baseline median "<<bm<<"ns MAD "<<bmad
            <<", candidate "<<cm<<"ns MAD "<<cmad<<", delta "<<change<<"%";
        report.notes.push_back(note.str());
        if(!exceeds_noise){inconclusive=true;report.notes.push_back(name+": effect within a simple MAD noise heuristic; INCONCLUSIVE, not a win");}
        else if(change>max_pct){failed=true;report.notes.push_back(name+": regression exceeds configured threshold");}
    }
    report.gate("benchmark regression",failed?Status::Fail:(inconclusive?Status::Unverified:Status::Pass),
        "max median regression "+std::to_string(max_pct)+"%; >="+std::to_string(min_samples)+" repeats/variant; 3xMAD heuristic, not statistical proof");
}

int dispatch(const Options& opt,Report& report) {
    const auto root=fs::canonical(opt.root.value_or(fs::path(".")));
    if(!fs::is_directory(root))throw std::runtime_error("root is not a directory");
    report.root=utf8_bytes(root.u8string());report.profile=opt.profile;
    report.action=opt.action+(opt.subaction.empty()?"":" "+opt.subaction);
    auto profile=load_profile(opt,root,report);
    if(opt.action=="perf") {
        if(opt.subaction=="compare") {
            if(!opt.results)report.gate("benchmark regression",Status::Unverified,"--results required");
            else benchmark_compare(resolve(root,*opt.results),opt.min_samples.value_or(profile.min_samples),
                opt.max_regression.value_or(profile.max_regression_pct),report);
        } else compiler_remarks(opt,root,report);
    } else {
        auto changed=opt.changed?std::optional<std::set<std::string>>(changed_paths(root,&report.notes)):std::nullopt;
        auto result=scan(root,changed,opt.profile,opt.exclude_tests);
        if(changed) {
            std::size_t missing=0;for(const auto& path:*changed)if(!fs::exists(root/path_from_utf8(path)))++missing;
            report.notes.push_back("Changed scope: "+std::to_string(changed->size())+" paths inside --root, "+std::to_string(missing)+
                " deleted/missing (not scanned); repository paths outside --root are excluded. Renames include old and new paths; untracked files included.");
        }
        report.files_scanned=result.files.size();report.files_skipped=result.skipped;
        report.findings_capped=result.capped;
        report.findings=std::move(result.findings);
        // Production P2 first so the 200-row Markdown table and SARIF consumers see what matters.
        std::stable_sort(report.findings.begin(),report.findings.end(),[](const Finding& a,const Finding& b){
            return std::tuple(a.scope=="test",a.severity)<std::tuple(b.scope=="test",b.severity);
        });
        report.notes.insert(report.notes.end(),result.notes.begin(),result.notes.end());
        report.notes.push_back("Pattern scanning uses a lightweight C++ lexer; heuristics are SUSPECTED and cannot establish P0/P1, UB, concurrency or semantic correctness");
        if(opt.action=="baseline") {
            const auto file=resolve(root,opt.output.value_or(fs::path(".cpp-rails/baseline.txt")));
            write_baseline(file,report);
            report.gate("heuristic baseline",Status::Pass,"wrote "+std::to_string(report.findings.size())+" candidate IDs to "+utf8_bytes(file.u8string())+"; manually review");
            return 0;
        }
        if(opt.baseline)load_baseline(resolve(root,*opt.baseline),report);
        apply_exceptions(resolve(root,opt.exception_file.value_or(fs::path(".cpp-rails/exceptions.toml"))),report);
        if(opt.action=="topology") {
            std::vector<fs::path> dirs;
            for (auto& d:opt.include_dirs) dirs.push_back(resolve(root,d).lexically_normal());
            for (auto& db:{resolve(root,opt.build_dir.value_or(fs::path("build")))/"compile_commands.json",root/"compile_commands.json"}) {
                auto found=compile_db_include_dirs(db,root);
                if (!found.empty()) report.notes.push_back("Include dirs read from "+utf8_bytes(db.u8string())+": "+std::to_string(found.size()));
                dirs.insert(dirs.end(),found.begin(),found.end());
            }
            report_topology(root,result.files,dirs,report);
        }
        if(opt.action=="triage") {
            std::stable_sort(report.findings.begin(),report.findings.end(),[](const Finding& a,const Finding& b){
                return std::tuple(a.existing,a.suppressed,a.scope=="test",a.severity,a.path,a.line)
                     < std::tuple(b.existing,b.suppressed,b.scope=="test",b.severity,b.path,b.line);
            });
            report.notes.push_back("Triage ranks NEW unsuppressed hints first; verify every candidate before recommending a refactor");
        }
        if(opt.action=="templates")template_gate(opt,root,report);
        if(opt.check_program) {
            cmake_gate(opt,root,profile,report);
            if(opt.lint||profile.require_lint)lint_gate(opt,root,result.files,report,opt.lint||profile.require_lint);
            if(opt.sanitizer=="asan-ubsan"||profile.require_asan_ubsan)sanitizer_gate(opt,root,"asan-ubsan",report);
            if(opt.sanitizer=="tsan"||profile.require_tsan)sanitizer_gate(opt,root,"tsan",report);
            compiler_matrix_gate(opt,root,report);
            lifetime_gate(opt,root,report);
            if(profile.require_abi||opt.abi_old||opt.abi_new)abi_gate(opt,root,report,true);
            if(profile.require_benchmark) {
                if(!opt.results)report.gate("representative benchmark",Status::Unverified,"required by low-latency profile; --results missing");
                else benchmark_compare(resolve(root,*opt.results),opt.min_samples.value_or(profile.min_samples),
                    opt.max_regression.value_or(profile.max_regression_pct),report);
            } else if(opt.results) benchmark_compare(resolve(root,*opt.results),opt.min_samples.value_or(profile.min_samples),
                opt.max_regression.value_or(profile.max_regression_pct),report);
            report.notes.push_back("Independent reviewer and behavioral/FFI/consumer checks are agent responsibilities; this CLI does not falsely mark them PASS");
        }
        if(profile.block_heuristic_p2 && report.new_unsuppressed_p2())report.gate("review P2 heuristic",Status::Fail,"strict profile requests review of new P2 patterns");
    }
    if(report.has_failure())return 1;
    if(report.required_unverified()||(opt.strict && report.any_unverified()))return 2;
    return 0;
}
int cli_entry(bool check_program,int argc,const char* const argv[]) {
    try {
        std::vector<std::string> args;
        for(int i=1;i<argc;++i)args.push_back(argv[i]);
        auto opt=parse_options(check_program,args);
        if(opt.help){std::cout<<usage(check_program);return 0;}
        Report report;
        auto result=dispatch(opt,report);
        const auto formatted=opt.format=="json"?json(report):opt.format=="sarif"?sarif(report):markdown(report);
        if(opt.output && opt.action!="baseline") {
            write_file(resolve(fs::canonical(opt.root.value_or(fs::path("."))),*opt.output),formatted);
            std::cout<<"Report written to "<<opt.output->string()<<'\n';
        } else std::cout<<formatted;
        return result;
    } catch(const std::exception& e) {
        std::cerr<<"cpp-rails error: "<<e.what()<<"\nUse --help for options.\n";
        return 2;
    }
}
} // namespace cpprails
