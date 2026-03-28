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

#include "components.hpp"
#include <cstdint>
#include <vector>

/*!
    Flat entity/component scene — Structure of Arrays (SoA).

    Each entity is a uint32_t index into the parallel component arrays.
    No inheritance, no virtual functions, no heap allocation per component.
    The arrays map directly to GPU SSBOs for rendering.
*/
class Scene {
public:
    //! Adds an entity with the given components. Returns the entity index.
    [[nodiscard]] uint32_t addEntity(const Transform& transform, const MeshID& mesh_id, const Bounds& bounds)
    {
        uint32_t index{static_cast<uint32_t>(m_transforms.size())};
        m_transforms.push_back(transform);
        m_mesh_ids.push_back(mesh_id);
        m_bounds.push_back(bounds);
        return index;
    }

    //! Returns the number of entities in the scene.
    [[nodiscard]] uint32_t entityCount() const
    {
        return static_cast<uint32_t>(m_transforms.size());
    }

    //! Returns the transform array (read-only).
    [[nodiscard]] const std::vector<Transform>& transforms() const
    {
        return m_transforms;
    }

    //! Returns the mesh ID array (read-only).
    [[nodiscard]] const std::vector<MeshID>& meshIDs() const
    {
        return m_mesh_ids;
    }

    //! Returns the bounds array (read-only).
    [[nodiscard]] const std::vector<Bounds>& bounds() const
    {
        return m_bounds;
    }

    //! Mutable access to a transform (for animation/physics).
    [[nodiscard]] Transform& transform(uint32_t index)
    {
        return m_transforms[index];
    }

    //! Mutable access to bounds (for recalculation after transform change).
    [[nodiscard]] Bounds& bound(uint32_t index)
    {
        return m_bounds[index];
    }

    //! Removes all entities.
    void clear()
    {
        m_transforms.clear();
        m_mesh_ids.clear();
        m_bounds.clear();
    }

private:
    std::vector<Transform> m_transforms; //!< Per-entity spatial transforms.
    std::vector<MeshID> m_mesh_ids; //!< Per-entity mesh identifiers.
    std::vector<Bounds> m_bounds; //!< Per-entity bounding spheres.
};
