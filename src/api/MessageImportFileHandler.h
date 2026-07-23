#pragma once
#include <system_error>

#include "service/path/dto/ImportFileDto.h"
#include "util/IRequestDispatcher.h"

namespace cosmo {

class MessageImportFileHandler {
public:
    MessageImportFileHandler() = default;
    service::MsgImportFileSend Handle(service::MsgImportFileRecv&& data, std::error_condition& errc);
    service::MsgImportFileSend Handle(service::MsgImportFileRecv&& data,
                                      const RequestDispatchContext& context, std::error_condition& errc);
    service::MsgQueryImportStatusSend Handle(service::MsgQueryImportStatusRecv&& data,
                                             std::error_condition& errc);
};

}  // namespace cosmo
