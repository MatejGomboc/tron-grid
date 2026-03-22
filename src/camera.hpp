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

#include <math/projection.hpp>
#include <math/quaternion.hpp>
#include <math/vector.hpp>

/*!
    Free-flight camera with quaternion orientation.

    Supports WASD + mouse look. Movement is in local camera space
    (forward/right/up relative to where the camera is looking).
    Rotation is quaternion-based — no gimbal lock.
*/
class Camera {
public:
    //! Constructs a camera at the given position looking along -Z.
    explicit Camera(const MathLib::Vec3& position = {0.0f, 0.0f, 0.0f}, float fov_y = MathLib::PI / 4.0f, float near_plane = 0.1f, float far_plane = 100.0f) :
        m_position(position),
        m_fov_y(fov_y),
        m_near_plane(near_plane),
        m_far_plane(far_plane)
    {
    }

    //! Returns the view matrix from position and orientation.
    [[nodiscard]] MathLib::Mat4 viewMatrix() const
    {
        return MathLib::viewFromQuaternion(m_position, m_orientation);
    }

    //! Returns the perspective projection matrix for the given aspect ratio.
    [[nodiscard]] MathLib::Mat4 projectionMatrix(float aspect) const
    {
        return MathLib::perspective(m_fov_y, aspect, m_near_plane, m_far_plane);
    }

    //! Moves the camera forward (along local -Z axis).
    void moveForward(float delta)
    {
        MathLib::Vec3 forward = m_orientation.rotate({0.0f, 0.0f, -1.0f});
        m_position += forward * delta;
    }

    //! Moves the camera right (along local +X axis).
    void moveRight(float delta)
    {
        MathLib::Vec3 right = m_orientation.rotate({1.0f, 0.0f, 0.0f});
        m_position += right * delta;
    }

    //! Moves the camera up (along world +Y axis).
    void moveUp(float delta)
    {
        m_position.y += delta;
    }

    //! Rotates the camera by yaw (horizontal) and pitch (vertical) in radians.
    void rotate(float yaw, float pitch)
    {
        // Yaw around world Y axis (so horizontal look is always level).
        MathLib::Quat yaw_quat = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);

        // Pitch around local X axis (camera's right vector).
        MathLib::Quat pitch_quat = MathLib::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch);

        // Apply yaw first (world space), then pitch (local space).
        m_orientation = (yaw_quat * m_orientation * pitch_quat).normalised();
    }

    //! Camera position in world space.
    [[nodiscard]] const MathLib::Vec3& position() const
    {
        return m_position;
    }

    //! Camera orientation quaternion.
    [[nodiscard]] const MathLib::Quat& orientation() const
    {
        return m_orientation;
    }

private:
    MathLib::Vec3 m_position{0.0f, 0.0f, 0.0f}; //!< World-space position.
    MathLib::Quat m_orientation{MathLib::Quat::identity()}; //!< Orientation (default: looking along -Z).
    float m_fov_y{MathLib::PI / 4.0f}; //!< Vertical field of view in radians.
    float m_near_plane{0.1f}; //!< Near clipping plane distance.
    float m_far_plane{100.0f}; //!< Far clipping plane distance.
};
