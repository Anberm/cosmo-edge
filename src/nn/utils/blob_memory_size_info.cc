#include "nn/utils/blob_memory_size_info.h"

#include "nn/utils/data_type_utils.h"
#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

static constexpr int64_t kMaxBlobBytes = 512L * 1024 * 1024;

namespace {

    bool CheckedMultiply(int64_t lhs, int64_t rhs, int64_t* result) {
        if (lhs <= 0 || rhs <= 0 || lhs > kMaxBlobBytes / rhs) {
            return false;
        }
        *result = lhs * rhs;
        return true;
    }

}  // namespace

int64_t GetBlobMemoryBytesSize(const BlobMemorySizeInfo& size_info) {
    const int bytes_per_element = DataTypeUtils::GetBytesSize(size_info.data_type);
    if (bytes_per_element <= 0) {
        return -1;
    }

    if (size_info.dims.size() == 1) {
        const int64_t dims_count = size_info.dims.front();
        int64_t bytes            = 0;
        if (!CheckedMultiply(dims_count, bytes_per_element, &bytes)) {
            return -1;
        }
        return bytes;
    } else if (size_info.dims.size() == 2) {
        int64_t dims_count = 1;
        for (auto dim : size_info.dims) {
            if (!CheckedMultiply(dims_count, dim, &dims_count)) {
                return -1;
            }
        }
        int64_t packed_count = 0;
        int64_t bytes        = 0;
        if (!CheckedMultiply(dims_count, 4, &packed_count) ||
            !CheckedMultiply(packed_count, bytes_per_element, &bytes)) {
            return -1;
        }
        return bytes;
    } else {
        return -1;
    }
}

}  // namespace cosmo::nn
