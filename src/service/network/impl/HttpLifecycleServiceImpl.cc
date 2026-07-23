// HttpLifecycleServiceImpl — Http Lifecycle Service Impl implementation.

#include "service/network/impl/HttpLifecycleServiceImpl.h"

#include <stdexcept>

#include "network/http/HttpServer.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IAppInfoService.h"
#include "util/Log.h"
#include "util/PathUtil.h"

namespace cosmo::service {

HttpLifecycleServiceImpl::HttpLifecycleServiceImpl(DispatcherFactory dispatcher_factory)
    : dispatcher_factory_(std::move(dispatcher_factory)) {}

HttpLifecycleServiceImpl::~HttpLifecycleServiceImpl() {
    HttpLifecycleServiceImpl::StopHttpServer();
}

void HttpLifecycleServiceImpl::InitHttpServer(const std::string& host, uint16_t port) {
    http_server_ = std::make_unique<network::http::HttpServer>();

    auto& reg = cosmo::service::ServiceRegistry::Instance();
    network::http::HttpServerCallbacks cbs{
        []() { return cosmo::path::GetUploadTmpPath(); },
        [&reg]() { return reg.Get<IAppInfoService>().UserDataPath(); },
        [&reg]() { return reg.Get<IAppInfoService>().LogPath(); },
    };

    if (!http_server_->Initialize(host, port, dispatcher_factory_, std::move(cbs))) {
        http_server_.reset();
        throw std::runtime_error("failed to initialize HTTP server on " + host + ":" + std::to_string(port));
    }

    LOG_INFO("HttpServer initialized on {}:{}", host, port);
}

void HttpLifecycleServiceImpl::RunHttpLoop() {
    if (!http_server_) {
        LOG_ERRO("{}", "RunHttpLoop: HttpServer not initialized!");
        std::abort();
    }
    http_server_->DispatchMsg();
}

void HttpLifecycleServiceImpl::RequestHttpStop() noexcept {
    if (http_server_) {
        http_server_->RequestStop();
    }
}

void HttpLifecycleServiceImpl::StopHttpServer() {
    if (http_server_) {
        http_server_->UnInitialize();
        http_server_.reset();
    }
}

}  // namespace cosmo::service
