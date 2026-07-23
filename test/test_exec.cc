// Unit tests for util/Exec — argv-based (no-shell) execution, ExecShell, ShellEscape.

#include <chrono>
#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "util/Exec.h"

using namespace cosmo::util;

TEST_CASE("Exec(argv) captures stdout and exit code", "[exec]") {
    SECTION("stdout captured, exit 0") {
        std::string out;
        int rc = Exec({"printf", "%s", "hello"}, out);
        REQUIRE(rc == 0);
        REQUIRE(out == "hello");
    }
    SECTION("non-zero exit code is propagated") {
        std::string out;
        int rc = Exec({"false"}, out);
        REQUIRE(rc == 1);
    }
}

TEST_CASE("Exec(argv) captures combined stdout+stderr", "[exec]") {
    std::string out;
    int rc = Exec({"sh", "-c", "printf %s outpart; printf %s errpart 1>&2"}, out);
    REQUIRE(rc == 0);
    REQUIRE(out.find("outpart") != std::string::npos);
    REQUIRE(out.find("errpart") != std::string::npos);
}

TEST_CASE("ExecWithOutputLimit bounds capture and terminates the child", "[exec][security]") {
    SECTION("an exact-size output succeeds and preserves existing output") {
        std::string out = "prefix:";
        const int rc    = ExecWithOutputLimit({"printf", "%s", "hello"}, out, 5);
        REQUIRE(rc == 0);
        REQUIRE(out == "prefix:hello");
    }

    SECTION("excess output terminates and reaps a child that would not exit") {
        std::string out;
        const auto start = std::chrono::steady_clock::now();
        const int rc     = ExecWithOutputLimit({"sh", "-c", "printf %s 123456; while :; do :; done"}, out, 5);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        REQUIRE(rc == 0x7F);
        REQUIRE(out == "12345");
        REQUIRE(elapsed < std::chrono::seconds(5));
    }
}

TEST_CASE("Exec(argv) does not interpret shell metacharacters", "[exec]") {
    // The argument must be emitted verbatim — no shell expansion, no command
    // separation. This is the core security property of the argv-based Exec:
    // untrusted values cannot break out of the intended argument.
    std::string out;
    int rc = Exec({"printf", "%s", "$(whoami); rm -rf / ; echo PWNED"}, out);
    REQUIRE(rc == 0);
    REQUIRE(out == "$(whoami); rm -rf / ; echo PWNED");
}

TEST_CASE("Exec(argv) reports failure for empty argv / missing program", "[exec]") {
    SECTION("empty argv returns 0x7F") {
        std::string out;
        REQUIRE(Exec(std::vector<std::string>{}, out) == 0x7F);
    }
    SECTION("missing program returns 0x7F") {
        std::string out;
        int rc = Exec({"cosmo_definitely_missing_binary_xyz"}, out);
        REQUIRE(rc == 0x7F);
    }
}

TEST_CASE("Exec(argv) vector overload splits lines", "[exec]") {
    SECTION("remove_newline strips trailing newline per line") {
        std::vector<std::string> lines;
        int rc = Exec({"printf", "a\nb\n"}, lines, true);
        REQUIRE(rc == 0);
        REQUIRE(lines.size() == 2);
        REQUIRE(lines[0] == "a");
        REQUIRE(lines[1] == "b");
    }
    SECTION("without remove_newline keeps trailing newline per line") {
        std::vector<std::string> lines;
        int rc = Exec({"printf", "a\nb\n"}, lines, false);
        REQUIRE(rc == 0);
        REQUIRE(lines.size() == 2);
        REQUIRE(lines[0] == "a\n");
        REQUIRE(lines[1] == "b\n");
    }
}

TEST_CASE("ShellEscape quotes values for ExecShell", "[exec]") {
    SECTION("simple value is single-quoted") {
        REQUIRE(ShellEscape("hello") == "'hello'");
    }
    SECTION("value with embedded single quote is escaped") {
        // 'a b'\''c'  -> when re-read by a POSIX shell, yields  a b'c
        REQUIRE(ShellEscape("a b'c") == "'a b'\\''c'");
    }
    SECTION("ExecShell + ShellEscape round-trips an untrusted value") {
        std::string out;
        const std::string val = "hello world; rm -rf /";
        int rc                = ExecShell("printf %s " + ShellEscape(val), out);
        REQUIRE(rc == 0);
        REQUIRE(out == val);
    }
}
