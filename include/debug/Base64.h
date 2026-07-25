#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace noix::debug {

/// Base64-encode binary data (RFC 4648). Used for WebSocket accept key.
std::string base64Encode(const std::vector<uint8_t>& data);

} // namespace noix::debug
