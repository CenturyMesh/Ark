#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace native_test {
namespace fs = std::filesystem;

inline void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

inline std::string pathUtf8(const fs::path& path) {
    auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

inline std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "Cannot open " + pathUtf8(path));
    return std::string(std::istreambuf_iterator<char>(in), {});
}

class TempDirectory {
public:
    explicit TempDirectory(const std::string& prefix) {
#ifdef _WIN32
        const auto pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
        const auto pid = static_cast<unsigned long>(getpid());
#endif
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path_ = fs::temp_directory_path() /
                (prefix + std::to_string(pid) + "-" + std::to_string(tick) + "-" + std::to_string(attempt));
            if (fs::create_directory(path_)) return;
        }
        throw std::runtime_error("Cannot create an isolated temporary directory");
    }
    ~TempDirectory() { std::error_code ignored; fs::remove_all(path_, ignored); }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

#ifdef _WIN32
inline std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                     static_cast<int>(utf8.size()), nullptr, 0);
    check(length > 0, "Invalid UTF-8 process argument");
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                        static_cast<int>(utf8.size()), out.data(), length);
    return out;
}
inline std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += c; }
        else { out.append(slashes, L'\\'); out += c; }
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}
#endif

class Process {
public:
    Process(const fs::path& exe, const std::vector<std::string>& args,
            const fs::path& cwd = fs::current_path()) {
#ifdef _WIN32
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE childIn = nullptr, childOut = nullptr, childErr = nullptr;
        if (!CreatePipe(&childIn, &inWrite_, &security, 0) ||
            !CreatePipe(&outRead_, &childOut, &security, 0) ||
            !CreatePipe(&errRead_, &childErr, &security, 0))
            throw std::runtime_error("CreatePipe failed");
        // Only the three child ends are inheritable.
        SetHandleInformation(inWrite_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead_, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW start{};
        start.cb = sizeof(start);
        start.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        start.wShowWindow = SW_HIDE;
        start.hStdInput = childIn;
        start.hStdOutput = childOut;
        start.hStdError = childErr;
        std::wstring command = quote(exe.wstring());
        for (const auto& arg : args) command += L" " + quote(wide(arg));
        std::vector<wchar_t> mutableCommand(command.begin(), command.end());
        mutableCommand.push_back(L'\0');
        PROCESS_INFORMATION info{};
        const std::wstring executable = exe.wstring();
        const std::wstring directory = cwd.wstring();
        job_ = CreateJobObjectW(nullptr, nullptr);
        check(job_ != nullptr, "CreateJobObject failed");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        check(SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                      &limits, sizeof(limits)) != 0, "SetInformationJobObject failed");
        const BOOL made = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr,
                                         nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                         nullptr, directory.c_str(), &start, &info);
        CloseHandle(childIn); CloseHandle(childOut); CloseHandle(childErr);
        if (!made) throw std::runtime_error("CreateProcessW failed: " + std::to_string(GetLastError()));
        process_ = info.hProcess;
        if (!AssignProcessToJobObject(job_, process_)) {
            TerminateProcess(process_, 1);
            CloseHandle(info.hThread);
            throw std::runtime_error("AssignProcessToJobObject failed");
        }
        ResumeThread(info.hThread);
        CloseHandle(info.hThread);
#else
        int input[2], output[2], errors[2];
        if (pipe(input) || pipe(output) || pipe(errors)) throw std::runtime_error("pipe failed");
        pid_ = fork();
        if (pid_ < 0) throw std::runtime_error("fork failed");
        if (pid_ == 0) {
            setpgid(0, 0);
            dup2(input[0], STDIN_FILENO);
            dup2(output[1], STDOUT_FILENO);
            dup2(errors[1], STDERR_FILENO);
            close(input[0]); close(input[1]); close(output[0]); close(output[1]);
            close(errors[0]); close(errors[1]);
            if (chdir(cwd.c_str()) != 0) _exit(127);
            std::string executable = exe.string();
            std::vector<char*> argv{executable.data()};
            std::vector<std::string> copies = args;
            for (auto& arg : copies) argv.push_back(arg.data());
            argv.push_back(nullptr);
            execv(executable.c_str(), argv.data());
            _exit(127);
        }
        setpgid(pid_, pid_);
        close(input[0]); close(output[1]); close(errors[1]);
        inWrite_ = input[1]; outRead_ = output[0]; errRead_ = errors[0];
#endif
        outThread_ = std::thread([this] { drain(outRead_, output_); });
        errThread_ = std::thread([this] { drain(errRead_, error_); });
    }
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process() {
        terminate();
        closeInput();
        if (outThread_.joinable()) outThread_.join();
        if (errThread_.joinable()) errThread_.join();
#ifdef _WIN32
        if (outRead_) CloseHandle(outRead_);
        if (errRead_) CloseHandle(errRead_);
        if (process_) CloseHandle(process_);
        if (job_) CloseHandle(job_);
#else
        if (outRead_ >= 0) close(outRead_);
        if (errRead_ >= 0) close(errRead_);
#endif
    }
    void write(const std::string& text) {
        size_t offset = 0;
        while (offset < text.size()) {
#ifdef _WIN32
            DWORD count = 0;
            check(WriteFile(inWrite_, text.data() + offset,
                            static_cast<DWORD>(std::min<size_t>(text.size() - offset, 1 << 20)),
                            &count, nullptr) && count > 0, "Writing process stdin failed");
#else
            ssize_t count = ::write(inWrite_, text.data() + offset, text.size() - offset);
            check(count > 0, "Writing process stdin failed");
#endif
            offset += static_cast<size_t>(count);
        }
    }
    void closeInput() {
#ifdef _WIN32
        if (inWrite_) { CloseHandle(inWrite_); inWrite_ = nullptr; }
#else
        if (inWrite_ >= 0) { close(inWrite_); inWrite_ = -1; }
#endif
    }
    bool running() {
#ifdef _WIN32
        return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
#else
        if (pid_ <= 0 || exited_) return false;
        int status = 0;
        const pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == 0) return true;
        if (result == pid_) { exited_ = true; status_ = status; }
        return false;
#endif
    }
    int wait(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (running()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                terminate();
                throw std::runtime_error("Child process exceeded watchdog");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        closeInput();
        if (outThread_.joinable()) outThread_.join();
        if (errThread_.joinable()) errThread_.join();
#ifdef _WIN32
        DWORD code = 0;
        check(GetExitCodeProcess(process_, &code) != 0, "GetExitCodeProcess failed");
        return static_cast<int>(code);
#else
        return WIFEXITED(status_) ? WEXITSTATUS(status_) : 128;
#endif
    }
    void terminate() {
#ifdef _WIN32
        if (running() && job_) { TerminateJobObject(job_, 1); WaitForSingleObject(process_, 2000); }
#else
        if (running()) { kill(-pid_, SIGKILL); waitpid(pid_, &status_, 0); exited_ = true; }
#endif
    }
    const std::string& output() const { return output_; }
    const std::string& error() const { return error_; }
private:
#ifdef _WIN32
    static void drain(HANDLE handle, std::string& into) {
        std::array<char, 8192> buffer{};
        DWORD count = 0;
        while (ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count) {
            if (into.size() < 8 * 1024 * 1024) into.append(buffer.data(), count);
        }
    }
    HANDLE process_ = nullptr, job_ = nullptr;
    HANDLE inWrite_ = nullptr, outRead_ = nullptr, errRead_ = nullptr;
#else
    static void drain(int handle, std::string& into) {
        std::array<char, 8192> buffer{};
        ssize_t count = 0;
        while ((count = ::read(handle, buffer.data(), buffer.size())) > 0)
            if (into.size() < 8 * 1024 * 1024) into.append(buffer.data(), static_cast<size_t>(count));
    }
    pid_t pid_ = -1;
    bool exited_ = false;
    int status_ = 0;
    int inWrite_ = -1, outRead_ = -1, errRead_ = -1;
#endif
    std::thread outThread_, errThread_;
    std::string output_, error_;
};

inline std::string run(const fs::path& exe, const std::vector<std::string>& args,
                       const fs::path& cwd, const std::string& input,
                       std::chrono::milliseconds timeout) {
    Process child(exe, args, cwd);
    child.write(input);
    child.closeInput();
    const int code = child.wait(timeout);
    check(code == 0, "Child exit " + std::to_string(code) + ": " + child.error());
    return child.output();
}
} // namespace native_test
