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

#include "math/vector.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace MathLib
{

    /*!
        4x4 column-major matrix.

        Storage is a single flat std::array<float, 16> in column-major order
        (`m[col * 4 + row]`). Flat (rather than nested
        `std::array<std::array<float, 4>, 4>`) so that `data()` returns a
        pointer into one 16-element array, making indexed access through the
        returned pointer well-defined for the entire range [0, 16) per the C++
        standard's array-bounds rules. Element access uses `operator()(col,
        row)` to read or write a single element by column-major coordinates.

        Layout (column-major, matches Vulkan/SPIR-V uniform-buffer expectations):

            (0, 0)  (1, 0)  (2, 0)  (3, 0)
            (0, 1)  (1, 1)  (2, 1)  (3, 1)
            (0, 2)  (1, 2)  (2, 2)  (3, 2)
            (0, 3)  (1, 3)  (2, 3)  (3, 3)

        i.e. element (col, row) lives at flat index `col * 4 + row`.
    */
    struct Mat4 {
        std::array<float, 16> m{}; //!< Column-major flat storage: element (col, row) at index col * 4 + row.

        //! Mutable element access — column-major.
        [[nodiscard]] constexpr float& operator()(uint32_t col, uint32_t row)
        {
            return m[col * 4 + row];
        }

        //! Const element access — column-major.
        [[nodiscard]] constexpr float operator()(uint32_t col, uint32_t row) const
        {
            return m[col * 4 + row];
        }

        //! Returns the identity matrix.
        [[nodiscard]] static constexpr Mat4 identity()
        {
            Mat4 result{};
            result(0, 0) = 1.0f;
            result(1, 1) = 1.0f;
            result(2, 2) = 1.0f;
            result(3, 3) = 1.0f;
            return result;
        }

        //! Returns a translation matrix.
        [[nodiscard]] static constexpr Mat4 translate(const Vec3& offset)
        {
            Mat4 result{identity()};
            result(3, 0) = offset.x;
            result(3, 1) = offset.y;
            result(3, 2) = offset.z;
            return result;
        }

        //! Returns a uniform or non-uniform scale matrix.
        [[nodiscard]] static constexpr Mat4 scale(const Vec3& s)
        {
            Mat4 result{};
            result(0, 0) = s.x;
            result(1, 1) = s.y;
            result(2, 2) = s.z;
            result(3, 3) = 1.0f;
            return result;
        }

        //! Returns a rotation matrix from an axis and angle (radians).
        [[nodiscard]] static Mat4 rotate(const Vec3& axis, float angle_radians)
        {
            Vec3 a{axis.normalised()};
            float c{std::cos(angle_radians)};
            float s{std::sin(angle_radians)};
            float t{1.0f - c};

            Mat4 result{};
            result(0, 0) = t * a.x * a.x + c;
            result(0, 1) = t * a.x * a.y + s * a.z;
            result(0, 2) = t * a.x * a.z - s * a.y;

            result(1, 0) = t * a.x * a.y - s * a.z;
            result(1, 1) = t * a.y * a.y + c;
            result(1, 2) = t * a.y * a.z + s * a.x;

            result(2, 0) = t * a.x * a.z + s * a.y;
            result(2, 1) = t * a.y * a.z - s * a.x;
            result(2, 2) = t * a.z * a.z + c;

            result(3, 3) = 1.0f;
            return result;
        }

        //! Returns the transposed matrix.
        [[nodiscard]] constexpr Mat4 transposed() const
        {
            Mat4 result{};
            for (uint32_t col{0}; col < 4; ++col) {
                for (uint32_t row{0}; row < 4; ++row) {
                    result(col, row) = (*this)(row, col);
                }
            }
            return result;
        }

        //! Multiplies this matrix by a Vec4.
        [[nodiscard]] constexpr Vec4 operator*(const Vec4& v) const
        {
            return {
                (*this)(0, 0) * v.x + (*this)(1, 0) * v.y + (*this)(2, 0) * v.z + (*this)(3, 0) * v.w,
                (*this)(0, 1) * v.x + (*this)(1, 1) * v.y + (*this)(2, 1) * v.z + (*this)(3, 1) * v.w,
                (*this)(0, 2) * v.x + (*this)(1, 2) * v.y + (*this)(2, 2) * v.z + (*this)(3, 2) * v.w,
                (*this)(0, 3) * v.x + (*this)(1, 3) * v.y + (*this)(2, 3) * v.z + (*this)(3, 3) * v.w,
            };
        }

        //! Multiplies two matrices.
        [[nodiscard]] constexpr Mat4 operator*(const Mat4& other) const
        {
            Mat4 result{};
            for (uint32_t col{0}; col < 4; ++col) {
                for (uint32_t row{0}; row < 4; ++row) {
                    result(col, row) = (*this)(0, row) * other(col, 0) + (*this)(1, row) * other(col, 1) + (*this)(2, row) * other(col, 2)
                        + (*this)(3, row) * other(col, 3);
                }
            }
            return result;
        }

        //! Returns the inverse of this matrix (cofactor expansion, Cramer's rule).
        [[nodiscard]] Mat4 inversed() const
        {
            float a00{(*this)(0, 0)};
            float a01{(*this)(0, 1)};
            float a02{(*this)(0, 2)};
            float a03{(*this)(0, 3)};
            float a10{(*this)(1, 0)};
            float a11{(*this)(1, 1)};
            float a12{(*this)(1, 2)};
            float a13{(*this)(1, 3)};
            float a20{(*this)(2, 0)};
            float a21{(*this)(2, 1)};
            float a22{(*this)(2, 2)};
            float a23{(*this)(2, 3)};
            float a30{(*this)(3, 0)};
            float a31{(*this)(3, 1)};
            float a32{(*this)(3, 2)};
            float a33{(*this)(3, 3)};

            float b00{a00 * a11 - a01 * a10};
            float b01{a00 * a12 - a02 * a10};
            float b02{a00 * a13 - a03 * a10};
            float b03{a01 * a12 - a02 * a11};
            float b04{a01 * a13 - a03 * a11};
            float b05{a02 * a13 - a03 * a12};
            float b06{a20 * a31 - a21 * a30};
            float b07{a20 * a32 - a22 * a30};
            float b08{a20 * a33 - a23 * a30};
            float b09{a21 * a32 - a22 * a31};
            float b10{a21 * a33 - a23 * a31};
            float b11{a22 * a33 - a23 * a32};

            float det{b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06};
            if (std::abs(det) < 1e-10f) {
                return identity(); // Singular matrix — return identity as safe fallback.
            }
            float inv_det{1.0f / det};

            Mat4 result{};
            result(0, 0) = (a11 * b11 - a12 * b10 + a13 * b09) * inv_det;
            result(0, 1) = (-a01 * b11 + a02 * b10 - a03 * b09) * inv_det;
            result(0, 2) = (a31 * b05 - a32 * b04 + a33 * b03) * inv_det;
            result(0, 3) = (-a21 * b05 + a22 * b04 - a23 * b03) * inv_det;
            result(1, 0) = (-a10 * b11 + a12 * b08 - a13 * b07) * inv_det;
            result(1, 1) = (a00 * b11 - a02 * b08 + a03 * b07) * inv_det;
            result(1, 2) = (-a30 * b05 + a32 * b02 - a33 * b01) * inv_det;
            result(1, 3) = (a20 * b05 - a22 * b02 + a23 * b01) * inv_det;
            result(2, 0) = (a10 * b10 - a11 * b08 + a13 * b06) * inv_det;
            result(2, 1) = (-a00 * b10 + a01 * b08 - a03 * b06) * inv_det;
            result(2, 2) = (a30 * b04 - a31 * b02 + a33 * b00) * inv_det;
            result(2, 3) = (-a20 * b04 + a21 * b02 - a23 * b00) * inv_det;
            result(3, 0) = (-a10 * b09 + a11 * b07 - a12 * b06) * inv_det;
            result(3, 1) = (a00 * b09 - a01 * b07 + a02 * b06) * inv_det;
            result(3, 2) = (-a30 * b03 + a31 * b01 - a32 * b00) * inv_det;
            result(3, 3) = (a20 * b03 - a21 * b01 + a22 * b00) * inv_det;
            return result;
        }

        //! Returns a pointer to the raw float data (16 floats, column-major).
        //! Indexing through the returned pointer is well-defined for indices [0, 16) —
        //! the underlying storage is a single std::array<float, 16>.
        [[nodiscard]] constexpr const float* data() const
        {
            return m.data();
        }

        [[nodiscard]] constexpr bool operator==(const Mat4& other) const = default;
    };

} // namespace MathLib
