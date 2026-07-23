#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include "util/Log.h"

namespace cosmo::service {

/// Lightweight DI container with compile-time type safety.
///
/// Supports two registration modes:
///   1. Owning:     Registry takes ownership via unique_ptr (production).
///   2. Non-owning: Registry stores a raw pointer (tests / external ownership).
///
/// Usage:
///   // Owning registration (production)
///   registry.Register<IFooService>(std::make_unique<FooServiceImpl>());
///
///   // Non-owning registration (tests)
///   MockFooService mock;
///   registry.Set<IFooService>(&mock);
///
///   // Retrieval (same for both modes)
///   auto& foo = registry.Get<IFooService>();
///
/// Registration is single-threaded and must finish before runtime callers start.
/// Lookup is thread-safe while the registry is initialized. Shutdown requires the
/// application to stop all external callers before invoking ShutdownAll(), because
/// Get() returns a non-owning reference whose use cannot be tracked by the registry.
class ServiceRegistry {
public:
    enum class LifecycleState {
        kRegistering,
        kInitialized,
        kShuttingDown,
    };

    static ServiceRegistry& Instance();

    /// Register an owned service implementation. The registry takes ownership
    /// of the unique_ptr and will destroy it in reverse registration order
    /// when ShutdownAll() is called.
    template <typename Interface, typename Impl>
    void Register(std::unique_ptr<Impl> impl) {
        static_assert(std::is_convertible_v<Impl*, Interface*>,
                      "registered implementation must implement the requested interface");
        if (!impl) {
            throw std::invalid_argument("cannot register a null service implementation");
        }

        auto* raw = static_cast<Interface*>(impl.get());
        std::shared_ptr<void> handle(std::move(impl));
        std::unique_lock lock(mutex_);
        const auto key = std::type_index(typeid(Interface));
        EnsureRegistrationAllowedLocked(typeid(Interface).name());
        if (entries_.find(key) != entries_.end()) {
            throw std::logic_error(std::string("service already registered: ") + typeid(Interface).name());
        }

        auto [entry, inserted] = entries_.emplace(key, Entry{raw, true});
        if (!inserted) {
            throw std::logic_error(std::string("service already registered: ") + typeid(Interface).name());
        }
        try {
            owned_.push_back(OwnedEntry{std::move(handle)});
        } catch (...) {
            entries_.erase(entry);
            throw;
        }
    }

    /// Register a non-owning pointer (for tests or externally-managed objects).
    /// Replacing another non-owning pointer is explicit and supported. An owned
    /// registration cannot be replaced or unregistered through Set(). Pass nullptr
    /// to unregister a non-owning entry.
    template <typename Interface>
    void Set(Interface* impl) {
        std::unique_lock lock(mutex_);
        const auto key = std::type_index(typeid(Interface));
        EnsureRegistrationAllowedLocked(typeid(Interface).name());
        auto existing = entries_.find(key);
        if (existing != entries_.end() && existing->second.is_owned) {
            throw std::logic_error(std::string("owned service cannot be replaced with Set: ") +
                                   typeid(Interface).name());
        }
        if (impl) {
            entries_[key] = Entry{static_cast<void*>(impl), false};
        } else {
            entries_.erase(key);
        }
    }

    /// Retrieve a service reference. Throws on lifecycle misuse or an unregistered
    /// interface. The returned reference is valid only while external shutdown has
    /// not begun.
    template <typename Interface>
    Interface& Get() {
        std::shared_lock lock(mutex_);
        if (state_ == LifecycleState::kShuttingDown) {
            LOG_ERRO("FATAL: ServiceRegistry is shutting down; cannot get {}", typeid(Interface).name());
            throw std::logic_error(std::string("service registry is shutting down: ") +
                                   typeid(Interface).name());
        }

        const auto key = std::type_index(typeid(Interface));
        const auto it  = entries_.find(key);
        if (it == entries_.end() || it->second.ptr == nullptr) {
            LOG_ERRO("FATAL: {} not registered in ServiceRegistry!", typeid(Interface).name());
            throw std::logic_error(std::string("service is not registered: ") + typeid(Interface).name());
        }
        return *static_cast<Interface*>(it->second.ptr);
    }

    /// Check if a service is registered.
    template <typename Interface>
    bool Has() const {
        std::shared_lock lock(mutex_);
        const auto key = std::type_index(typeid(Interface));
        const auto it  = entries_.find(key);
        return it != entries_.end() && it->second.ptr != nullptr;
    }

    /// Freeze the service map after all owning services and aliases have been
    /// registered. Runtime registration attempts fail until ShutdownAll() finishes.
    void CompleteRegistration();

    LifecycleState GetLifecycleState() const;

    /// Destroy all owned services in reverse registration order, then clear
    /// all entries. Call this in SwDeviceDestroy().
    void ShutdownAll();

    /// Number of registered services (for diagnostics).
    size_t Size() const {
        std::shared_lock lock(mutex_);
        return entries_.size();
    }

private:
    ServiceRegistry() = default;
    ~ServiceRegistry();
    ServiceRegistry(const ServiceRegistry&)            = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    struct Entry {
        void* ptr     = nullptr;
        bool is_owned = false;
    };

    struct OwnedEntry {
        std::shared_ptr<void> handle;
    };

    void EnsureRegistrationAllowedLocked(const char* interface_name) const;

    std::unordered_map<std::type_index, Entry> entries_;
    std::vector<OwnedEntry> owned_;
    mutable std::shared_mutex mutex_;
    std::condition_variable_any shutdown_cv_;
    LifecycleState state_{LifecycleState::kRegistering};
    std::thread::id shutdown_thread_id_;
};

}  // namespace cosmo::service
