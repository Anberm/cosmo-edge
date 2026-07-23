#ifdef COSMO_NN_USE_HOST_BACKEND

#include "nn/device/host/host_node_factory.h"

#include "nn/device/cpu/cpu_affine_crop_node.h"
#include "nn/device/cpu/cpu_copy_node.h"
#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/device/cpu/cpu_dino_encode_node.h"
#include "nn/device/cpu/cpu_normalize_node.h"
#include "nn/device/cpu/cpu_resize_node.h"
#include "nn/device/cpu/cpu_sequence_node.h"
#include "nn/device/cpu/cpu_yolo_decode_node.h"

namespace cosmo::nn {

std::unique_ptr<Node> CreateHostNode(NodeType type) {
    switch (type) {
        case NODE_RESIZE:
            return std::make_unique<CpuResizeNode>();
        case NODE_CROP_RESIZE:
            return std::make_unique<CpuCropResizeNode>();
        case NODE_AFFINE_CROP:
            return std::make_unique<CpuAffineCropNode>();
        case NODE_SEQUENCE:
            return std::make_unique<CpuSequenceNode>();
        case NODE_NORMALIZE:
            return std::make_unique<CpuNormalizeNode>();
        case NODE_COPY:
            return std::make_unique<CpuCopyNode>();
        case NODE_DINO_ENCODE:
            return std::make_unique<CpuDinoEncodeNode>();
        case NODE_YOLO_NPU_DECODE:
            return std::make_unique<CpuYoloDecodeNPUNode>();
        default:
            return nullptr;
    }
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_HOST_BACKEND
