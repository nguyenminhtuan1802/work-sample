/* Copyright 2023-2024 The MathWorks, Inc.

FlexTreeService calls the Flex Tree API using the gRPC stub.

*/
#pragma once

#include "mw_grpc_clients/mw_grpc_clients_spec.hpp"

#include <string>
#include <memory>

namespace mathworks::flex::tree {
class FlexFindObjectsRequest;
class FlexFindObjectsResponse;
class FlexFindPropertiesRequest;
class FlexFindPropertiesResponse;
class FlexFilterObjectsRequest;
class FlexFilterObjectsResponse;
class FlexFilterPropertiesRequest;
class FlexFilterPropertiesResponse;
class FlexGetObjectDetailsRequest;
class FlexGetObjectDetailsResponse;
class FlexGetPropertyDetailsRequest;
class FlexGetPropertyDetailsResponse;
class FlexGetPropertyValuesRequest;
class FlexGetPropertyValuesResponse;
class FlexSetPropertyValuesRequest;
class FlexSetPropertyValuesResponse;
class FlexCallObjectMethodsRequest;
class FlexCallObjectMethodsResponse;
class FlexGetObjectTypeDetailsRequest;
class FlexGetObjectTypeDetailsResponse;
class FlexGetObjectTypesRequest;
class FlexGetObjectTypesResponse;
class FlexGetEnumerationsRequest;
class FlexGetEnumerationsResponse;
class FlexGetEnumerationDetailsRequest;
class FlexGetEnumerationDetailsResponse;
} // namespace mathworks::flex::tree

namespace flexProto = mathworks::flex::tree;

namespace datamodelapi {

struct DataModelTreeServiceGrpcContext;

class MW_GRPC_CLIENTS_EXPORT_CLASS DataModelTreeServiceClient final {
  public:
    DataModelTreeServiceClient();
    ~DataModelTreeServiceClient();

    void Connect(int apiPort = 35707);

    double GetApiPort() const {
        return mApiPort;
    };

    bool IsServerReady() const;

    // Call a FlexTreeServiceClient method
    flexProto::FlexFindObjectsResponse           FindObjects(const flexProto::FlexFindObjectsRequest* request) const;
    flexProto::FlexFindPropertiesResponse        FindProperties(const flexProto::FlexFindPropertiesRequest* request) const;
    flexProto::FlexFilterObjectsResponse         FilterObjects(const flexProto::FlexFilterObjectsRequest* request) const;
    flexProto::FlexFilterPropertiesResponse      FilterProperties(const flexProto::FlexFilterPropertiesRequest* request) const;
    flexProto::FlexGetObjectDetailsResponse      GetObjectDetails(const flexProto::FlexGetObjectDetailsRequest* request) const;
    flexProto::FlexGetPropertyDetailsResponse    GetPropertyDetails(const flexProto::FlexGetPropertyDetailsRequest* request) const;
    flexProto::FlexGetPropertyValuesResponse     GetPropertyValues(const flexProto::FlexGetPropertyValuesRequest* request) const;
    flexProto::FlexSetPropertyValuesResponse     SetPropertyValues(const flexProto::FlexSetPropertyValuesRequest* request) const;
    flexProto::FlexCallObjectMethodsResponse     CallObjectMethods(const flexProto::FlexCallObjectMethodsRequest* request) const;
    flexProto::FlexGetObjectTypeDetailsResponse  GetObjectTypeDetails(const flexProto::FlexGetObjectTypeDetailsRequest* request) const;
    flexProto::FlexGetObjectTypesResponse        GetObjectTypes(const flexProto::FlexGetObjectTypesRequest* request) const;
    flexProto::FlexGetEnumerationsResponse       GetEnumerations(const flexProto::FlexGetEnumerationsRequest* request) const;
    flexProto::FlexGetEnumerationDetailsResponse GetEnumerationDetails(const flexProto::FlexGetEnumerationDetailsRequest* request) const;

  private:
    std::unique_ptr<DataModelTreeServiceGrpcContext> mGrpcContext;
    int                                              mApiPort;
};

} // namespace datamodelapi
