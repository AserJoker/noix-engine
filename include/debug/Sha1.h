#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace noix::debug {

/// Standalone SHA-1 hash (RFC 3174), used for WebSocket handshake.
std::vector<uint8_t> sha1(const std::string& data);

/// Convenience: SHA-1 of data, returned as raw 20-byte hex string.
std::string sha1Hex(const std::string& data);

} // namespace noix::debug
