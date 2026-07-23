#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

struct evbuffer;

namespace cosmo::network::http {

enum class MultipartParseError {
    kNone,
    kMalformed,
    kPayloadTooLarge,
    kIo,
};

/// Result of a multipart parse operation.
struct MultipartParseResult {
    std::map<std::string, std::string> fields;
    std::string uploaded_path;
    std::string uploaded_name;
    std::uint64_t uploaded_size{0};
    bool ok{false};
    MultipartParseError error{MultipartParseError::kNone};
    std::string err;
};

struct MultipartLimits {
    size_t max_boundary_bytes{200};
    size_t max_header_bytes{16 * 1024};
    size_t max_field_bytes{64 * 1024};
    size_t max_file_bytes{8 * 1024 * 1024};
    size_t max_parts{16};
};

/// Stream parser for "single file + few fields" multipart uploads:
/// - Reads from evbuffer in chunks to avoid evbuffer_pullup(-1) large memory allocation
/// - File content is written directly to disk, not concatenated in memory
class MultipartStreamParser {
public:
    explicit MultipartStreamParser(std::string boundary, MultipartLimits limits = {});

    /// Parse multipart body from evbuffer, writing file content to disk via makeFilePath callback.
    /// @param input_buf    The libevent evbuffer containing the raw multipart body.
    /// @param makeFilePath Callback that returns a new server-generated filesystem path.
    /// @return             Parse result containing extracted fields and status.
    MultipartParseResult ParseToFile(struct evbuffer* input_buf,
                                     const std::function<std::string()>& makeFilePath);

private:
    std::string boundary_;
    std::string boundary_line_;
    std::string end_boundary_;
    std::string marker_;
    MultipartLimits limits_;
};

}  // namespace cosmo::network::http
