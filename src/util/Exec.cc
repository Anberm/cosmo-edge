// Process execution utilities.

#include "util/Exec.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

#include "fmt/format.h"

namespace cosmo::util {

constexpr int kExecFailureCode = 0x7F;

std::string ShellEscape(const std::string& path) {
    std::string escaped;
    escaped.reserve(path.size() + 2);
    escaped.push_back('\'');
    for (char c : path) {
        if (c == '\'') {
            escaped.append("'\\''");
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

namespace {

    enum class ReadStatus {
        kComplete,
        kError,
        kOutputLimitExceeded,
    };

    // Read all bytes from `fd` until EOF, appending them to `out`. Stops on EOF or a
    // non-EINTR read error.
    void ReadAllFromFd(int fd, std::string& out) {
        char buf[1024];
        for (;;) {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<size_t>(n));
            } else if (n == 0) {
                break;  // EOF
            } else if (errno != EINTR) {
                break;  // unrecoverable read error
            }
        }
    }

    ReadStatus ReadAllFromFdWithLimit(int fd, std::string& out, size_t max_output_bytes) {
        const size_t initial_size = out.size();
        char buf[1024];
        for (;;) {
            const ssize_t read_size = read(fd, buf, sizeof(buf));
            if (read_size > 0) {
                const size_t appended_size  = out.size() - initial_size;
                const size_t remaining_size = max_output_bytes - appended_size;
                const size_t received_size  = static_cast<size_t>(read_size);
                if (received_size > remaining_size) {
                    out.append(buf, remaining_size);
                    return ReadStatus::kOutputLimitExceeded;
                }
                out.append(buf, received_size);
            } else if (read_size == 0) {
                return ReadStatus::kComplete;
            } else if (errno != EINTR) {
                return ReadStatus::kError;
            }
        }
    }

    bool TerminateChild(pid_t pid) {
        for (;;) {
            if (kill(pid, SIGKILL) == 0 || errno == ESRCH) {
                return true;
            }
            if (errno != EINTR) {
                return false;
            }
        }
    }

    // Split `text` into lines and append them to `out`, mirroring the original
    // fgets-per-line behaviour: each entry includes its trailing '\n' unless
    // `remove_newline` is true. A trailing partial line (no '\n') is included as-is.
    void AppendLines(const std::string& text, std::vector<std::string>& out, bool remove_newline) {
        size_t start = 0;
        while (start < text.size()) {
            size_t nl = text.find('\n', start);
            if (nl == std::string::npos) {
                out.emplace_back(text, start);
                break;
            }
            std::string line(text, start, nl - start + 1);  // keep the '\n'
            if (remove_newline) {
                line.pop_back();
            }
            out.push_back(std::move(line));
            start = nl + 1;
        }
    }

    // Spawn argv[0] with execvp() (no shell), redirecting the child's stdout+stderr
    // into a pipe read by the parent. Appends captured output to `out`. The char*
    // argv array is built up front so the child only performs async-signal-safe
    // calls between fork() and execvp() (this process is multithreaded).
    int ExecArgv(const std::vector<std::string>& argv, std::string& out,
                 const size_t* max_output_bytes = nullptr) {
        if (argv.empty()) {
            return kExecFailureCode;
        }

        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);

        int pipefd[2];
        if (pipe(pipefd) != 0) {
            return kExecFailureCode;
        }

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            return kExecFailureCode;
        }

        if (pid == 0) {
            // Child — async-signal-safe calls only until execvp().
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) {
                _exit(kExecFailureCode);
            }
            close(pipefd[1]);
            // SIGPIPE is set to SIG_IGN by the parent to protect against
            // broken-pipe writes to dead WebSocket clients.  SIG_IGN
            // survives execve() per POSIX, so reset it here so child
            // processes (and their shell pipelines) see the default
            // disposition where a broken pipe terminates the writer.
            signal(SIGPIPE, SIG_DFL);
            execvp(c_argv[0], c_argv.data());
            _exit(kExecFailureCode);  // only reached if execvp() failed
        }

        // Parent — drain the pipe before reaping to avoid deadlock when the child's
        // output exceeds the pipe buffer.
        close(pipefd[1]);
        ReadStatus read_status = ReadStatus::kComplete;
        if (max_output_bytes == nullptr) {
            ReadAllFromFd(pipefd[0], out);
        } else {
            read_status = ReadAllFromFdWithLimit(pipefd[0], out, *max_output_bytes);
        }
        if (read_status != ReadStatus::kComplete) {
            if (!TerminateChild(pid)) {
                read_status = ReadStatus::kError;
            }
        }
        close(pipefd[0]);

        int status = 0;
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                return kExecFailureCode;
            }
        }
        if (read_status != ReadStatus::kComplete) {
            return kExecFailureCode;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return kExecFailureCode;
    }

}  // namespace

int Exec(const std::vector<std::string>& argv, std::string& out) {
    return ExecArgv(argv, out);
}

int ExecWithOutputLimit(const std::vector<std::string>& argv, std::string& out, size_t max_output_bytes) {
    return ExecArgv(argv, out, &max_output_bytes);
}

int Exec(const std::vector<std::string>& argv, std::vector<std::string>& out, bool remove_newline) {
    std::string text;
    int rc = ExecArgv(argv, text);
    AppendLines(text, out, remove_newline);
    return rc;
}

int ExecShell(const std::string& cmd, std::string& out) {
    FILE* file = popen(fmt::format("{} 2>&1", cmd).c_str(), "r");
    if (!file) {
        return kExecFailureCode;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), file)) {
        out.append(buf);
    }
    int ret = pclose(file);
    if (WIFEXITED(ret)) {
        return WEXITSTATUS(ret);
    }
    return kExecFailureCode;
}

int ExecShell(const std::string& cmd, std::vector<std::string>& out, bool remove_newline) {
    FILE* file = popen(fmt::format("{} 2>&1", cmd).c_str(), "r");
    if (!file) {
        return kExecFailureCode;
    }
    char buf[1024]{};
    while (fgets(buf, sizeof(buf), file)) {
        size_t len = std::strlen(buf);
        if (remove_newline && len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        out.emplace_back(buf);
    }
    int ret = pclose(file);
    if (WIFEXITED(ret)) {
        return WEXITSTATUS(ret);
    }
    return kExecFailureCode;
}

bool IsHostnameSafe(const std::string& host) {
    if (host.empty() || host.size() > 253) {
        return false;
    }
    for (char c : host) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != ':') {
            return false;
        }
    }
    return true;
}

std::string PingResolveIp(const std::string& host) {
    if (host.empty() || !IsHostnameSafe(host)) {
        return "";
    }
    // Already an IP address — no resolution needed
    if (host.find_first_not_of("0123456789.") == std::string::npos) {
        return host;
    }
    auto escaped = ShellEscape(host);
    // Ping once with 1s timeout, extract IP from first response line.
    // Pipeline: ping → kill stale process → head first line → awk column 3 → grep IPv4 pattern
    const std::string cmd = "(ping " + escaped +
                            R"( -w 1 -c 1 & 2>/dev/null;)"
                            R"(epid=$!;proc=`ps -ef | grep $epid | grep -v "grep")"
                            R"(| grep -v "\[.*\]"`;)"
                            R"(if [[ "$epid" && "$proc" ]];then sleep 10;kill -9 $epid 2>/dev/null;fi;))"
                            R"(|  head -n 1 | awk '{print $3}' | grep -oE "([0-9]{1,3}[.]){3}[0-9]{1,3}")";

    std::vector<std::string> result;
    int ret = ExecShell(cmd, result, true);
    if (ret != 0) {
        return "";
    }
    return result.empty() ? "" : result[0];
}

std::string PingLatency(const std::string& host) {
    if (host.empty() || !IsHostnameSafe(host)) {
        return "";
    }
    auto escaped = ShellEscape(host);
    // Ping once with 1s timeout, extract round-trip time from response.
    // Pipeline: ping → kill stale process → head 4 lines → strip spaces → grep time=N.N → awk extract value
    const std::string cmd =
        "(ping " + escaped +
        R"( -w 1 -c 1 & 2>/dev/null;)"
        R"(epid=$!;proc=`ps -ef | grep $epid | grep -v "grep")"
        R"( | grep -v "\[.*\]"`;)"
        R"(if [[ "$epid" && "$proc" ]];then sleep 10;kill -9 $epid 2>/dev/null;fi;))"
        R"( |  head -n 4 | sed s/[[:space:]]//g | grep -oE "time=[0-9]{1,3}.[0-9]{1,3}" | awk -F= '{print $2}')";

    std::string result;
    int ret = ExecShell(cmd, result);
    if (ret != 0) {
        return "";
    }
    // Trim trailing whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    return result;
}

}  // namespace cosmo::util
