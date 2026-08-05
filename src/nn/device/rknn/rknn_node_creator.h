#pragma once

#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/node/node_creator.h"

namespace cosmo::nn {

class RknnNodeCreator final : public NodeCreator {
public:
    explicit RknnNodeCreator(DeviceType device_type);
    std::unique_ptr<Node> CreateNode(NodeType type) override;
};

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
