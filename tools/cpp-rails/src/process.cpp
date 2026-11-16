#include "cpp_rails.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
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


}
