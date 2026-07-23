// FaceLibServiceImpl — FaceLibService implementation

#include "service/face/impl/FaceLibServiceImpl.h"

#include "flow/face/FaceManager.h"
#include "flow/face/PersonImport.h"
#include "service/detail/ServiceRegistry.h"
#include "service/face/impl/FaceFeatureExtractor.h"
#include "util/Exception.h"
#include "util/Log.h"

namespace cosmo::service {

FaceLibServiceImpl::FaceLibServiceImpl()
    : person_import_(std::make_unique<cosmo::PersonImport>()),
      face_manager_(std::make_unique<cosmo::FaceManager>()) {}

FaceLibServiceImpl::OperationGuard::OperationGuard(FaceLibServiceImpl& owner) : owner_(&owner) {
    std::lock_guard<std::mutex> lock(owner.operation_mutex_);
    if (owner.stopped_) {
        owner_ = nullptr;
        return;
    }
    ++owner.active_operations_;
}

FaceLibServiceImpl::OperationGuard::~OperationGuard() {
    if (owner_ != nullptr) {
        owner_->EndOperation();
    }
}

FaceLibServiceImpl::OperationGuard::operator bool() const noexcept {
    return owner_ != nullptr;
}

void FaceLibServiceImpl::EndOperation() noexcept {
    std::lock_guard<std::mutex> lock(operation_mutex_);
    if (--active_operations_ == 0) {
        operation_cv_.notify_all();
    }
}

std::shared_ptr<cosmo::FaceFeatureExtractor> FaceLibServiceImpl::EnsureFaceFeature() {
    std::lock_guard<std::mutex> lock(face_feature_mutex_);
    if (!face_feature_) {
        face_feature_ = std::make_shared<cosmo::FaceFeatureExtractor>();
    }
    return face_feature_;
}

FaceLibServiceImpl::~FaceLibServiceImpl() {
    StopImpl();
}

// ---- FaceManager: face lib management ----
std::vector<cosmo::FaceLibPtr> FaceLibServiceImpl::GetAllFaceLibs() {
    return face_manager_->GetAllFaceLibs();
}

size_t FaceLibServiceImpl::GetFaceLibMaxCount() const {
    return face_manager_->GetFaceLibMaxCount();
}

cosmo::util::ErrorEnum FaceLibServiceImpl::AddFaceLib(cosmo::FaceLibPtr faceLib) {
    return face_manager_->AddFaceLib(faceLib);
}

cosmo::util::ErrorEnum FaceLibServiceImpl::UpdateFaceLib(const std::string& faceLibId,
                                                         cosmo::MsgBaseFaceLibInfo&& info) {
    return face_manager_->UpdateFaceLib(faceLibId, std::move(info));
}

std::vector<cosmo::MsgResultFaceLibInfo> FaceLibServiceImpl::RemoveFaceLib(
    const std::vector<std::string>& libIdList) {
    return face_manager_->RemoveFaceLib(libIdList);
}

std::vector<cosmo::FaceLibPtr> FaceLibServiceImpl::GetFaceLibs(std::vector<std::string> libIds) const {
    return face_manager_->GetFaceLibs(std::move(libIds));
}

cosmo::FaceLibPtr FaceLibServiceImpl::GetFaceLib(const std::string& libId) const {
    return face_manager_->GetFaceLib(libId);
}

cosmo::FaceLibPtr FaceLibServiceImpl::CreateFaceLib(cosmo::MsgBaseFaceLibInfo&& data, std::string& outId) {
    auto lib = std::make_shared<cosmo::FaceLib>(std::move(data));
    outId    = lib->GetId();
    return lib;
}

// ---- FaceManager: person management ----
bool FaceLibServiceImpl::IsValidSerialNumber(const std::string& personId, const std::string& serialNumber) {
    return face_manager_->IsValidSerialNumber(personId, serialNumber);
}

cosmo::PersonPtr FaceLibServiceImpl::GetPerson(const std::string& personId) const {
    return face_manager_->GetPerson(personId);
}

std::vector<cosmo::PersonPtr> FaceLibServiceImpl::GetAllPerson() const {
    return face_manager_->GetAllPerson();
}

void FaceLibServiceImpl::AddPerson(cosmo::FaceLibPtr faceLib, cosmo::PersonPtr spPerson) {
    face_manager_->AddPerson(std::move(faceLib), std::move(spPerson));
}

void FaceLibServiceImpl::UpdatePerson(std::vector<cosmo::FaceLibPtr> faceLibList, cosmo::PersonPtr spPerson) {
    face_manager_->UpdatePerson(std::move(faceLibList), std::move(spPerson));
}

std::string FaceLibServiceImpl::SetQueryCond(const cosmo::MsgQueryFacesR& queryCond) {
    return face_manager_->SetQueryCond(queryCond);
}

cosmo::util::ErrorEnum FaceLibServiceImpl::RemoveAllPerson(const std::string& faceLibId) {
    return face_manager_->RemoveAllPerson(faceLibId);
}

std::vector<cosmo::MsgResultInfo> FaceLibServiceImpl::RemovePerson(
    cosmo::FaceLibPtr faceLib, const std::vector<std::string>& personIdList) {
    return face_manager_->RemovePerson(std::move(faceLib), personIdList);
}

// ---- Person factory & mutation (IPersonRepo extensions) ----

cosmo::PersonPtr FaceLibServiceImpl::CreatePerson() {
    return std::make_shared<cosmo::Person>(std::string{});
}

std::string FaceLibServiceImpl::GetPersonId(const cosmo::PersonPtr& person) const {
    return person->GetId();
}

size_t FaceLibServiceImpl::GetPersonPictureCount(const cosmo::PersonPtr& person) const {
    return person->GetPictureCount();
}

int64_t FaceLibServiceImpl::GetPersonCreateTime(const cosmo::PersonPtr& person) const {
    return person->GetCreateTime();
}

std::vector<cosmo::FacePicPtr> FaceLibServiceImpl::GetPersonPictures(const cosmo::PersonPtr& person) const {
    return person->GetPictures();
}

bool FaceLibServiceImpl::IsPersonInFaceLibs(const cosmo::PersonPtr& person,
                                            const std::vector<std::string>& libIds) const {
    return person->IsInFaceLibs(libIds);
}

void FaceLibServiceImpl::UpdatePersonMetadata(cosmo::PersonPtr person, const std::string& name,
                                              const std::string& serialNumber, int64_t updateTime,
                                              int64_t createTime) {
    if (!name.empty()) {
        person->SetName(name);
    }
    if (!serialNumber.empty()) {
        person->SetSerialNumber(serialNumber);
    }
    person->SetUpdateTime(updateTime);
    if (person->GetCreateTime() == 0) {
        person->SetCreateTime(createTime);
    }
}

void FaceLibServiceImpl::AddPersonPicture(cosmo::PersonPtr person, cosmo::FacePicPtr pic) {
    person->AddPicture(std::move(pic));
}

void FaceLibServiceImpl::RemovePersonPicture(cosmo::PersonPtr person, const std::string& picId) {
    person->RemovePicture(picId);
}

cosmo::FacePicPtr FaceLibServiceImpl::CreateFacePic(const std::string& id, const std::string& path,
                                                    const cosmo::AiFeature& feature) {
    return std::make_shared<cosmo::FacePic>(id, path, feature);
}

// ---- Face feature ----

cosmo::util::ErrorEnum FaceLibServiceImpl::ExtractFaceFeature(VideoFramePtr& image, float quality,
                                                              cosmo::AiFeature& feature,
                                                              VideoFramePtr& cutImage) {
    OperationGuard operation(*this);
    if (!operation) {
        return cosmo::util::ErrorEnum::ServiceNotInit;
    }
    auto ec = EnsureFaceFeature()->HandFeatureImage(image, quality, feature, cutImage);
    return static_cast<cosmo::util::ErrorEnum>(ec.value());
}

float FaceLibServiceImpl::CalculateFaceScore(const cosmo::AiFeature& f1, const cosmo::AiFeature& f2) {
    OperationGuard operation(*this);
    if (!operation) {
        return 0.0F;
    }
    return EnsureFaceFeature()->CalculateScore(f1, f2);
}

float FaceLibServiceImpl::GetFaceScore(float distance) {
    OperationGuard operation(*this);
    if (!operation) {
        return 0.0F;
    }
    return EnsureFaceFeature()->GetScore(distance);
}

bool FaceLibServiceImpl::FaceCompare(std::vector<std::string> sets, cosmo::AiFeature& feature,
                                     cosmo::AiDetectMatchHighScoreInfo& info, float param_limit_score) {
    OperationGuard operation(*this);
    if (!operation) {
        return false;
    }
    return face_manager_->FaceCompare(std::move(sets), feature, info, param_limit_score);
}

void FaceLibServiceImpl::LoadFaceData() {
    face_manager_->Load();
}

void FaceLibServiceImpl::ReleaseFaceModels() {
    OperationGuard operation(*this);
    if (!operation) {
        return;
    }
    std::shared_ptr<cosmo::FaceFeatureExtractor> feature;
    {
        std::lock_guard<std::mutex> lock(face_feature_mutex_);
        feature = std::move(face_feature_);
    }
    if (feature) {
        LOG_INFO("{}", "FaceLibServiceImpl: releasing face feature models to free VRAM");
        feature->Stop();
    }
}

// ---- Person import ----
void FaceLibServiceImpl::Stop() {
    StopImpl();
}

void FaceLibServiceImpl::StopImpl() {
    std::lock_guard<std::mutex> stop_lock(stop_mutex_);
    {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        stopped_ = true;
    }

    // PersonImport may call IFaceFeature while completing. Do not hold the
    // operation mutex while waiting for it.
    person_import_->Stop();

    {
        std::unique_lock<std::mutex> lock(operation_mutex_);
        operation_cv_.wait(lock, [this]() { return active_operations_ == 0; });
    }

    std::shared_ptr<cosmo::FaceFeatureExtractor> feature;
    {
        std::lock_guard<std::mutex> lock(face_feature_mutex_);
        feature = std::move(face_feature_);
    }
    if (feature) {
        feature->Stop();
    }
}

void FaceLibServiceImpl::ImportFile(const std::string& filePath, const std::string& faceLibId) {
    OperationGuard operation(*this);
    if (!operation) {
        throw util::ErrorMessage(util::ErrorEnum::ServiceNotInit, "Face service is shutting down");
    }
    if (!person_import_->ImportFile(filePath, faceLibId)) {
        throw util::ErrorMessage(util::ErrorEnum::ResourceLimit,
                                 "A person import operation is already in progress");
    }
}

std::pair<int, int> FaceLibServiceImpl::GetImportStatus() const {
    return person_import_->GetStatus();
}

bool FaceLibServiceImpl::ImportComplete() const {
    return person_import_->Complete();
}

int FaceLibServiceImpl::GetImportTotalCount() const {
    return person_import_->GetTotalCount();
}

std::string FaceLibServiceImpl::GetImportFailedUrl() const {
    return person_import_->GetFailedUrl();
}

}  // namespace cosmo::service
