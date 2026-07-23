#include "nn/utils/blob_memory_size_utils.h"

#include <limits>

#include "nn/utils/dims_vector_utils.h"

namespace cosmo::nn {

BlobMemorySizeInfo Calculate1DMemorySize(BlobDesc& desc) {
    BlobMemorySizeInfo info;
    info.data_type = desc.data_type;

    int count = -1;
    if (desc.data_format == DATA_FORMAT_NC4HW4) {
        if (desc.dims.size() == 4 && desc.dims[1] > 0 &&
            desc.dims[1] <= std::numeric_limits<int>::max() - 3) {
            auto aligned_dims = desc.dims;
            aligned_dims[1]   = cosmo::nn::RoundUp(aligned_dims[1], 4);
            count             = DimsVectorUtils::Count(aligned_dims);
        }
    } else {
        count = DimsVectorUtils::Count(desc.dims);
        if (count >= 0 && ImageFormatIsYUV(desc.image_format)) {
            count = count / 2;
        }
    }
    info.dims.push_back(count);

    return info;
}

}  // namespace cosmo::nn
