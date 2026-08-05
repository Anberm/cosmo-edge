#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/device/rknn/rknn_node_creator.h"

#include "nn/device/host/host_node_factory.h"
#include "nn/device/rknn/rknn_net_node.h"

namespace cosmo::nn {

RknnNodeCreator::RknnNodeCreator(DeviceType device_type) : NodeCreator(device_type) {}

std::unique_ptr<Node> RknnNodeCreator::CreateNode(NodeType type) {
    if (type == NODE_NET)
        return std::make_unique<RknnNetNode>();
    return CreateHostNode(type);
}

NodeCreatorRegister<RknnNodeCreator> g_rknn_node_creator_register(DEVICE_RKNN);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
