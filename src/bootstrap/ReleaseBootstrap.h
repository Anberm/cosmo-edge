#pragma once

namespace cosmo::bootstrap {

// Production entry point. The archive is the only variable input; roots,
// validation tools, backend script, and trust anchor are all fixed.
int BootstrapSignedRelease(const char* archive_path);

// Stable factory-recovery entry point.  This deliberately has no variable
// path or trust input and can therefore still run while the release facades
// are absent or being rolled back.
int RecoverFactoryBootstrap();

}  // namespace cosmo::bootstrap
