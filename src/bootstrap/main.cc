#include <iostream>
#include <string_view>

#include "bootstrap/ReleaseBootstrap.h"

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "install") {
        return cosmo::bootstrap::BootstrapSignedRelease(argv[2]);
    }
    if (argc == 2 && std::string_view(argv[1]) == "recover") {
        return cosmo::bootstrap::RecoverFactoryBootstrap();
    }
    std::cerr << "Usage: cosmo-release-bootstrap install <absolute-archive>\n"
                 "       cosmo-release-bootstrap recover\n";
    return 2;
}
