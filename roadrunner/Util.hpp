#pragma once

#include <foundation/log/diagnostic.hpp>

namespace {
foundation::log::diagnostic_logger logger_("roadrunnerservice_logger");
}

namespace grpc {
class Status;
}

namespace roadrunnerapi {
static const int cDefaultApiPort{35707};
static const int cDefaultTestConnectionPort{35708};
static const int cMinPort{1024};
static const int cMaxPort{65535};

class Util {
  public:
    [[noreturn]] static void ReportGrpcError(const grpc::Status& status);
};
} // namespace roadrunnerapi
