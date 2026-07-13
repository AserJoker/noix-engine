#include "resource/ResourcePack.h"
#include "core/Logger.h"
#include <algorithm>

namespace noix::resource {

ResourcePack::ResourcePack(std::filesystem::path basePath)
    : _defaultPath(std::move(basePath)) {}

void ResourcePack::addPack(const std::filesystem::path& packPath) {
    _packRoots.push_back(std::filesystem::weakly_canonical(packPath));
    core::Logger::instance().info("resource pack added: {}", packPath.string());
}

std::optional<std::filesystem::path> ResourcePack::resolve(const core::NamespacedId& id) const {
    auto rel = toRelativePath(id);
    for (auto it = _packRoots.rbegin(); it != _packRoots.rend(); ++it) {
        auto candidate = *it / rel;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    auto candidate = _defaultPath / rel;
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }
    return std::nullopt;
}

bool ResourcePack::exists(const core::NamespacedId& id) const {
    return resolve(id).has_value();
}

std::filesystem::path ResourcePack::toRelativePath(const core::NamespacedId& id) {
    return std::filesystem::path("resources") / id.ns() / id.name();
}

} // namespace noix::resource
