#pragma once

/*
 * Drawable — Renderable unit combining Mesh + Material + Transform.
 * Value type, not managed by SlotMap/AssetManager.
 * Transform is a glm::mat4 updated externally (Logic Thread / Script).
 */

#include "video/Material.h"
#include "video/Mesh.h"

#include <glm/mat4x4.hpp>

namespace noix::video {

class Drawable {
public:
    Drawable() = default;
    Drawable(Mesh::Handle mesh, Material::Handle material,
             glm::mat4 transform = glm::mat4(1.0f));

    Mesh::Handle     &mesh()      { return _mesh; }
    Material::Handle &material()  { return _material; }
    glm::mat4       &transform() { return _transform; }

    const Mesh::Handle     &mesh()      const { return _mesh; }
    const Material::Handle &material()  const { return _material; }
    const glm::mat4       &transform() const { return _transform; }

    bool isValid() const;

private:
    Mesh::Handle     _mesh;
    Material::Handle _material;
    glm::mat4        _transform{1.0f};
};

} // namespace noix::video
