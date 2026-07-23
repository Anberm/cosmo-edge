// ServiceRegistry — Service Registry implementation.

#include "service/detail/ServiceRegistry.h"

namespace cosmo::service {

ServiceRegistry& ServiceRegistry::Instance() {
    static ServiceRegistry instance;
    return instance;
}

ServiceRegistry::~ServiceRegistry() {
    ShutdownAll();
}

void ServiceRegistry::EnsureRegistrationAllowedLocked(const char* interface_name) const {
    if (state_ != LifecycleState::kRegistering) {
        throw std::logic_error(std::string("service registration is closed: ") + interface_name);
    }
}

void ServiceRegistry::CompleteRegistration() {
    std::unique_lock lock(mutex_);
    if (state_ != LifecycleState::kRegistering) {
        throw std::logic_error("service registry registration is not active");
    }
    state_ = LifecycleState::kInitialized;
}

ServiceRegistry::LifecycleState ServiceRegistry::GetLifecycleState() const {
    std::shared_lock lock(mutex_);
    return state_;
}

void ServiceRegistry::ShutdownAll() {
    std::vector<OwnedEntry> owned;
    {
        std::unique_lock lock(mutex_);
        if (state_ == LifecycleState::kShuttingDown) {
            if (shutdown_thread_id_ == std::this_thread::get_id()) {
                return;
            }
            shutdown_cv_.wait(lock, [this]() { return state_ != LifecycleState::kShuttingDown; });
            return;
        }

        state_              = LifecycleState::kShuttingDown;
        shutdown_thread_id_ = std::this_thread::get_id();
        entries_.clear();
        owned.swap(owned_);
    }

    // Destructors may inspect the registry. Destroy outside mutex_ so those
    // callbacks fail fast instead of deadlocking on the registry lock.
    while (!owned.empty()) {
        auto handle = std::move(owned.back().handle);
        owned.pop_back();
        handle.reset();
    }

    {
        std::unique_lock lock(mutex_);
        shutdown_thread_id_ = {};
        state_              = LifecycleState::kRegistering;
    }
    shutdown_cv_.notify_all();
}

}  // namespace cosmo::service
