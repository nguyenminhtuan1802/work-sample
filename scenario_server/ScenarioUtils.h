#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"
#include "RoadRunnerConversions.h"

#include <future>
#include <string>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/common.hpp>
#include <glm/mat4x4.hpp>

////////////////////////////////////////////////////////////////////////////////

// class OGRSpatialReference;
// class OGRCoordinateTransformation;

namespace rrProtoGrpc {
namespace mpProjectionUtil {

glm::dmat4 GetLLA2ECEFTransform(double latitude, double longitude, double altitude);

double GetFloatingPointRemainder(double u0, double u1);

double GetInverseTan(double u0, double u1);

glm::dmat4 GetLLA2ECEFTransform(double latitude, double longitude, double altitude);

glm::vec3 GetECEF2LLA(double ecefX, double ecefY, double ecefZ);

// class mpProjection {
//     VZ_DECLARE_NONCOPYABLE(mpProjection);
//
//   public:
//     mpProjection(rrSP<const OGRSpatialReference> spatialReference)
//         : SpatialReference(spatialReference) {}
//
//     rrSP<const OGRSpatialReference> GetSpatialReference() const {
//         return SpatialReference;
//     }
//
//     // Get the WKT string, returns empty string if no spatial reference.
//     std::string ToWkt() const;
//
//     // Get proj4 string
//     std::string ToProj4() const;
//
//   private:
//     rrSP<const OGRSpatialReference> SpatialReference;
// };

//// For now we support creation from wkt or proj4 strings
// rrSP<const mpProjection> CreateFromProjectionString(const std::string& projectionString);
//
//// Create from EPSG code
// rrSP<const mpProjection> CreateFromEPSGCode(const int epsgCode);
//
//// Create spatial reference from wkt or proj4 string
// rrSP<OGRSpatialReference> SpatialReferenceFromString(const std::string& projectionStdString);
//
// enum EnumEpsgCode {
//     eEpsgEgm1996 = 5773, // Geoidal height based on applying EGM96 geoid to WGS84 Ellipsoid
//     eEpsgWgs84   = 4326,
//     eEpsgNad83   = 4269,
//     eEpsgNadv88  = 5703,
//     eEpsgInvalid = -1,
// };
//
// rrSP<const mpProjection> GetStandardLatLong();

////////////////////////////////////////////////////////////////////////////////
// Allows projecting points from two projections through GDAL
// class mpProjectionTransform {
//  public:
//    ~mpProjectionTransform();
//    mpProjectionTransform(const mpProjection& source, const mpProjection& dest);
//
//    // Transforms geometry using CoordinateTransform
//    // - Can throw exceptions
//    // - Modifies input geometry directly
//    // void Transform(OGRGeometry& geom, bool reverse = false) const;
//
//    // Can throw exceptions
//    // void Transform(glm::dvec2& inoutSourcePoint, bool reverse = false) const override;
//    void Transform(glm::dvec3& inoutSourcePoint, bool reverse = false) const;
//
//    // Note - these are probably rather slow as they loop over points transforming them one at a time.
//    // This is due to OGR's bad transform interface.
//    // void Transform(std::vector<glm::dvec2>& inoutSourcePoints, bool reverse = false) const override;
//    // void Transform(std::vector<glm::dvec3>& inoutSourcePoints, bool reverse = false) const override;
//
//  private:
//    OGRCoordinateTransformation* CoordinateTransform        = nullptr;
//    OGRCoordinateTransformation* ReverseCoordinateTransform = nullptr;
//};

}
} // namespace rrProtoGrpc::mpProjectionUtil
