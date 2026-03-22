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

#include <cmath>

namespace MathLib
{

    //! Pi constant (3.14159...).
    constexpr float PI = 3.14159265358979323846f;

    //! 2D vector (UV coordinates, screen positions).
    struct Vec2 {
        float x{0.0f}; //!< X component.
        float y{0.0f}; //!< Y component.

        //! Returns the dot product of two vectors.
        [[nodiscard]] constexpr float dot(const Vec2& other) const
        {
            return x * other.x + y * other.y;
        }

        //! Returns the squared length (avoids sqrt).
        [[nodiscard]] constexpr float lengthSquared() const
        {
            return dot(*this);
        }

        //! Returns the length of the vector.
        [[nodiscard]] float length() const
        {
            return std::sqrt(lengthSquared());
        }

        //! Returns a normalised copy. Returns zero vector if length is zero.
        [[nodiscard]] Vec2 normalised() const
        {
            float len = length();
            if (len == 0.0f) {
                return {0.0f, 0.0f};
            }
            float inv = 1.0f / len;
            return {x * inv, y * inv};
        }

        [[nodiscard]] constexpr Vec2 operator+(const Vec2& other) const
        {
            return {x + other.x, y + other.y};
        }

        [[nodiscard]] constexpr Vec2 operator-(const Vec2& other) const
        {
            return {x - other.x, y - other.y};
        }

        [[nodiscard]] constexpr Vec2 operator*(float scalar) const
        {
            return {x * scalar, y * scalar};
        }

        [[nodiscard]] constexpr Vec2 operator-() const
        {
            return {-x, -y};
        }

        constexpr Vec2& operator+=(const Vec2& other)
        {
            x += other.x;
            y += other.y;
            return *this;
        }

        constexpr Vec2& operator-=(const Vec2& other)
        {
            x -= other.x;
            y -= other.y;
            return *this;
        }

        constexpr Vec2& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const Vec2& other) const = default;
    };

    //! 3D vector (position, direction, colour, scale).
    struct Vec3 {
        float x{0.0f}; //!< X component.
        float y{0.0f}; //!< Y component.
        float z{0.0f}; //!< Z component.

        //! Returns the dot product of two vectors.
        [[nodiscard]] constexpr float dot(const Vec3& other) const
        {
            return x * other.x + y * other.y + z * other.z;
        }

        //! Returns the cross product of two vectors.
        [[nodiscard]] constexpr Vec3 cross(const Vec3& other) const
        {
            return {
                y * other.z - z * other.y,
                z * other.x - x * other.z,
                x * other.y - y * other.x,
            };
        }

        //! Returns the squared length (avoids sqrt).
        [[nodiscard]] constexpr float lengthSquared() const
        {
            return dot(*this);
        }

        //! Returns the length of the vector.
        [[nodiscard]] float length() const
        {
            return std::sqrt(lengthSquared());
        }

        //! Returns a normalised copy. Returns zero vector if length is zero.
        [[nodiscard]] Vec3 normalised() const
        {
            float len = length();
            if (len == 0.0f) {
                return {0.0f, 0.0f, 0.0f};
            }
            float inv = 1.0f / len;
            return {x * inv, y * inv, z * inv};
        }

        [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const
        {
            return {x + other.x, y + other.y, z + other.z};
        }

        [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const
        {
            return {x - other.x, y - other.y, z - other.z};
        }

        [[nodiscard]] constexpr Vec3 operator*(float scalar) const
        {
            return {x * scalar, y * scalar, z * scalar};
        }

        [[nodiscard]] constexpr Vec3 operator-() const
        {
            return {-x, -y, -z};
        }

        constexpr Vec3& operator+=(const Vec3& other)
        {
            x += other.x;
            y += other.y;
            z += other.z;
            return *this;
        }

        constexpr Vec3& operator-=(const Vec3& other)
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            return *this;
        }

        constexpr Vec3& operator*=(float scalar)
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const Vec3& other) const = default;
    };

    //! 4D vector (homogeneous coordinates, RGBA colours).
    struct Vec4 {
        float x{0.0f}; //!< X component.
        float y{0.0f}; //!< Y component.
        float z{0.0f}; //!< Z component.
        float w{0.0f}; //!< W component.

        //! Constructs a Vec4 from a Vec3 and a W component.
        [[nodiscard]] static constexpr Vec4 fromVec3(const Vec3& v, float w)
        {
            return {v.x, v.y, v.z, w};
        }

        //! Returns the XYZ components as a Vec3.
        [[nodiscard]] constexpr Vec3 xyz() const
        {
            return {x, y, z};
        }

        //! Returns the dot product of two vectors.
        [[nodiscard]] constexpr float dot(const Vec4& other) const
        {
            return x * other.x + y * other.y + z * other.z + w * other.w;
        }

        [[nodiscard]] constexpr Vec4 operator+(const Vec4& other) const
        {
            return {x + other.x, y + other.y, z + other.z, w + other.w};
        }

        [[nodiscard]] constexpr Vec4 operator-(const Vec4& other) const
        {
            return {x - other.x, y - other.y, z - other.z, w - other.w};
        }

        [[nodiscard]] constexpr Vec4 operator*(float scalar) const
        {
            return {x * scalar, y * scalar, z * scalar, w * scalar};
        }

        [[nodiscard]] constexpr Vec4 operator-() const
        {
            return {-x, -y, -z, -w};
        }

        [[nodiscard]] constexpr bool operator==(const Vec4& other) const = default;
    };

} // namespace MathLib
