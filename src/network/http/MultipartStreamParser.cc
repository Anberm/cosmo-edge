#include "network/http/MultipartStreamParser.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include "event2/buffer.h"

namespace cosmo::network::http {
namespace {

    constexpr mode_t kPrivateFileMode           = 0600;
    constexpr size_t kMaxClosingBoundaryPadding = 128;

    struct BoundaryMatch {
        size_t position{0};
        size_t suffix_bytes{0};
        bool is_closing{false};
    };

    std::string_view Trim(std::string_view value) {
        const auto begin = value.find_first_not_of(" \t");
        if (begin == std::string_view::npos) {
            return {};
        }
        const auto end = value.find_last_not_of(" \t");
        return value.substr(begin, end - begin + 1);
    }

    std::string ToLower(std::string_view value) {
        std::string output;
        output.reserve(value.size());
        std::transform(value.begin(), value.end(), std::back_inserter(output),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return output;
    }

    bool IsReservedField(std::string_view name) {
        const auto lower = ToLower(name);
        return lower == "filepath" || lower == "filename" || lower == "contentlength";
    }

    bool NormalizeFilename(const std::string& input, std::string& output) {
        output.clear();
        if (input.empty() || input.size() > 1024 || input.find('\0') != std::string::npos) {
            return false;
        }
        const auto separator = input.find_last_of("/\\");
        output               = input.substr(separator == std::string::npos ? 0 : separator + 1);
        if (output.empty() || output == "." || output == ".." || output.size() > 255) {
            return false;
        }
        return std::none_of(output.begin(), output.end(),
                            [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    }

    bool WriteAll(int fd, const char* data, size_t size) {
        size_t written = 0;
        while (written < size) {
            auto result = write(fd, data + written, size - written);
            if (result < 0 && errno == EINTR) {
                continue;
            }
            if (result <= 0) {
                return false;
            }
            written += static_cast<size_t>(result);
        }
        return true;
    }

    bool CloseChecked(int& fd, bool sync) {
        if (fd < 0) {
            return true;
        }
        bool ok = !sync || fsync(fd) == 0;
        if (close(fd) != 0 && errno != EINTR) {
            ok = false;
        }
        fd = -1;
        return ok;
    }

    struct ContentDisposition {
        std::string name;
        std::string filename;
        bool has_filename{false};
    };

    std::optional<ContentDisposition> ParseContentDisposition(const std::string& headers) {
        std::string disposition_line;
        size_t begin = 0;
        while (begin <= headers.size()) {
            auto end = headers.find("\r\n", begin);
            if (end == std::string::npos) {
                end = headers.size();
            }
            std::string_view line(headers.data() + begin, end - begin);
            const auto colon = line.find(':');
            if (colon != std::string_view::npos &&
                ToLower(Trim(line.substr(0, colon))) == "content-disposition") {
                if (!disposition_line.empty()) {
                    return std::nullopt;
                }
                disposition_line = std::string(Trim(line.substr(colon + 1)));
            }
            if (end == headers.size()) {
                break;
            }
            begin = end + 2;
        }
        if (disposition_line.empty()) {
            return std::nullopt;
        }

        ContentDisposition result;
        bool first = true;
        begin      = 0;
        while (begin <= disposition_line.size()) {
            auto end = disposition_line.find(';', begin);
            if (end == std::string::npos) {
                end = disposition_line.size();
            }
            auto segment = Trim(std::string_view(disposition_line).substr(begin, end - begin));
            if (first) {
                if (ToLower(segment) != "form-data") {
                    return std::nullopt;
                }
                first = false;
            } else if (!segment.empty()) {
                const auto equals = segment.find('=');
                if (equals == std::string_view::npos) {
                    return std::nullopt;
                }
                const auto key   = ToLower(Trim(segment.substr(0, equals)));
                const auto value = Trim(segment.substr(equals + 1));
                if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
                    return std::nullopt;
                }
                const auto decoded = std::string(value.substr(1, value.size() - 2));
                if (decoded.find('"') != std::string::npos) {
                    return std::nullopt;
                }
                if (key == "name") {
                    result.name = decoded;
                } else if (key == "filename") {
                    result.filename     = decoded;
                    result.has_filename = true;
                }
            }
            if (end == disposition_line.size()) {
                break;
            }
            begin = end + 1;
        }
        if (result.name.empty() || result.name.size() > 128 ||
            std::any_of(result.name.begin(), result.name.end(),
                        [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
            return std::nullopt;
        }
        return result;
    }

}  // namespace

MultipartStreamParser::MultipartStreamParser(std::string boundary, MultipartLimits limits)
    : boundary_(std::move(boundary)), limits_(limits) {
    boundary_line_ = "--" + boundary_;
    end_boundary_  = boundary_line_ + "--";
    marker_        = "\r\n" + boundary_line_;
}

MultipartParseResult MultipartStreamParser::ParseToFile(struct evbuffer* input_buf,
                                                        const std::function<std::string()>& makeFilePath) {
    MultipartParseResult out;
    if (input_buf == nullptr) {
        out.error = MultipartParseError::kMalformed;
        out.err   = "input buffer is null";
        return out;
    }
    if (boundary_.empty() || boundary_.size() > limits_.max_boundary_bytes || limits_.max_header_bytes == 0 ||
        limits_.max_field_bytes == 0 || limits_.max_file_bytes == 0 || limits_.max_parts == 0 ||
        !makeFilePath) {
        out.error = MultipartParseError::kMalformed;
        out.err   = "invalid multipart configuration";
        return out;
    }

    enum class State { kFirstBoundary, kHeaders, kData, kDone, kError };
    State state = State::kFirstBoundary;

    std::string current_name;
    std::string current_filename;
    bool current_is_file{false};
    std::string field_buffer;
    int file_fd{-1};
    size_t file_bytes{0};
    size_t part_count{0};
    bool file_seen{false};
    std::string uploaded_path;
    std::string uploaded_name;
    size_t uploaded_bytes{0};
    std::set<std::string> field_names;

    std::string carry;
    carry.reserve(64 * 1024 + marker_.size() + 2);

    auto fail = [&](std::string message, MultipartParseError error = MultipartParseError::kMalformed) {
        out.error = error;
        out.err   = std::move(message);
        state     = State::kError;
    };

    auto ensure_file_open = [&]() -> bool {
        if (file_fd >= 0) {
            return true;
        }
        try {
            uploaded_path = makeFilePath();
        } catch (...) {
            return false;
        }
        if (uploaded_path.empty()) {
            return false;
        }
        file_fd = open(uploaded_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                       kPrivateFileMode);
        return file_fd >= 0;
    };

    MultipartParseError append_error = MultipartParseError::kNone;
    auto append_data                 = [&](const char* data, size_t size) -> bool {
        append_error = MultipartParseError::kNone;
        if (current_is_file) {
            if (size > limits_.max_file_bytes - file_bytes) {
                append_error = MultipartParseError::kPayloadTooLarge;
                return false;
            }
            if (!ensure_file_open() || !WriteAll(file_fd, data, size)) {
                append_error = MultipartParseError::kIo;
                return false;
            }
            file_bytes += size;
            return true;
        }
        if (size > limits_.max_field_bytes - field_buffer.size()) {
            append_error = MultipartParseError::kPayloadTooLarge;
            return false;
        }
        field_buffer.append(data, size);
        return true;
    };

    auto find_valid_marker = [&](bool input_exhausted) -> std::optional<BoundaryMatch> {
        size_t search_from = 0;
        while (true) {
            const auto position = carry.find(marker_, search_from);
            if (position == std::string::npos) {
                return std::nullopt;
            }
            const auto suffix = position + marker_.size();
            if (carry.size() < suffix + 2) {
                return std::nullopt;
            }
            if (carry.compare(suffix, 2, "\r\n") == 0) {
                return BoundaryMatch{position, 2, false};
            }
            if (carry.compare(suffix, 2, "--") == 0) {
                auto line_end = suffix + 2;
                while (line_end < carry.size() && (carry[line_end] == ' ' || carry[line_end] == '\t') &&
                       line_end - (suffix + 2) < kMaxClosingBoundaryPadding) {
                    ++line_end;
                }
                if (line_end - (suffix + 2) == kMaxClosingBoundaryPadding && line_end < carry.size() &&
                    (carry[line_end] == ' ' || carry[line_end] == '\t')) {
                    search_from = position + 1;
                    continue;
                }
                if (line_end == carry.size()) {
                    if (input_exhausted) {
                        return BoundaryMatch{position, line_end - suffix, true};
                    }
                    return std::nullopt;
                }
                if (carry.size() - line_end == 1 && carry[line_end] == '\r' && !input_exhausted) {
                    return std::nullopt;
                }
                if (carry.size() - line_end >= 2 && carry.compare(line_end, 2, "\r\n") == 0) {
                    return BoundaryMatch{position, line_end + 2 - suffix, true};
                }
            }
            search_from = position + 1;
        }
    };

    while (state != State::kDone && state != State::kError) {
        std::array<char, 64 * 1024> input{};
        const auto read_size = evbuffer_remove(input_buf, input.data(), input.size());
        if (read_size <= 0) {
            fail("unexpected end of multipart body");
            break;
        }
        carry.append(input.data(), read_size);
        const bool input_exhausted = evbuffer_get_length(input_buf) == 0;

        bool progressed = true;
        while (progressed && state != State::kDone && state != State::kError) {
            progressed = false;
            switch (state) {
                case State::kFirstBoundary: {
                    if (carry.size() < boundary_line_.size() + 2) {
                        break;
                    }
                    if (carry.compare(0, boundary_line_.size(), boundary_line_) != 0 ||
                        carry.compare(boundary_line_.size(), 2, "\r\n") != 0) {
                        fail("invalid first multipart boundary");
                        break;
                    }
                    carry.erase(0, boundary_line_.size() + 2);
                    state      = State::kHeaders;
                    progressed = true;
                    break;
                }
                case State::kHeaders: {
                    const auto header_end = carry.find("\r\n\r\n");
                    if (header_end == std::string::npos) {
                        if (carry.size() > limits_.max_header_bytes) {
                            fail("multipart headers too large", MultipartParseError::kPayloadTooLarge);
                        }
                        break;
                    }
                    if (header_end > limits_.max_header_bytes || ++part_count > limits_.max_parts) {
                        fail(header_end > limits_.max_header_bytes ? "multipart headers too large"
                                                                   : "too many multipart parts",
                             MultipartParseError::kPayloadTooLarge);
                        break;
                    }
                    auto disposition = ParseContentDisposition(carry.substr(0, header_end));
                    if (!disposition) {
                        fail("invalid multipart content disposition");
                        break;
                    }
                    current_name    = disposition->name;
                    current_is_file = disposition->has_filename;
                    current_filename.clear();
                    field_buffer.clear();
                    file_bytes = 0;
                    if (current_is_file) {
                        if (file_seen || !NormalizeFilename(disposition->filename, current_filename)) {
                            fail(file_seen ? "multiple multipart files are not allowed"
                                           : "invalid multipart filename");
                            break;
                        }
                        file_seen     = true;
                        uploaded_name = current_filename;
                    } else {
                        const auto lower_name = ToLower(current_name);
                        if (IsReservedField(current_name) || field_names.count(lower_name) != 0) {
                            fail(IsReservedField(current_name) ? "reserved multipart field"
                                                               : "duplicate multipart field");
                            break;
                        }
                        field_names.insert(lower_name);
                    }
                    carry.erase(0, header_end + 4);
                    state      = State::kData;
                    progressed = true;
                    break;
                }
                case State::kData: {
                    const auto marker_match = find_valid_marker(input_exhausted);
                    if (!marker_match) {
                        const auto keep =
                            std::min(carry.size(), marker_.size() + kMaxClosingBoundaryPadding + 4);
                        const auto flush_size = carry.size() - keep;
                        if (flush_size == 0) {
                            break;
                        }
                        if (!append_data(carry.data(), flush_size)) {
                            fail(current_is_file ? "multipart file exceeds limit or cannot be written"
                                                 : "multipart field exceeds limit",
                                 append_error);
                            break;
                        }
                        carry.erase(0, flush_size);
                        progressed = true;
                        break;
                    }

                    if (!append_data(carry.data(), marker_match->position)) {
                        fail(current_is_file ? "multipart file exceeds limit or cannot be written"
                                             : "multipart field exceeds limit",
                             append_error);
                        break;
                    }
                    if (current_is_file) {
                        if (!ensure_file_open() || !CloseChecked(file_fd, true)) {
                            fail("cannot finalize multipart file", MultipartParseError::kIo);
                            break;
                        }
                        uploaded_bytes = file_bytes;
                    } else {
                        out.fields.emplace(current_name, std::move(field_buffer));
                    }

                    carry.erase(0, marker_match->position + marker_.size());
                    if (marker_match->is_closing) {
                        carry.erase(0, marker_match->suffix_bytes);
                        if (!carry.empty() || evbuffer_get_length(input_buf) != 0) {
                            fail("unexpected data after closing multipart boundary");
                            break;
                        }
                        state      = State::kDone;
                        progressed = true;
                        break;
                    }
                    if (marker_match->suffix_bytes != 2 || carry.compare(0, 2, "\r\n") != 0) {
                        fail("invalid multipart boundary suffix");
                        break;
                    }
                    carry.erase(0, 2);
                    state      = State::kHeaders;
                    progressed = true;
                    break;
                }
                case State::kDone:
                case State::kError:
                    break;
            }
        }
    }

    if (state == State::kDone && file_seen && file_fd < 0) {
        out.fields["filePath"]      = uploaded_path;
        out.fields["fileName"]      = uploaded_name;
        out.fields["contentLength"] = std::to_string(uploaded_bytes);
        out.uploaded_path           = uploaded_path;
        out.uploaded_name           = uploaded_name;
        out.uploaded_size           = uploaded_bytes;
        out.ok                      = true;
        out.error                   = MultipartParseError::kNone;
        return out;
    }
    CloseChecked(file_fd, false);
    if (out.err.empty()) {
        out.error = MultipartParseError::kMalformed;
        out.err   = file_seen ? "multipart parse failed" : "multipart file is missing";
    }
    return out;
}

}  // namespace cosmo::network::http
