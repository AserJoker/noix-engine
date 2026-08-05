#include "core/Resource.h"
#include "core/Logger.h"

#include <fstream>
#include <stdexcept>

namespace noix::core {

std::vector<uint8_t> Resource::readFileContent() const {
    std::ifstream file(_filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Logger::instance().error("Resource: Failed to read: {}",
                                 _filePath.string());
        return {};
    }
    auto size = file.tellg();
    if (size < 0) return {};
    file.seekg(0);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.read(reinterpret_cast<char *>(buffer.data()), size);
    if (!file) return {};
    return buffer;
}

void Resource::assertEditable() const {
    if (_mode != ResourceMode::Dynamic) {
        throw std::runtime_error(
            "Resource: Cannot modify Static resource: " + _id.toString());
    }
}

} // namespace noix::core
