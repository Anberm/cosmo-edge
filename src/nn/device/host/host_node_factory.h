#pragma once

#ifdef COSMO_NN_USE_HOST_BACKEND

#include <memory>

#include "nn/node/node.h"

namespace cosmo::nn {

// Creates preprocessing, postprocessing and copy nodes that operate on host
// memory. Network execution is deliberately excluded so each inference backend
// retains its own runtime lifecycle.
std::unique_ptr<Node> CreateHostNode(NodeType type);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_HOST_BACKEND
