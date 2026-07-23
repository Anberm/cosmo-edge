// Process execution utilities.
//   Exec(argv)     — no-shell execution via fork()+execvp(): the default, safe API.
//   ExecShell(cmd) — runs `cmd` through /bin/sh; shell-injection sensitive, reserved
//                    for commands that genuinely require shell syntax (pipes, &&,
//                    redirects, command lists, ...).

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cosmo::util {

// Execute a program WITHOUT a shell: argv[0] is the program, argv[1..] are its
// arguments (execvp semantics — the program is resolved via PATH). Combined
// stdout+stderr is appended to `out`. Returns the process exit code, or 0x7F
// (127) if argv is empty, fork()/pipe() fails, or the program cannot be exec'd.
// This is the preferred entry point: arguments are never interpreted by a shell.
int Exec(const std::vector<std::string>& argv, std::string& out);

// Execute argv while capturing at most max_output_bytes from combined
// stdout+stderr. If the child produces more output, it is terminated and
// reaped, no more than max_output_bytes are appended to out, and 0x7F is
// returned. The limit applies only to bytes appended by this invocation.
int ExecWithOutputLimit(const std::vector<std::string>& argv, std::string& out, size_t max_output_bytes);

// Same as above, but appends the captured output to `out` split into lines. If
// remove_newline is true, each line's trailing '\n' is stripped.
int Exec(const std::vector<std::string>& argv, std::vector<std::string>& out, bool remove_newline = false);

// Execute a shell command string via /bin/sh (popen), appending combined
// stdout+stderr to `out`. Returns the process exit code, or 0x7F on popen
// failure.
//
// SHELL-INJECTION SENSITIVE: `cmd` is interpreted by the shell, so any untrusted
// value interpolated into it MUST first be passed through ShellEscape(). Prefer
// the argv-based Exec() for untrusted arguments; use ExecShell() only when shell
// syntax (pipes, &&, redirects, command lists) is genuinely required.
int ExecShell(const std::string& cmd, std::string& out);

// Shell command string variant that appends captured output to `out` split into
// lines. See ExecShell(const std::string&, std::string&) for injection caveats.
int ExecShell(const std::string& cmd, std::vector<std::string>& out, bool remove_newline = false);

// Shell-escape a value by wrapping it in single quotes (for use with ExecShell()
// when interpolating untrusted data into a command string).
std::string ShellEscape(const std::string& path);

// Validate that a hostname/IP contains only safe characters [a-zA-Z0-9.:-].
bool IsHostnameSafe(const std::string& host);

// Resolve a hostname to an IPv4 address using a background ping probe.
// Returns the resolved IP string, or empty string on failure.
// The host must pass IsHostnameSafe() validation.
std::string PingResolveIp(const std::string& host);

// Probe IP accessibility by ping and extract round-trip time.
// Returns the latency string (e.g. "1.234"), or empty string if unreachable.
// The host must pass IsHostnameSafe() validation.
std::string PingLatency(const std::string& host);

}  // namespace cosmo::util
