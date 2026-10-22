/*
 * fuzz_watchdog.h -- a portable "the worker must finish" guard.
 *
 * WHY A CHILD PROCESS.  The reader's failure contract is "throw mpp::Error, never
 * crash, never hang".  A tight loop or a stack overflow cannot be interrupted
 * from inside the same thread portably: there is no in-process way to abort a
 * C++ function that never returns.  So each fuzzer main process is a thin
 * supervisor: it re-executes itself with `--worker` plus the same arguments and
 * waits with a timeout.  The worker rewrites a progress file before every case,
 * so a timeout or a crash can be attributed to a concrete seed.
 *
 * WHY NOT alarm()/SIGALRM.  alarm() is POSIX-only and the project's CI includes
 * MSVC; the Windows branch here uses WaitForSingleObject + TerminateProcess,
 * which is the same idea with the platform's own API.  Both branches are
 * compiled by their own platform only, and the surrounding fuzz logic is shared.
 *
 * The watchdog is a test harness, not product code.  It never touches the .mpp
 * inputs; the worker writes only under the fuzz build directory.
 */

#ifndef W57_FUZZ_WATCHDOG_H
#define W57_FUZZ_WATCHDOG_H

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <cstdlib>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fuzz {

/* True when argv contains `--worker`; the supervisor strips it when respawning. */
inline bool invoked_as_worker(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--worker") {
            return true;
        }
    }
    return false;
}

/*
 * argv without `--worker`, plus a new `--worker` at the end: the command that
 * re-executes this binary in worker mode.  Adding the flag is what stops the
 * child from spawning a child of its own.
 */
inline std::vector<std::string> worker_command(int argc, char** argv) {
    std::vector<std::string> command;
    command.reserve(static_cast<std::size_t>(argc) + 1);
    for (int i = 0; i < argc; ++i) {
        std::string const argument = argv[i];
        if (argument == "--worker") {
            continue;
        }
        command.push_back(argument);
    }
    if (command.empty()) {
        command.push_back("fuzz_worker");
    }
    command.push_back("--worker");
    return command;
}

#if defined(_WIN32)

namespace detail {

inline std::wstring widen(std::string const& text) {
    if (text.empty()) {
        return {};
    }
    int const needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        wide.data(), needed);
    return wide;
}

inline std::wstring quote(std::wstring const& text) {
    std::wstring out = L"\"";
    for (wchar_t const ch : text) {
        if (ch == L'"') {
            out += L'\\';
        }
        out += ch;
    }
    out += L'"';
    return out;
}

inline std::string hex_digits(unsigned long value) {
    static char const* table = "0123456789abcdef";
    std::string digits(8, '0');
    for (unsigned i = 0; i < 8; ++i) {
        digits[7 - i] = table[(value >> (4 * i)) & 0xFu];
    }
    return "0x" + digits;
}

inline std::string exit_code_text(unsigned long code) {
    return std::to_string(code) + " (" + hex_digits(code) + ")";
}

}  // namespace detail

/*
 * Runs `command` as a child and waits up to `timeout_ms`.
 * Returns 0 when the child exited 0, -1 on timeout, -2 on a spawn error and
 * 1 for any other non-zero exit (including a crash code).
 */
inline int run_watched(std::vector<std::string> const& command,
                       unsigned timeout_ms,
                       std::string& message) {
    std::wstring command_line;
    for (std::size_t i = 0; i < command.size(); ++i) {
        if (i != 0) {
            command_line += L' ';
        }
        command_line += detail::quote(detail::widen(command[i]));
    }
    std::vector<wchar_t> buffer(command_line.begin(), command_line.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, 0, nullptr,
                       nullptr, &startup, &process) == FALSE) {
        message = "watchdog: CreateProcessW failed, GetLastError " +
                  std::to_string(static_cast<unsigned long>(GetLastError()));
        return -2;
    }
    DWORD const wait = WaitForSingleObject(process.hProcess,
                                           static_cast<DWORD>(timeout_ms));
    if (wait == WAIT_TIMEOUT) {
        (void)TerminateProcess(process.hProcess, 1);
        (void)WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        message = "watchdog: worker exceeded " + std::to_string(timeout_ms) + " ms";
        return -1;
    }
    if (wait != WAIT_OBJECT_0) {
        (void)TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        message = "watchdog: WaitForSingleObject failed";
        return -2;
    }
    DWORD exit_code = 1;
    (void)GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_code == 0) {
        message = "worker exited 0";
        return 0;
    }
    message = "watchdog: worker died with exit code " + detail::exit_code_text(exit_code);
    return 1;
}

#else  /* POSIX */

/*
 * Runs `command` as a child and waits up to `timeout_ms`.
 * The executable is resolved from argv[0]; ctest and the supervisor both pass an
 * absolute path, and a bare name is resolved against the current directory
 * rather than PATH (execv semantics, so a missing path is a loud error instead
 * of an accidental system binary).
 */
inline int run_watched(std::vector<std::string> const& command,
                       unsigned timeout_ms,
                       std::string& message) {
    if (command.empty()) {
        message = "watchdog: empty command";
        return -2;
    }
    std::filesystem::path executable(command.front());
    std::error_code error;
    if (!executable.is_absolute()) {
        executable = std::filesystem::absolute(executable, error);
    }
    if (!std::filesystem::exists(executable, error)) {
        message = "watchdog: cannot find " + executable.string();
        return -2;
    }

    std::vector<char*> arguments;
    arguments.reserve(command.size() + 1);
    for (std::string const& part : command) {
        arguments.push_back(const_cast<char*>(part.c_str()));
    }
    arguments.push_back(nullptr);

    pid_t const child = fork();
    if (child < 0) {
        message = "watchdog: fork failed";
        return -2;
    }
    if (child == 0) {
        execv(executable.string().c_str(), arguments.data());
        _exit(127);
    }
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int status = 0;
    for (;;) {
        pid_t const result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            break;
        }
        if (result < 0) {
            message = "watchdog: waitpid failed";
            return -2;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            (void)kill(child, SIGKILL);
            (void)waitpid(child, &status, 0);
            message = "watchdog: worker exceeded " + std::to_string(timeout_ms) + " ms";
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (WIFEXITED(status)) {
        int const code = WEXITSTATUS(status);
        if (code == 0) {
            message = "worker exited 0";
            return 0;
        }
        message = "watchdog: worker died with exit code " + std::to_string(code);
        return 1;
    }
    if (WIFSIGNALED(status)) {
        message = "watchdog: worker killed by signal " +
                  std::to_string(WTERMSIG(status));
        return 1;
    }
    message = "watchdog: worker ended in an unknown state";
    return 1;
}

#endif  /* _WIN32 */

}  // namespace fuzz

#endif /* W57_FUZZ_WATCHDOG_H */
