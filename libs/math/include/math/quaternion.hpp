/*
    Copyright (C) 2026 Matej Gomboc https://github.com/MatejGomboc/tron-grid

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#pragma once

#include "math/matrix.hpp"
#include "math/vector.hpp"
#include <cmath>

namespace MathLib
{

    //! Quaternion for 3D rotations — no gimbal lock, smooth interpolation via slerp.
    struct Quat {
        float w{1.0f}; //!< Scalar (real) part. Default is identity rotation.
        float x{0.0f}; //!< X component of the vector (imaginary) part.
        float y{0.0f}; //!< Y component of the vector (imaginary) part.
        float z{0.0f}; //!< Z component of the vector (imaginary) part.

        //! Returns the identity quaternion (no rotation).
        [[nodiscard]] static constexpr Quat identity()
        {
            return {1.0f, 0.0f, 0.0f, 0.0f};
        }

        //! Creates a quaternion from an axis and angle (radians).
        [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, float angle_radians)
        {
            Vec3 a = axis.normalised();
            float half_angle = angle_radians * 0.5f;
            float s = std::sin(half_angle);
            return {std::cos(half_angle), a.x * s, a.y * s, a.z * s};
        }

        //! Returns the dot product of two quaternions.
        [[nodiscard]] constexpr float dot(const Quat& other) const
        {
            return w * other.w + x * other.x + y * other.y + z * other.z;
        }

        //! Returns the squared length.
        [[nodiscard]] constexpr float lengthSquared() const
        {
            return dot(*this);
        }

        //! Returns the length.
        [[nodiscard]] float length() const
        {
            return std::sqrt(lengthSquared());
        }

        //! Returns a normalised copy. Returns identity if length is zero.
        [[nodiscard]] Quat normalised() const
        {
            float len = length();
            if (len == 0.0f) {
                return identity();
            }
            float inv = 1.0f / len;
            return {w * inv, x * inv, y * inv, z * inv};
        }

        //! Multiplies two quaternions (combines rotations).
        [[nodiscard]] constexpr Quat operator*(const Quat& other) const
        {
            return {
                w * other.w - x * other.x - y * other.y - z * other.z,
                w * other.x + x * other.w + y * other.z - z * other.y,
                w * other.y - x * other.z + y * other.w + z * other.x,
                w * other.z + x * other.y - y * other.x + z * other.w,
            };
        }

        //! Rotates a Vec3 by this quaternion.
        [[nodiscard]] constexpr Vec3 rotate(const Vec3& v) const
        {
            // q * v * q^-1, optimised (avoids full quaternion multiply)
            Vec3 qv{x, y, z};
            Vec3 t = qv.cross(v) * 2.0f;
            return v + t * w + qv.cross(t);
        }

        //! Converts this quaternion to a 4x4 rotation matrix.
        [[nodiscard]] Mat4 toMat4() const
        {
            float xx = x * x;
            float yy = y * y;
            float zz = z * z;
            float xy = x * y;
            float xz = x * z;
            float yz = y * z;
            float wx = w * x;
            float wy = w * y;
            float wz = w * z;

            Mat4 result{};
            result.m[0][0] = 1.0f - 2.0f * (yy + zz);
            result.m[0][1] = 2.0f * (xy + wz);
            result.m[0][2] = 2.0f * (xz - wy);

            result.m[1][0] = 2.0f * (xy - wz);
            result.m[1][1] = 1.0f - 2.0f * (xx + zz);
            result.m[1][2] = 2.0f * (yz + wx);

            result.m[2][0] = 2.0f * (xz + wy);
            result.m[2][1] = 2.0f * (yz - wx);
            result.m[2][2] = 1.0f - 2.0f * (xx + yy);

            result.m[3][3] = 1.0f;
            return result;
        }

        //! Spherical linear interpolation between two quaternions.
        [[nodiscard]] static Quat slerp(const Quat& a, const Quat& b, float t)
        {
            float cos_theta = a.dot(b);

            // If the dot product is negative, negate one quaternion to take the short path.
            Quat b_adj = b;
            if (cos_theta < 0.0f) {
                cos_theta = -cos_theta;
                b_adj = {-b.w, -b.x, -b.y, -b.z};
            }

            // If quaternions are very close, use linear interpolation to avoid division by zero.
            if (cos_theta > 0.9995f) {
                return Quat{
                    a.w + t * (b_adj.w - a.w),
                    a.x + t * (b_adj.x - a.x),
                    a.y + t * (b_adj.y - a.y),
                    a.z + t * (b_adj.z - a.z),
                }
                    .normalised();
            }

            float theta = std::acos(std::min(cos_theta, 1.0f));
            float sin_theta = std::sin(theta);
            float wa = std::sin((1.0f - t) * theta) / sin_theta;
            float wb = std::sin(t * theta) / sin_theta;

            return {
                wa * a.w + wb * b_adj.w,
                wa * a.x + wb * b_adj.x,
                wa * a.y + wb * b_adj.y,
                wa * a.z + wb * b_adj.z,
            };
        }

        [[nodiscard]] constexpr bool operator==(const Quat& other) const = default;
    };

} // namespace MathLib
