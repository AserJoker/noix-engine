#include "video/Drawable.h"

namespace noix::video {

Drawable::Drawable(Mesh::Handle mesh, Material::Handle material,
                   glm::mat4 transform)
    : _mesh(std::move(mesh)),
      _material(std::move(material)),
      _transform(std::move(transform)) {}

bool Drawable::isValid() const {
    return _mesh.isValid() && _material.isValid();
}

} // namespace noix::video
