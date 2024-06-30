#include "Util.hpp"
#include "resources/roadrunnerapi/roadrunnerservice.hpp"
#include <fl/except/MsgIDException.hpp>
#include <grpcpp/grpcpp.h>

using grpc::Status;

namespace roadrunnerapi {

[[noreturn]] void Util::ReportGrpcError(const Status& status) {
    FOUNDATION_LOG_INFO(logger_, << "Call resulted in gRPC error code "
                                 << status.error_code()
                                 << " with the message: " << status.error_message());
    if (status.error_code() == 14) {
        throw fl::except::MakeException(roadrunnerapi::roadrunnerservice::unavailableError());
    } else {
        throw fl::except::MakeException(
            roadrunnerapi::roadrunnerservice::apiCallError(status.error_message()));
    }
}

} // namespace roadrunnerapi
