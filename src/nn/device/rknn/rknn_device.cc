#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/device/naive/naive_device.h"

namespace cosmo::nn {

// Graph metadata and compatibility tensors retain the graph-owned NaiveDevice
// lifecycle. Native input allocation and rknn_set_io_mem binding are owned by
// RknnNetNode/RKNN preprocess, so this registration does not imply host-copy
// inference on the admitted DMA-BUF path.
TypeDeviceRegister<NaiveDevice> g_rknn_device_register(DEVICE_RKNN);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
