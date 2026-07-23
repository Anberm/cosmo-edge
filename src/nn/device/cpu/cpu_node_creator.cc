#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_node_creator.h"

#include "nn/device/cpu/cpu_net_node.h"
#include "nn/device/host/host_node_factory.h"

namespace cosmo::nn {

CpuNodeCreator::CpuNodeCreator(DeviceType device_type) : NodeCreator(device_type) {}

std::unique_ptr<Node> CpuNodeCreator::CreateNode(NodeType type) {
    if (type == NODE_NET)
        return std::make_unique<CpuNetNode>();
    return CreateHostNode(type);
}

// Auto-register CPU node creator
NodeCreatorRegister<CpuNodeCreator> g_cpu_node_creator_register(DEVICE_CPU);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
