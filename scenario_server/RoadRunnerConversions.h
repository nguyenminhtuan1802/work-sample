#pragma once

#include "mw_grpc_clients/scenario_server/ProtobufNamespace.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/common.hpp>
#include <glm/mat4x4.hpp>

#include <memory>
#include <stdexcept>
#include <optional>
#include <future>
#include <array>
#include <cmath>
#include <cfloat>
#include <iostream>

#include <boost/lexical_cast.hpp>

template <typename T>
using rrSP = std::shared_ptr<T>;

template <typename T>
using rrUP = std::unique_ptr<T>;

#define VZ_DECLARE_NONCOPYABLE(ClassName)            \
    ClassName(const ClassName&)            = delete; \
    ClassName& operator=(const ClassName&) = delete

#define VZ_FORWARD_DECLARE(innerNamespace, symbol) \
    namespace innerNamespace {                     \
    symbol;                                        \
    } // namespaces

// Unused variable
#define VZ_UNUSED(x) (void)(x)

// No return statement
#define VZ_NO_RETURN [[noreturn]]

// Empty (no-op) statement
#define VZ_NO_OP() (void)(0)

template <typename T>
using rrOpt = std::optional<T>;

class rrException : public std::runtime_error {
  public:
    rrException()
        : runtime_error("No message set.")
        , mMsg("No message.") {}

    rrException(const std::string& msg)
        : runtime_error("No message set.")
        , mMsg(msg)
        , mHasMsg(true) {}

    bool HasCustomMessage() const {
        return mHasMsg;
    }

    const std::string ToString() const {
        return mMsg;
    }

  private:
    std::string mMsg;
    bool        mHasMsg = false;
};

#define rrVerify(expression) assert((expression))


#define rrThrowVerify(condition, message) \
    ((condition) ? void(0) : throw rrException((message)))

#define rrThrow(message) \
    throw rrException(message)

////////////////////////////////////////////////////////////////////////////////

inline void rrWarning(const std::string& message) {
    std::cout << "Warning: " << message << std::endl;
}

////////////////////////////////////////////////////////////////////////////////

inline void rrDev(const std::string& message) {
    std::cout << "Info: " << message << std::endl;
}

////////////////////////////////////////////////////////////////////////////////

inline void rrPrint(const std::string& message) {
    std::cout << "Info: " << message << std::endl;
}

////////////////////////////////////////////////////////////////////////////////

inline void rrError(const std::string& message) {
    std::cout << "Error: " << message << std::endl;
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
bool isRunning(std::future<T> const& f) {
    if (f.valid()) {
        return f.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    } else {
        return false;
    }
}

////////////////////////////////////////////////////////////////////////////////

static constexpr double cDoubleEpsilon = 1e-12;
static constexpr float  cFloatEpsilon  = 1e-6f;

////////////////////////////////////////////////////////////////////////////////

inline bool EpsilonEqual(double first, double second, double epsilon = cDoubleEpsilon) {
    return abs(first - second) < epsilon;
}

////////////////////////////////////////////////////////////////////////////////

template <typename T, typename U>
T rrDynamicCast(U* ptr) {
    return dynamic_cast<T>(ptr);
}

////////////////////////////////////////////////////////////////////////////////

template <typename T, typename U>
std::shared_ptr<T> rrDynamicCast(const rrSP<U>& ptr) {
    return std::dynamic_pointer_cast<T>(ptr);
}

////////////////////////////////////////////////////////////////////////////////

void SetVelocity(sseProto::Actor& actor, double speed);

////////////////////////////////////////////////////////////////////////////////

namespace Util {
static constexpr double cMinStepSize = 0.00001;
static constexpr double cMaxStepSize = 10.0;

// Define the min, max constants for max simulation time
static constexpr double cMaxSimulationTimeLowerBound = 0.0;
static constexpr double cMaxSimulationTimeUpperBound = DBL_MAX;

// Define the min, max constants for pacing
static constexpr double cPacingLowerBound = 0.05000000;
static constexpr double cPacingUpperBound = 20;

void SetVelocity(sseProto::ActorRuntime& actorRuntime, double speed);

void SetVelocity(sseProto::Actor& actor, double speed);

double GetSpeed(const sseProto::ActorRuntime& actorRuntime);

double GetSpeed(const sseProto::Actor& actor);

void CheckSimulationStepSize(const double stepSize);

void CheckSimulationPace(const double pace);

void CheckMaxSimulationTime(const double simTime);

/*void SetWorldCoordinates(sseProto::ActorRuntime& actor, const sseProto::ActorRuntime& parent)
void SetLocalCoordinates(sseProto::ActorRuntime& actor, const sseProto::ActorRuntime& parent)*/
} // namespace Util

////////////////////////////////////////////////////////////////////////////////

namespace geometry {

////////////////////////////////////////////////////////////////////////////////

template <typename Ty>
class Vector3 {
  public:
    // default constructor
    Vector3<Ty>()
        : mVector({(Ty)0., (Ty)0., (Ty)0.}) {}

    // constructor with x, y, z
    Vector3<Ty>(const Ty x, const Ty y, const Ty z)
        : mVector({x, y, z}) {}

    // constructor from another
    Vector3<Ty>(const std::array<Ty, 3>& vec)
        : mVector(vec) {}

    // constructor from proto common vector
    Vector3<Ty>(const sseProtoCommon::Vector3& vec)
        : mVector({vec.x(), vec.y(), vec.z()}) {}

    Vector3<Ty> operator-(const Vector3<Ty>& other) const {
        return Vector3<Ty>(mVector[0] - other[0], mVector[1] - other[1], mVector[2] - other[2]);
    }

    Vector3<Ty> operator+(const Vector3<Ty>& other) const {
        return Vector3<Ty>(mVector[0] + other[0], mVector[1] + other[1], mVector[2] + other[2]);
    }

    Vector3<Ty>& operator=(const Vector3<Ty>& other) {
        mVector[0] = other[0];
        mVector[1] = other[1];
        mVector[2] = other[2];

        return *this;
    }

    // Copy constructor
    Vector3(const Vector3& other) {
        mVector[0] = other[0];
        mVector[1] = other[1];
        mVector[2] = other[2];
    }

    Ty& operator[](const size_t idx) {
        return mVector[idx];
    }

    const Ty& operator[](const size_t idx) const {
        return mVector[idx];
    }

    Ty getX() const {
        return mVector[0];
    }

    Ty getY() const {
        return mVector[1];
    }

    Ty getZ() const {
        return mVector[2];
    }

    operator std::array<double, 3>&() {
        return mVector;
    }

  private:
    std::array<Ty, 3> mVector = {(Ty)0, (Ty)0, (Ty)0};
};

////////////////////////////////////////////////////////////////////////////////
// Multiply by scalar
template <typename R>
Vector3<R> operator*(R scalar, Vector3<R> const& vec) {
    return Vector3<R>(scalar * vec.getX(), scalar * vec.getY(), scalar * vec.getZ());
}

////////////////////////////////////////////////////////////////////////////////
// Multiply by scalar
template <typename R>
std::array<double, 3> operator*(R scalar, const std::array<double, 3>& vec) {
    std::array<double, 3> returnVal = {scalar * vec[0], scalar * vec[1], scalar * vec[2]};
    return returnVal;
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
class OrientedBox3 {
  public:
    // Default constructor: Sets the center of the box as origin
    // and the default orientation as identity
    OrientedBox3() = default;

    // Set the center
    void setCenter(const std::array<T, 3>& c) {
        mCenter = c;
    }

    void setCenter(const sseProtoCommon::Vector3& c) {
        mCenter[0] = c.x();
        mCenter[1] = c.y();
        mCenter[2] = c.z();
    }

    std::array<T, 3> getCenter() const {
        return mCenter;
    }

    // Set the axes
    void setAxes(const std::array<std::array<T, 3>, 3>& a) {
        mAxes = a;
    }

    void setAxes(const sseProtoCommon::Matrix4x4& m) {
        mAxes[0] = {m.col0().x(), m.col0().y(), m.col0().z()};
        mAxes[1] = {m.col1().x(), m.col1().y(), m.col1().z()};
        mAxes[2] = {m.col2().x(), m.col2().y(), m.col2().z()};
    }

    std::array<T, 3> getAxis(int i) const {
        return mAxes[i];
    }

    // Set the extents
    void setExtents(const std::array<T, 3>& e) {
        mExtents = e;
    }

    void setExtents(const sseProtoCommon::Vector3& min, const sseProtoCommon::Vector3& max) {
        mExtents = {std::abs(max.x() - min.x() / 2.), std::abs(max.y() - min.y() / 2.), std::abs(max.z() - min.z() / 2.)};
    }

    std::array<T, 3> getExtents() const {
        return mExtents;
    }

    // get & set min & max
    std::array<T, 3> getMin() const {
        return mMin;
    }

    void setMin(const std::array<T, 3>& min) {
        mMin = min;
    }

    std::array<T, 3> getMax() const {
        return mMax;
    }

    void setMax(const std::array<T, 3>& max) {
        mMax = max;
    }

    // get vertices of the bounding box
    void GetVertices(std::array<Vector3<double>, 8>& v) const {
        double          sign(1.0);
        Vector3<double> center(mCenter);
        Vector3<double> axis0(mAxes[0]);
        Vector3<double> axis1(mAxes[1]);
        Vector3<double> axis2(mAxes[2]);

        for (size_t ii = 0; ii < 2; ++ii) {
            // (x, y, z) --> (-x, -y, -z)
            Vector3<double> res = center + (sign * (mExtents[0] * axis0 + mExtents[1] * axis1 + mExtents[2] * axis2));
            v[4 * ii + 0]       = {res[0], res[1], res[2]};
            // (-x, y, z) --> (x, -y, -z)
            res           = center + (sign * (-mExtents[0] * axis0 + mExtents[1] * axis1 + mExtents[2] * axis2));
            v[4 * ii + 1] = {res[0], res[1], res[2]};
            // (x, -y, z) --> (-x, y, -z)
            res           = center + (sign * (mExtents[0] * axis0 - mExtents[1] * axis1 + mExtents[2] * axis2));
            v[4 * ii + 2] = {res[0], res[1], res[2]};
            // (-x, -y, z) --> (x, y, -z)
            res           = center + (sign * (-mExtents[0] * axis0 - mExtents[1] * axis1 + mExtents[2] * axis2));
            v[4 * ii + 3] = {res[0], res[1], res[2]};

            sign = -1.0;
        }
    }

  private:
    // Center of the box
    std::array<T, 3> mCenter = {(T)0, (T)0, (T)0};

    // Axes must be orthonormal
    std::array<std::array<T, 3>, 3> mAxes = {
        {{(T)1, (T)0, (T)0},
         {(T)0, (T)1, (T)0},
         {(T)0, (T)0, (T)1}}
    };

    // Extents along each of the axes
    std::array<T, 3> mExtents = {(T)1, (T)1, (T)1};

    // min and max of the bounding box (default: degenerate box)
    std::array<T, 3> mMin = {(T)0, (T)0, (T)0};
    std::array<T, 3> mMax = {(T)0, (T)0, (T)0};
};

////////////////////////////////////////////////////////////////////////////////

template <typename T>
double Dot(const std::array<T, 3>& a, const std::array<T, 3>& b) {
    return (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
}

////////////////////////////////////////////////////////////////////////////////

double Dot(const Vector3<double>& a, const Vector3<double>& b);

////////////////////////////////////////////////////////////////////////////////

class PolyLine {
  public:
    void AddPoint(const Vector3<double>& point, double s = 0.0) {
        mPoints.push_back(point);
    }

    void ParametrizeArcLength() {}

  private:
    std::vector<Vector3<double>> mPoints;
};

////////////////////////////////////////////////////////////////////////////////

OrientedBox3<double> GetOrientedBoundingBox(const sseProto::Actor& actor);

////////////////////////////////////////////////////////////////////////////////

bool InContainer(const std::array<double, 3>& point, const OrientedBox3<double>& box);

////////////////////////////////////////////////////////////////////////////////

class DCPQuery {
  public:
    struct Result {
        double          distance = 0., squareOfDistance = 0.;
        Vector3<double> closestPoint = {0., 0., 0.};
    };

    ////////////////////////////////////////////////////////////////////////////////

    Result operator()(Vector3<double> const& point, OrientedBox3<double> const& box) const;

  protected:
    void ComputeClosestPoint(Vector3<double>& point, Vector3<double> const& boxExtent, Result& result) const;
};

////////////////////////////////////////////////////////////////////////////////

DCPQuery::Result DCPQueryAny(Vector3<double> const& point, OrientedBox3<double> const& box);

////////////////////////////////////////////////////////////////////////////////

bool TIQuery(OrientedBox3<double> const& box0, OrientedBox3<double> const& box1);

////////////////////////////////////////////////////////////////////////////////

class ParallelConfigurations {
  public:
    struct OrientedBox3OrientedBox3Result {
        OrientedBox3OrientedBox3Result()
            : mDistance(0.0)
            , mPoints({
                  {{0., 0., 0.}, {0., 0., 0.}}
        }) {}

        double                         mDistance;
        std::array<Vector3<double>, 2> mPoints;
    };

    ////////////////////////////////////////////////////////////////////////////////

    static OrientedBox3OrientedBox3Result FindDistance(const geometry::OrientedBox3<double>& box1,
                                                       const geometry::OrientedBox3<double>& box2);

    ////////////////////////////////////////////////////////////////////////////////

    static bool CheckParallel(const OrientedBox3<double>& box1,
                              const OrientedBox3<double>& box2);
};

} // namespace geometry

////////////////////////////////////////////////////////////////////////////////

template <typename ToT, typename FromT, typename Enable = void>
struct rrConversion;

////////////////////////////////////////////////////////////////////////////////

template <typename ToT, typename FromT, typename... ArgsT>
ToT rrConvert(const FromT& from, ArgsT&&... args) {
    return rrConversion<ToT, FromT>{}(from, std::forward<ArgsT>(args)...);
}

////////////////////////////////////////////////////////////////////////////////
// Convenience macro for defining common-case conversions
//	(i.e. no additional parameters, and FromT is taken by const ref)
//	- Example: VZ_CONVERSION(float, long) { return (float)from; }
#define VZ_CONVERSION(ToType, FromType)                                       \
    template <>                                                               \
    struct rrConversion<ToType, FromType> {                                   \
        using FromT = FromType;                                               \
        using ToT   = ToType;                                                 \
        /*Note: 'const&' after type to handle const pointer types correctly*/ \
        ToType operator()(FromType const& from) const;                        \
    };                                                                        \
                                                                              \
    inline ToType rrConversion<ToType, FromType>::operator()(FromType const& from) const

////////////////////////////////////////////////////////////////////////////////

template <>
inline geometry::Vector3<double> rrConvert(const sseProtoCommon::Vector3& vec) {
    return geometry::Vector3<double>(vec.x(), vec.y(), vec.z());
}

////////////////////////////////////////////////////////////////////////////////

template <typename T>
T rrConvert(const std::string& val) {
    T ret;
    try {
        ret = boost::lexical_cast<T>(val);
    } catch (const boost::bad_lexical_cast& e) {
        rrThrow(e.what());
    }

    return ret;
}

////////////////////////////////////////////////////////////////////////////////

VZ_CONVERSION(glm::dvec4, mathworks::scenario::common::Vector4) {
    return {from.x(), from.y(), from.z(), from.w()};
}

////////////////////////////////////////////////////////////////////////////////

VZ_CONVERSION(glm::dmat4, mathworks::scenario::common::Matrix4x4) {
    return {
        rrConvert<glm::dvec4>(from.col0()),
        rrConvert<glm::dvec4>(from.col1()),
        rrConvert<glm::dvec4>(from.col2()),
        rrConvert<glm::dvec4>(from.col3())};
}

////////////////////////////////////////////////////////////////////////////////

VZ_CONVERSION(mathworks::scenario::common::Vector4, glm::dvec4) {
    mathworks::scenario::common::Vector4 point;

    point.set_x(from.x);
    point.set_y(from.y);
    point.set_z(from.z);
    point.set_w(from.w);

    return point;
}

////////////////////////////////////////////////////////////////////////////////

VZ_CONVERSION(mathworks::scenario::common::Matrix4x4, glm::dmat4) {
    mathworks::scenario::common::Matrix4x4 matrix;

    *matrix.mutable_col0() = rrConvert<mathworks::scenario::common::Vector4>(from[0]);
    *matrix.mutable_col1() = rrConvert<mathworks::scenario::common::Vector4>(from[1]);
    *matrix.mutable_col2() = rrConvert<mathworks::scenario::common::Vector4>(from[2]);
    *matrix.mutable_col3() = rrConvert<mathworks::scenario::common::Vector4>(from[3]);

    return matrix;
}

////////////////////////////////////////////////////////////////////////////////

namespace stdx {
// Appends a range of values to the end of container
template <typename ContainerT, typename IteratorT>
void Append(ContainerT& container, const IteratorT& begin, const IteratorT& end) {
    container.insert(container.end(), begin, end);
}

////////////////////////////////////////////////////////////////////////////////
// Appends a container of values to the end of container
template <typename ContainerT, typename OtherContainerT>
void Append(ContainerT& container, const OtherContainerT& otherContainer) {
    Append(container, otherContainer.begin(), otherContainer.end());
}

////////////////////////////////////////////////////////////////////////////////
template <typename ContainerType, typename Val>
bool ContainsKey(const ContainerType& container, const Val& value) {
    return (container.find(value) != container.end());
}
} // namespace stdx

////////////////////////////////////////////////////////////////////////////////
