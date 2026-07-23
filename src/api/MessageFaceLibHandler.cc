// MessageFaceLibHandler — Message Face Lib Handler implementation.

#include "api/MessageFaceLibHandler.h"

#include <algorithm>
#include <iterator>

#include "db/TransactionGuard.h"
#include "service/face/IFaceLibRepo.h"
#include "service/face/IPersonDaoService.h"
#include "service/face/IPersonRepo.h"
#include "util/ErrorCode.h"
#include "util/Exception.h"

namespace cosmo {

static constexpr const char* kTag = "FaceLibHandler";

namespace {
    MsgResultFaceLibInfo MakeFaceLibFailure(const std::string& id, util::ErrorEnum error) {
        MsgResultFaceLibInfo result{};
        result.failedFaceLibId = id;
        result.resCode         = static_cast<int>(error);
        result.resMsg          = make_error_condition(error).message();
        return result;
    }

    MsgResultInfo MakePersonFailure(const std::string& id, util::ErrorEnum error) {
        MsgResultInfo result{};
        result.id      = id;
        result.resCode = static_cast<int>(error);
        result.resMsg  = make_error_condition(error).message();
        return result;
    }
}  // namespace

MessageFaceLibHandler::MessageFaceLibHandler(service::IFaceLibRepo& lib_repo,
                                             service::IPersonRepo& person_repo,
                                             service::IFaceFeature& face_feature,
                                             service::IPersonDaoService& dao_svc,
                                             service::IFaceLibService& face_lib_svc,
                                             service::IVideoFrameCodec& codec)
    : lib_repo_(lib_repo),
      person_repo_(person_repo),
      face_feature_(face_feature),
      dao_svc_(dao_svc),
      face_lib_svc_(face_lib_svc),
      codec_(codec) {}

// Face lib delete
Lib::MsgDeleteFaceLibSend MessageFaceLibHandler::Handle(Lib::MsgDeleteFaceLibRecv&& data,
                                                        std::error_condition& /*errc*/) {
    Lib::MsgDeleteFaceLibSend retData{};

    std::vector<std::string> vec;
    vec.reserve(data.faceLibIdList.size());
    std::transform(data.faceLibIdList.begin(), data.faceLibIdList.end(), std::back_inserter(vec),
                   [](const auto& ref) { return ref.ToString(); });

    for (const auto& id : vec) {
        db::TransactionGuard<service::IPersonDaoService> guard(dao_svc_);
        if (!dao_svc_.RemoveFaceLib(id)) {
            retData.resData.failedFaceLibList.push_back(
                MakeFaceLibFailure(id, util::ErrorEnum::DatabaseFailed));
            continue;
        }

        auto results = lib_repo_.RemoveFaceLib({id});
        if (results.size() != 1) {
            retData.resData.failedFaceLibList.push_back(
                MakeFaceLibFailure(id, util::ErrorEnum::InternalError));
            continue;
        }
        auto& result = results.front();
        if (static_cast<util::ErrorEnum>(result.resCode) != util::ErrorEnum::Success) {
            retData.resData.failedFaceLibList.push_back(result);
            continue;
        }
        guard.Commit();
    }
    return retData;
}

// Face lib person delete
Lib::MsgDeletePersonSend MessageFaceLibHandler::Handle(Lib::MsgDeletePersonRecv&& data,
                                                       std::error_condition& errc) {
    Lib::MsgDeletePersonSend retData{};
    if (data.removeAll) {
        if (!data.faceLibId.empty() && !lib_repo_.GetFaceLib(data.faceLibId)) {
            errc = util::ErrorEnum::NoSuchId;
            return retData;
        }

        std::vector<std::string> library_ids;
        if (!data.faceLibId.empty()) {
            library_ids.push_back(data.faceLibId);
        } else {
            db::FaceLibQueryCondition cond{};
            constexpr int kMaxQueryPageSize = 10000;
            cond.page_size                  = kMaxQueryPageSize;
            auto db_result                  = dao_svc_.QueryFaceLib(cond);
            library_ids.reserve(db_result.face_lib_list.size());
            std::transform(db_result.face_lib_list.begin(), db_result.face_lib_list.end(),
                           std::back_inserter(library_ids), [](const auto& face_lib) { return face_lib.id; });
        }

        db::TransactionGuard<service::IPersonDaoService> guard(dao_svc_);
        for (const auto& id : library_ids) {
            if (!dao_svc_.ClearFaceLib(id)) {
                throw util::ErrorMessage(util::ErrorEnum::DatabaseFailed);
            }
        }
        errc = person_repo_.RemoveAllPerson(data.faceLibId);
        if (errc == util::ErrorEnum::Success) {
            guard.Commit();
        }
    } else {
        auto face_lib = data.faceLibId.empty() ? FaceLibPtr{} : lib_repo_.GetFaceLib(data.faceLibId);
        if (!data.faceLibId.empty() && !face_lib) {
            for (const auto& id : data.personIdList) {
                retData.failedList.push_back(MakePersonFailure(id, util::ErrorEnum::NoSuchId));
            }
            return retData;
        }

        for (const auto& id : data.personIdList) {
            db::TransactionGuard<service::IPersonDaoService> guard(dao_svc_);
            const bool db_ok =
                face_lib ? dao_svc_.RemovePersonFromFaceLib(id, data.faceLibId) : dao_svc_.RemovePerson(id);
            if (!db_ok) {
                retData.failedList.push_back(MakePersonFailure(id, util::ErrorEnum::DatabaseFailed));
                continue;
            }

            auto results = person_repo_.RemovePerson(face_lib, {id});
            if (results.size() != 1) {
                retData.failedList.push_back(MakePersonFailure(id, util::ErrorEnum::InternalError));
                continue;
            }
            auto& result = results.front();
            if (static_cast<util::ErrorEnum>(result.resCode) != util::ErrorEnum::Success) {
                retData.failedList.push_back(result);
                continue;
            }
            guard.Commit();
        }
    }
    return retData;
}

}  // namespace cosmo
