#pragma once

#include "bmcv_api_ext.h"

namespace cosmo::nn {

class SophonImageGuard {
public:
    SophonImageGuard() = default;

    ~SophonImageGuard() {
        Reset();
    }

    SophonImageGuard(const SophonImageGuard&)            = delete;
    SophonImageGuard& operator=(const SophonImageGuard&) = delete;

    bm_image* Out() {
        return &image_;
    }

    bm_image& Get() {
        return image_;
    }

    const bm_image& Get() const {
        return image_;
    }

    void MarkCreated() {
        created_ = true;
    }

    void Reset() noexcept {
        if (!created_) {
            return;
        }
        if (bm_image_is_attached(image_)) {
            (void)bm_image_detach(image_);
        }
#ifdef COSMO_NN_SOPHON_1684X
        (void)bm_image_destroy(image_);
#else
        (void)bm_image_destroy(&image_);
#endif
        created_ = false;
    }

private:
    bm_image image_{};
    bool created_ = false;
};

}  // namespace cosmo::nn
