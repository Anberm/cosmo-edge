#pragma once

#include <atomic>
#include <exception>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "flow/face/FaceLib.h"

namespace cosmo {
class FaceLib;

/// Person import service — handles bulk face import from zip archives.
/// Owned by FaceLibServiceImpl (no longer a singleton).
class PersonImport {
public:
    PersonImport();
    ~PersonImport();

    /// Permanently reject new imports and wait for the current import to
    /// finish. Safe to call repeatedly and concurrently.
    void Stop() noexcept;

    /// Import images from a zip archive into a face library.
    /// @param file_name Path to the uploaded zip file.
    /// @param face_lib_id Target face library identifier.
    /// @return true if import started successfully.
    bool ImportFile(const std::string& file_name, const std::string& face_lib_id);

    /// Get the unique ID of the current import batch.
    std::string GetQueryId() const;

    /// Get the total number of records in the current import batch.
    int GetTotalCount() const;

    /// Get <success, failure> counts of the current import.
    std::pair<int, int> GetStatus() const;

    /// Check whether the current import has completed.
    /// Thread-safe — does not call future::get() from multiple threads.
    bool Complete() const;

    /// Get the URL to the failure report CSV file.
    std::string GetFailedUrl() const;

private:
    void DoImportFile(const std::string& file_name, FaceLibPtr facelib, const std::string& uuid);
    bool UnzipArchive(const std::string& file_name, const std::string& work_dir);
    void ProcessImages(const std::string& work_dir, FaceLibPtr facelib, std::ofstream& ofile);
    void Clean();

    // Serializes ownership and replacement of future_.  Status readers use
    // mtx_ independently so polling cannot race with import admission.
    std::mutex stop_mutex_;
    mutable std::mutex import_mutex_;
    mutable std::shared_mutex mtx_;
    std::string query_id_;
    std::string error_file_;
    int success_{0};
    int failed_{0};
    int total_{0};
    std::exception_ptr async_error_;
    mutable std::future<void> future_;
    mutable std::atomic<bool> completed_{true};
    bool stopped_{false};
};
}  // namespace cosmo
