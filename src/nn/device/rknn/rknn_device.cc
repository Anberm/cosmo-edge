#ifdef COSMO_NN_USE_RKNN_BACKEND

#include "nn/device/naive/naive_device.h"

namespace cosmo::nn {

// RKNN's copy-first backend uses graph-owned host buffers. Registering a
// DEVICE_RKNN allocator keeps externally wrapped image blobs and BlobStore's
// calculate-device lifecycle valid without pretending the buffers are NPU DMA.
TypeDeviceRegister<NaiveDevice> g_rknn_device_register(DEVICE_RKNN);

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_RKNN_BACKEND
