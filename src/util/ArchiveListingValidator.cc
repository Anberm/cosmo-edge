#include "util/ArchiveListingValidator.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "util/Exec.h"
#include "util/PathUtil.h"

namespace cosmo::util {

namespace {

    constexpr size_t kMaxArchiveMemberBytes     = 512;
    constexpr size_t kListingFixedOverheadBytes = 16 * 1024;

    bool IsAtEnd(std::istringstream& stream) {
        stream >> std::ws;
        return stream.eof();
    }

    bool HasOnlyExpectedControls(std::string_view listing) {
        return std::none_of(listing.begin(), listing.end(),
                            [](const unsigned char ch) { return (ch < 0x20 && ch != '\n') || ch == 0x7f; });
    }

    bool IsAbsoluteArchivePath(std::string_view member) {
        if (member.empty()) {
            return false;
        }
        if (member.front() == '/') {
            return true;
        }
        return member.size() >= 3 && std::isalpha(static_cast<unsigned char>(member[0])) != 0 &&
               member[1] == ':' && member[2] == '/';
    }

    bool NormalizeArchiveMember(std::string member, bool is_directory, std::string& normalized) {
        normalized.clear();
        if (member.empty() || member.size() > kMaxArchiveMemberBytes || IsAbsoluteArchivePath(member) ||
            member.find('\\') != std::string::npos) {
            return false;
        }
        if (std::any_of(member.begin(), member.end(),
                        [](const unsigned char ch) { return ch < 0x20 || ch == 0x7f; })) {
            return false;
        }

        while (member.size() >= 2 && member.compare(0, 2, "./") == 0) {
            member.erase(0, 2);
        }
        if (IsAbsoluteArchivePath(member)) {
            return false;
        }

        if (is_directory) {
            while (!member.empty() && member.back() == '/') {
                member.pop_back();
            }
        } else if (!member.empty() && member.back() == '/') {
            return false;
        }

        if (member.empty() || member == ".") {
            if (!is_directory) {
                return false;
            }
            normalized = ".";
            return true;
        }

        std::istringstream stream(member);
        std::string component;
        while (std::getline(stream, component, '/')) {
            if (!cosmo::path::IsSafePathComponent(component)) {
                return false;
            }
        }
        normalized = std::move(member);
        return true;
    }

    bool ParseZipCountLine(const std::string& line, size_t& entry_count) {
        std::istringstream stream(line);
        std::string zip;
        std::string file;
        std::string size_label;
        std::string bytes_label;
        std::string number;
        std::string of;
        std::string entries_label;
        std::uintmax_t archive_size = 0;
        return (stream >> zip >> file >> size_label >> archive_size >> bytes_label >> number >> of >>
                entries_label >> entry_count) &&
               zip == "Zip" && file == "file" && size_label == "size:" && bytes_label == "bytes," &&
               number == "number" && of == "of" && entries_label == "entries:" && IsAtEnd(stream);
    }

    bool IsPercentToken(std::string_view token) {
        if (token.size() < 2 || token.back() != '%') {
            return false;
        }
        token.remove_suffix(1);
        bool has_digit = false;
        bool has_dot   = false;
        for (size_t index = 0; index < token.size(); ++index) {
            const char ch = token[index];
            if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                has_digit = true;
            } else if (ch == '.' && !has_dot) {
                has_dot = true;
            } else if (ch == '-' && index == 0) {
                continue;
            } else {
                return false;
            }
        }
        return has_digit;
    }

    bool ParseZipFooter(const std::string& line, size_t& entry_count, std::uintmax_t& uncompressed_bytes) {
        std::istringstream stream(line);
        std::string file_label;
        std::string first_bytes_label;
        std::string uncompressed_label;
        std::uintmax_t compressed_bytes = 0;
        std::string second_bytes_label;
        std::string compressed_label;
        std::string percentage;
        return (stream >> entry_count >> file_label >> uncompressed_bytes >> first_bytes_label >>
                uncompressed_label >> compressed_bytes >> second_bytes_label >> compressed_label >>
                percentage) &&
               (file_label == "file," || file_label == "files,") && first_bytes_label == "bytes" &&
               uncompressed_label == "uncompressed," && second_bytes_label == "bytes" &&
               compressed_label == "compressed:" && IsPercentToken(percentage) && IsAtEnd(stream);
    }

    bool ParseArchiveEntry(const std::string& line, ArchiveListingFormat format, std::uintmax_t& entry_size,
                           bool& is_directory, std::string& member) {
        if (line.empty() || (line.front() != '-' && line.front() != 'd')) {
            return false;
        }

        std::istringstream stream(line);
        std::string permissions;
        if (!(stream >> permissions) || permissions.empty() || permissions.front() != line.front()) {
            return false;
        }
        is_directory = permissions.front() == 'd';

        if (format == ArchiveListingFormat::kZipVerbose) {
            std::string version;
            std::string system;
            std::string text_mode;
            std::uintmax_t compressed_size = 0;
            std::string method;
            std::string date;
            std::string time;
            if (!(stream >> version >> system >> entry_size >> text_mode >> compressed_size >> method >>
                  date >> time)) {
                return false;
            }
        } else {
            std::string owner;
            std::string date;
            std::string time;
            if (!(stream >> owner >> entry_size >> date >> time)) {
                return false;
            }
        }

        std::string remainder;
        std::getline(stream, remainder);
        if (remainder.size() < 2 || remainder.front() != ' ' || remainder[1] == ' ') {
            return false;
        }
        member = remainder.substr(1);
        return true;
    }

    bool CalculateListingByteLimit(size_t max_entries, size_t& byte_limit) {
        constexpr size_t kBytesPerEntry = 1024;
        if (max_entries >
            (std::numeric_limits<size_t>::max() - kListingFixedOverheadBytes) / kBytesPerEntry) {
            return false;
        }
        byte_limit = max_entries * kBytesPerEntry + kListingFixedOverheadBytes;
        return true;
    }

}  // namespace

bool ValidateArchiveListingOutput(std::string_view listing, ArchiveListingFormat format,
                                  const ArchiveListingLimits& limits) {
    size_t listing_byte_limit = 0;
    if (listing.empty() || limits.max_entries == 0 || limits.max_file_bytes == 0 ||
        limits.max_total_bytes == 0 || !CalculateListingByteLimit(limits.max_entries, listing_byte_limit) ||
        listing.size() > listing_byte_limit || !HasOnlyExpectedControls(listing)) {
        return false;
    }

    enum class ZipPhase {
        kHeader,
        kCount,
        kEntries,
        kDone,
    };
    ZipPhase zip_phase          = ZipPhase::kHeader;
    size_t declared_entry_count = 0;
    size_t entry_count          = 0;
    std::uintmax_t total_size   = 0;
    std::unordered_set<std::string> members;

    std::istringstream lines{std::string(listing)};
    std::string line;
    while (std::getline(lines, line)) {
        if (format == ArchiveListingFormat::kZipVerbose && zip_phase == ZipPhase::kHeader) {
            if (line.compare(0, 8, "Archive:") != 0 || line.size() <= 9 || line[8] != ' ' ||
                line.find_first_not_of(' ', 8) == std::string::npos) {
                return false;
            }
            zip_phase = ZipPhase::kCount;
            continue;
        }
        if (format == ArchiveListingFormat::kZipVerbose && zip_phase == ZipPhase::kCount) {
            if (!ParseZipCountLine(line, declared_entry_count) || declared_entry_count > limits.max_entries) {
                return false;
            }
            zip_phase = ZipPhase::kEntries;
            continue;
        }
        if (format == ArchiveListingFormat::kZipVerbose && zip_phase == ZipPhase::kDone) {
            return false;
        }

        if (format == ArchiveListingFormat::kZipVerbose && zip_phase == ZipPhase::kEntries && !line.empty() &&
            std::isdigit(static_cast<unsigned char>(line.front())) != 0) {
            size_t footer_entry_count        = 0;
            std::uintmax_t footer_total_size = 0;
            if (!ParseZipFooter(line, footer_entry_count, footer_total_size) ||
                footer_entry_count != entry_count || footer_total_size != total_size) {
                return false;
            }
            zip_phase = ZipPhase::kDone;
            continue;
        }

        std::uintmax_t entry_size = 0;
        bool is_directory         = false;
        std::string member;
        std::string normalized;
        if (!ParseArchiveEntry(line, format, entry_size, is_directory, member) ||
            ++entry_count > limits.max_entries || entry_size > limits.max_file_bytes ||
            total_size > limits.max_total_bytes || entry_size > limits.max_total_bytes - total_size ||
            !NormalizeArchiveMember(std::move(member), is_directory, normalized) ||
            !members.insert(normalized).second) {
            return false;
        }
        total_size += entry_size;
    }

    if (entry_count == 0) {
        return false;
    }
    return format == ArchiveListingFormat::kTarVerbose ||
           (zip_phase == ZipPhase::kDone && entry_count == declared_entry_count);
}

bool ValidateArchiveListingFile(const std::string& archive_path, ArchiveListingFormat format,
                                const ArchiveListingLimits& limits) {
    size_t listing_byte_limit = 0;
    if (limits.max_entries == 0 || !CalculateListingByteLimit(limits.max_entries, listing_byte_limit)) {
        return false;
    }
    std::string listing;
    const std::vector<std::string> argv =
        format == ArchiveListingFormat::kZipVerbose
            ? std::vector<std::string>{"unzip", "-Z", "-l", archive_path}
            : std::vector<std::string>{"tar", "-tzvf", archive_path, "--quoting-style=escape"};
    return cosmo::util::ExecWithOutputLimit(argv, listing, listing_byte_limit) == 0 &&
           ValidateArchiveListingOutput(listing, format, limits);
}

}  // namespace cosmo::util
