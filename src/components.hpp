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

#include <math/quaternion.hpp>
#include <math/vector.hpp>
#include <cstdint>

/*!
    Plain data components for the entity/component scene structure.

    These are stored in parallel Structure of Arrays (SoA) inside the Scene class.
    No inheritance, no virtual functions — pure data that maps directly to GPU SSBOs.
*/

//! Spatial transform — position, orientation, and scale in world space.
struct Transform {
    MathLib::Vec3 position{0.0f, 0.0f, 0.0f}; //!< World-space position.
    MathLib::Quat orientation{MathLib::Quat::identity()}; //!< Orientation quaternion.
    MathLib::Vec3 scale{1.0f, 1.0f, 1.0f}; //!< Scale along each axis.

    //! Computes the model matrix (translation * rotation * scale).
    [[nodiscard]] MathLib::Mat4 modelMatrix() const
    {
        return MathLib::Mat4::translate(position) * orientation.toMat4() * MathLib::Mat4::scale(scale);
    }
};

//! Identifies which mesh this entity uses (index into the mesh registry).
struct MeshID {
    uint32_t id{0}; //!< Mesh index.
};

//! Bounding sphere for frustum culling and spatial queries.
struct Bounds {
    MathLib::Vec3 centre{0.0f, 0.0f, 0.0f}; //!< World-space bounding sphere centre.
    float radius{0.0f}; //!< Bounding sphere radius.
};
