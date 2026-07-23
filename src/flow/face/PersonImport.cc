// PersonImport — Person Import implementation.

#include "flow/face/PersonImport.h"

#include <iconv.h>

#include <array>
#include <filesystem>
#include <fstream>

#include "flow/face/FacePic.h"
#include "flow/face/FaceRegLib.h"
#include "flow/face/Person.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/IFaceFeature.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IFaceLibService.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRepo.h"
#include "service/media/IVideoFrameCodec.h"
#include "util/ArchiveListingValidator.h"
#include "util/DurationLogger.h"
#include "util/Exception.h"
#include "util/Exec.h"
#include "util/FileUtil.h"
#include "util/Log.h"
#include "util/PathUtil.h"
#include "util/StringUtil.h"
#include "util/TimeUtil.h"
#include "util/Transcode.h"
#include "util/UuidUtil.h"

namespace cosmo {
namespace fs = std::filesystem;

static constexpr const char* kTag           = "PersonImport";
static constexpr float kImportQuality       = 80.0f;
static constexpr int kDbBatchCommitInterval = 1000;
static const std::vector<std::string> kValidImageExtensions{".png", ".jpg", ".jpeg"};

namespace {

    constexpr size_t kMaxImportArchiveEntries         = 10000;
    constexpr std::uintmax_t kMaxImportFileBytes      = 500ULL * 1024 * 1024;
    constexpr std::uintmax_t kMaxImportExtractedBytes = 1024ULL * 1024 * 1024;

    bool IsZipFile(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        std::array<unsigned char, 4> header{};
        if (!stream.read(reinterpret_cast<char*>(header.data()), header.size())) {
            return false;
        }
        return header[0] == 'P' && header[1] == 'K' &&
               ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
                (header[2] == 7 && header[3] == 8));
    }

    bool ValidateZipMemberPaths(const std::string& archive_path) {
        return util::ValidateArchiveListingFile(
            archive_path, util::ArchiveListingFormat::kZipVerbose,
            {kMaxImportArchiveEntries, kMaxImportFileBytes, kMaxImportExtractedBytes});
    }

    bool ValidateExtractedTree(const std::string& root) {
        size_t count              = 0;
        std::uintmax_t total_size = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (++count > kMaxImportArchiveEntries) {
                return false;
            }
            const auto status = it->symlink_status(ec);
            if (ec || fs::is_symlink(status) || (!fs::is_regular_file(status) && !fs::is_directory(status))) {
                return false;
            }
            std::string resolved;
            if (!cosmo::path::ResolveExistingPathWithinRoot(root, it->path().string(),
                                                            cosmo::path::PathEntryType::kAny, resolved)) {
                return false;
            }
            const auto relative = fs::relative(it->path(), root, ec);
            if (ec || relative.empty()) {
                return false;
            }
            for (const auto& component : relative) {
                if (!cosmo::path::IsSafePathComponent(component.string())) {
                    return false;
                }
            }
            if (fs::is_regular_file(status)) {
                const auto size = fs::file_size(it->path(), ec);
                if (ec || size > kMaxImportFileBytes || size > kMaxImportExtractedBytes - total_size) {
                    return false;
                }
                total_size += size;
            }
        }
        return !ec;
    }

    void RemoveManagedUpload(const std::string& path) {
        std::string resolved;
        if (!cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetUploadPath(), path,
                                                        cosmo::path::PathEntryType::kRegularFile, resolved)) {
            LOG_WARN("{} Refusing to remove unmanaged upload path", kTag);
            return;
        }
        std::error_code ec;
        fs::remove(resolved, ec);
        if (ec) {
            LOG_WARN("{} Failed to remove managed upload: {}", kTag, ec.message());
        }
    }

}  // namespace

PersonImport::PersonImport() : error_file_(fs::path(cosmo::path::GetWebLocalPath()) / "importerror.csv") {}

PersonImport::~PersonImport() {
    Stop();
}

void PersonImport::Stop() noexcept {
    // Serialize concurrent Stop callers so every caller returns only after the
    // active future has completed. Move the single-consumer future while
    // holding import_mutex_, then wait without either lock used by the worker.
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    std::future<void> pending;
    {
        std::lock_guard<std::mutex> import_lock(import_mutex_);
        stopped_ = true;
        pending  = std::move(future_);
    }
    if (pending.valid()) {
        try {
            pending.get();
        } catch (...) {
            // Shutdown must still complete even if the asynchronous job failed.
        }
    }
}

bool PersonImport::ImportFile(const std::string& file_name, const std::string& face_lib_id) {
    // future_ is a single-consumer handle.  Keep admission, completion
    // collection, and replacement in one critical section so concurrent HTTP
    // workers cannot start two imports or call get() on the same future.
    std::lock_guard<std::mutex> import_lock(import_mutex_);
    if (stopped_) {
        LOG_WARN("{} Reject import after shutdown", kTag);
        return false;
    }
    try {
        if (future_.valid()) {
            if (future_.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
                return false;
            }
            future_.get();
        }
    } catch (const std::exception& e) {
        std::filesystem::remove_all(fs::path(cosmo::path::GetTemporaryDirPath()) / GetQueryId());
        LOG_ERRO("{} {}", kTag, e.what());
    }

    std::string managed_upload;
    if (!cosmo::path::ResolveExistingPathWithinRoot(cosmo::path::GetUploadPath(), file_name,
                                                    cosmo::path::PathEntryType::kRegularFile,
                                                    managed_upload)) {
        LOG_ERRO("{} Reject unmanaged or unsafe face import archive", kTag);
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "File validation error");
    }
    std::error_code archive_ec;
    const auto archive_size = fs::file_size(managed_upload, archive_ec);
    if (archive_ec || archive_size == 0 || archive_size > kMaxImportFileBytes || !IsZipFile(managed_upload) ||
        !ValidateZipMemberPaths(managed_upload)) {
        LOG_ERRO("{} Reject invalid face import archive", kTag);
        RemoveManagedUpload(managed_upload);
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "File validation error");
    }

    auto face_lib = service::ServiceRegistry::Instance().Get<service::IFaceLibRepo>().GetFaceLib(face_lib_id);
    if (!face_lib) {
        LOG_ERRO("{} Failed to query face library, {}", kTag, face_lib_id);
        RemoveManagedUpload(managed_upload);
        throw util::ErrorMessage(util::ErrorEnum::ParameterException, "Face library ID query failed");
    }

    auto id = util::GenerateUUID();

    const auto job_dir   = fs::path(cosmo::path::GetTemporaryDirPath()) / id;
    const auto temp_path = job_dir / "input.zip";
    std::error_code cp_ec;
    std::filesystem::create_directory(job_dir, cp_ec);
    if (!cp_ec) {
        std::filesystem::permissions(job_dir, fs::perms::owner_all, fs::perm_options::replace, cp_ec);
    }
    if (!cp_ec) {
        std::filesystem::copy_file(managed_upload, temp_path, std::filesystem::copy_options::none, cp_ec);
    }
    if (cp_ec) {
        LOG_ERRO("{} Failed to claim import archive: {}", kTag, cp_ec.message());
        std::filesystem::remove_all(job_dir, cp_ec);
        throw util::ErrorMessage(util::ErrorEnum::FileMoveFailed, "Failed to claim import archive");
    }
    RemoveManagedUpload(managed_upload);
    LOG_INFO("{} File move and backup completed {} {}", kTag, temp_path, query_id_);

    completed_.store(false, std::memory_order_release);
    try {
        future_ = async(std::launch::async, [this, temp_path, face_lib, id]() {
            LOG_INFO("{} Ready to import face", kTag);
            try {
                DoImportFile(temp_path, face_lib, id);
            } catch (...) {
                {
                    std::lock_guard<std::shared_mutex> lock(mtx_);
                    async_error_ = std::current_exception();
                }
                std::error_code cleanup_error;
                std::filesystem::remove_all(fs::path(cosmo::path::GetTemporaryDirPath()) / id, cleanup_error);
                LOG_ERRO("{} Import job {} failed", kTag, id);
            }
            completed_.store(true, std::memory_order_release);
            LOG_INFO("{} Import face completed", kTag);
        });
    } catch (...) {
        completed_.store(true, std::memory_order_release);
        std::error_code cleanup_error;
        std::filesystem::remove_all(job_dir, cleanup_error);
        throw;
    }
    return true;
}

bool PersonImport::UnzipArchive(const std::string& file_name, const std::string& work_dir) {
    std::filesystem::create_directories(work_dir);
    // Fix occasional decompression garbled text issue on 1800, reference:
    // https://blog.csdn.net/guo_qiangqiang/article/details/107163832
    std::string unzip_out;
    int unzip_ret = util::Exec({"unzip", file_name, "-d", work_dir}, unzip_out);
    if (unzip_ret != 0) {
        LOG_WARN("{} unzip exit code {}: {}", kTag, unzip_ret, unzip_out);
        return false;
    }
    if (!ValidateExtractedTree(work_dir)) {
        LOG_WARN("{} Reject extracted face archive tree", kTag);
        return false;
    }
    std::error_code ec;
    fs::remove(fs::path{file_name}, ec);
    LOG_INFO("{} Unzip {} -d {}", kTag, file_name, work_dir);
    return true;
}

void PersonImport::ProcessImages(const std::string& work_dir, FaceLibPtr face_lib, std::ofstream& ofile) {
    util::Transcode transcode("UTF-8", "GB18030");

    // Count total files
    for (auto& dir_entry : fs::recursive_directory_iterator(work_dir)) {
        if (fs::is_directory(dir_entry.status())) {
            continue;
        }
        std::lock_guard<std::shared_mutex> lock(mtx_);
        ++total_;
    }

    fs::path unzip_path{"/usr/bin/unzip"};
    // Photos decompressed by busybox have original encoding, normal unzip has UTF-8, no conversion needed
    bool need_transcode = fs::is_symlink(unzip_path) && fs::read_symlink(unzip_path).filename() == "busybox";

    size_t face_count = face_lib->GetFaceCount();
    size_t max_count  = face_lib->GetFaceMaxCount();
    util::DurationLogger logger_total("import face recognise");
    auto& dao_svc     = service::ServiceRegistry::Instance().Get<service::IPersonDaoService>();
    auto& person_repo = service::ServiceRegistry::Instance().Get<service::IPersonRepo>();
    auto& codec_svc   = service::ServiceRegistry::Instance().Get<service::IVideoFrameCodec>();
    auto& feature_svc = service::ServiceRegistry::Instance().Get<service::IFaceFeature>();

    struct PendingPerson {
        PersonPtr person;
        std::vector<FacePicPtr> pictures;
        std::string source_name;
    };
    std::vector<PendingPerson> pending;
    pending.reserve(kDbBatchCommitInterval);
    bool transaction_active = false;

    const auto record_failure = [&](const std::string& source_name, const std::string& reason) {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        ++failed_;
        ofile << "\"" << failed_ << "\t\",\"" << source_name << "\t\",\"" << reason << "\t\",\"\t\"\n";
    };
    const auto begin_transaction = [&]() {
        dao_svc.Begin();
        transaction_active = true;
    };
    const auto rollback_transaction = [&]() noexcept {
        if (!transaction_active) {
            return;
        }
        try {
            dao_svc.Rollback();
        } catch (const std::exception& e) {
            LOG_ERRO("{} Failed to roll back face import batch: {}", kTag, e.what());
        }
        transaction_active = false;
    };
    const auto publish_pending = [&]() {
        for (auto& item : pending) {
            person_repo.AddPerson(face_lib, item.person);
            for (auto& picture : item.pictures) {
                item.person->AddPicture(picture);
            }
        }
        {
            std::lock_guard<std::shared_mutex> lock(mtx_);
            success_ += static_cast<int>(pending.size());
        }
        pending.clear();
    };
    const auto commit_batch = [&](bool restart) {
        dao_svc.Commit();
        transaction_active = false;
        publish_pending();
        if (restart) {
            begin_transaction();
        }
    };

    begin_transaction();
    LOG_INFO("{} Unzip completed, pic count is {} {}", kTag, total_, work_dir);

    try {
        for (auto& dir_entry : fs::recursive_directory_iterator(work_dir)) {
            if (fs::is_directory(dir_entry.status())) {
                continue;
            }
            fs::path name;
            if (need_transcode) {
                name = transcode.Convert(dir_entry.path().filename().string());
            } else {
                name = dir_entry.path().filename();
            }
            std::string raw_path = name.string();
            raw_path.erase(std::remove(raw_path.begin(), raw_path.end(), '"'), raw_path.end());

            if (face_count >= max_count) {
                std::lock_guard<std::shared_mutex> lock(mtx_);
                ++failed_;
                ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\""
                      << "Face library capacity reached limit" << "\t\",\"";
                ofile << "\t\"\n";
                continue;
            }
            auto it = find_if(kValidImageExtensions.begin(), kValidImageExtensions.end(),
                              [&dir_entry](const auto& ext) {
                                  return util::ToLower(dir_entry.path().extension().string()) == ext;
                              });
            if (it == kValidImageExtensions.end()) {
                std::lock_guard<std::shared_mutex> lock(mtx_);
                ++failed_;
                ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\"" << "Invalid image format"
                      << "\t\",\"";
                ofile << "\t\"\n";
                continue;
            }

            auto person = std::make_shared<Person>(std::string{});
            person->SetName(name.stem());
            person->SetCreateTime(util::GetMilliseconds());
            person->SetUpdateTime(person->GetCreateTime());
            VideoFramePtr image;
            VideoFramePtr cut_image;
            std::vector<FacePicPtr> face_pics;
            auto file_data = util::ReadFileBin(dir_entry.path().string());
            if (!file_data.empty()) {
                image = codec_svc.DecodeJpeg(file_data);
            }
            if (!image) {
                std::lock_guard<std::shared_mutex> lock(mtx_);
                ++failed_;
                ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\""
                      << "Image too small or decode failed" << "\t\",\"";
                ofile << "\t\"\n";
                continue;
            }
            try {
                auto id = util::GenerateUUID();
                AiFeature feature;
                auto temp = feature_svc.ExtractFaceFeature(image, kImportQuality, feature, cut_image);
                if (util::ErrorEnum::Success != temp) {
                    std::lock_guard<std::shared_mutex> lock(mtx_);
                    ++failed_;
                    ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\""
                          << "get face feature failed" << "\t\",\"";
                    ofile << "\t\"\n";
                    continue;
                }
                auto orig_jpeg         = codec_svc.EncodeJpeg(cut_image);
                std::string photo_path = (fs::path(cosmo::path::GetFaceLibPhotoDir()) / id).concat(".jpg");
                if (util::WriteFile(photo_path, reinterpret_cast<const std::uint8_t*>(orig_jpeg.data()),
                                    static_cast<int>(orig_jpeg.size()))) {
                    face_pics.push_back(
                        std::make_shared<FacePic>(id, cosmo::path::GetFaceLibPhotoDir(), feature));
                } else {
                    LOG_ERRO("{} write file error", kTag);
                }
            } catch (const util::ErrorMessage& e) {
                std::lock_guard<std::shared_mutex> lock(mtx_);
                ++failed_;
                ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\"" << e.what() << "\t\",\"";
                ofile << "\t\"\n";
                continue;
            }
            if (!face_pics.empty()) {
                db::FaceRegRecordUnit face_record_unit;
                face_record_unit.face_update_time = person->GetUpdateTime();
                face_record_unit.face_create_time = person->GetCreateTime();
                face_record_unit.face_name        = person->GetName();
                face_record_unit.serial_name      = person->GetSerialNumber();
                face_record_unit.id               = person->GetId();
                face_record_unit.face_lib_id.push_back(face_lib->GetId());
                for (const auto& picture : face_pics) {
                    db::FacePicInfo temp;
                    temp.id      = picture->GetId();
                    temp.feature = picture->GetFeature().feature;
                    face_record_unit.face_pic_infos.push_back(temp);
                }

                if (!dao_svc.AddPerson(face_record_unit)) {
                    rollback_transaction();
                    face_count -= pending.size();
                    for (const auto& item : pending) {
                        record_failure(item.source_name, "Database transaction rolled back");
                    }
                    pending.clear();
                    record_failure(raw_path, "Database write failed");
                    begin_transaction();
                    continue;
                }

                pending.push_back({person, std::move(face_pics), raw_path});
                ++face_count;
                if (pending.size() == kDbBatchCommitInterval) {
                    commit_batch(true);
                }
            } else {
                std::lock_guard<std::shared_mutex> lock(mtx_);
                ++failed_;
                ofile << "\"" << failed_ << "\t\",\"" << raw_path << "\t\",\"";
                ofile << "\t\"\n";
            }
        }
        commit_batch(false);
    } catch (...) {
        rollback_transaction();
        pending.clear();
        feature_svc.ReleaseFaceModels();
        throw;
    }
    // Release VRAM of face feature extraction model after batch import
    feature_svc.ReleaseFaceModels();
}

void PersonImport::DoImportFile(const std::string& file_name, FaceLibPtr face_lib, const std::string& uuid) {
    Clean();
    {
        std::lock_guard<std::shared_mutex> lock(mtx_);
        query_id_ = uuid;
    }
    LOG_INFO("{} import file {} {}", kTag, file_name.c_str(), query_id_);

    auto work_dir = (fs::path(cosmo::path::GetTemporaryDirPath()) / query_id_ / "extracted").string();

    if (!UnzipArchive(file_name, work_dir)) {
        std::error_code ec;
        std::filesystem::remove_all(fs::path(cosmo::path::GetTemporaryDirPath()) / query_id_, ec);
        throw util::ErrorMessage(util::ErrorEnum::UnZipFileFailed, "Failed to extract face archive");
    }

    std::ofstream ofile(error_file_);
    unsigned char szUTF_8BOM[] = {0xEF, 0xBB, 0xBF, 0};
    ofile << szUTF_8BOM;
    ofile << "\"No.\",\"Photo Name\",\"Failure Reason\"\n";

    ProcessImages(work_dir, face_lib, ofile);

    std::filesystem::remove_all(fs::path(cosmo::path::GetTemporaryDirPath()) / query_id_);
}

std::string PersonImport::GetQueryId() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return query_id_;
}

int PersonImport::GetTotalCount() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return total_;
}

std::pair<int, int> PersonImport::GetStatus() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return {success_, failed_};
}

bool PersonImport::Complete() const {
    if (!completed_.load(std::memory_order_acquire)) {
        return false;
    }
    std::shared_lock<std::shared_mutex> lock(mtx_);
    if (async_error_) {
        std::rethrow_exception(async_error_);
    }
    return true;
}

void PersonImport::Clean() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    total_       = 0;
    success_     = 0;
    failed_      = 0;
    async_error_ = nullptr;
    if (!query_id_.empty()) {
        std::string cleanup_path;
        const auto temporary_root = cosmo::path::GetTemporaryDirPath();
        if (cosmo::path::IsSafePathComponent(query_id_) &&
            cosmo::path::ResolvePathWithinRoot(
                temporary_root, (fs::path(temporary_root) / query_id_).string(), cleanup_path)) {
            std::filesystem::remove_all(cleanup_path);
        }
    }
    query_id_.clear();
}

std::string PersonImport::GetFailedUrl() const {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    return fs::path(cosmo::path::GetWebAcessPath()) / "importerror.csv";
}

}  // namespace cosmo
