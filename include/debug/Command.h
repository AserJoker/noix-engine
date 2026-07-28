#pragma once

/*
 * Command — base class for HTTP API endpoints.
 * Each command represents one API endpoint with a hierarchical name,
 * description, JSON Schema for request/response, and an execute method.
 *
 * Name convention: "system/ping", "system/info", "system/shutdown"
 * Maps to POST /api/v1/{name}
 */

#include "core/Value.h"

#include <string>

namespace noix::debug {

using Value = noix::core::Value;

class Command {
public:
    virtual ~Command() = default;

    /// Endpoint name, e.g. "system/ping"
    virtual std::string name() const = 0;

    /// Human-readable description (for schema self-description)
    virtual std::string description() const = 0;

    /// JSON Schema for the request body
    virtual Value requestSchema() const = 0;

    /// JSON Schema for the response body
    virtual Value responseSchema() const = 0;

    /// Execute the command, return response Value
    virtual Value execute(const Value& request) = 0;
};

} // namespace noix::debug
