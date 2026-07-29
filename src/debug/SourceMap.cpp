#include "debug/SourceMap.h"
#include "cJSON.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#define GETCWD _getcwd
#else
#include <unistd.h>
#define GETCWD getcwd
#endif

namespace noix::debug {

/* ---- Base64 decoder ---- */

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64DecodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<uint8_t> base64Decode(const std::string &encoded) {
    std::vector<uint8_t> result;
    result.reserve(encoded.size() * 3 / 4);

    int val = 0, valb = -8;
    for (char c : encoded) {
        int d = base64DecodeChar(c);
        if (d < 0) continue;
        val = (val << 6) | d;
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

/* ---- VLQ decoder ---- */

/* VLQ encoding: each group is 6 bits, MSB is continuation flag.
   Values use sign bit in the LSB of the first group. */

static int decodeVLQ(const std::string &str, size_t &pos) {
    int result = 0;
    int shift = 0;
    bool continuation;

    do {
        if (pos >= str.size()) return 0;
        int d = base64DecodeChar(str[pos++]);
        if (d < 0) return 0;
        continuation = (d & 0x20) != 0;
        d &= 0x1F;
        result += d << shift;
        shift += 5;
    } while (continuation);

    /* LSB is sign bit: 1 = negative */
    bool negative = (result & 1) != 0;
    result >>= 1;
    if (negative) result = -result;
    return result;
}

/* ---- SourceMap implementation ---- */

void SourceMap::parseMappings(const std::string &mappingsStr) {
    _mappings.clear();

    size_t pos = 0;
    int genLine = 1;
    int genCol = 0;
    int srcIdx = 0;
    int origLine = 1;
    int origCol = 0;

    while (pos < mappingsStr.size()) {
        /* Skip commas between segments */
        if (mappingsStr[pos] == ',') {
            pos++;
            continue;
        }

        /* Semicolons advance to the next generated line */
        if (mappingsStr[pos] == ';') {
            pos++;
            genLine++;
            genCol = 0;
            continue;
        }

        /* Decode a segment: genCol, srcIdx, origLine, origCol [, nameIdx] */
        genCol += decodeVLQ(mappingsStr, pos);
        if (pos >= mappingsStr.size() || mappingsStr[pos] == ',' || mappingsStr[pos] == ';') {
            /* No source info in this segment (e.g., generated code with no mapping) */
            continue;
        }
        srcIdx += decodeVLQ(mappingsStr, pos);
        origLine += decodeVLQ(mappingsStr, pos);
        origCol += decodeVLQ(mappingsStr, pos);
        /* Skip optional name index */
        if (pos < mappingsStr.size() && mappingsStr[pos] != ',' && mappingsStr[pos] != ';') {
            decodeVLQ(mappingsStr, pos);
        }

        Mapping m;
        m.generatedLine = genLine;
        m.generatedColumn = genCol;
        m.sourceIndex = srcIdx;
        m.originalLine = origLine;
        m.originalColumn = origCol;
        _mappings.push_back(m);
    }
}

void SourceMap::resolveSourcePaths(const std::string &jsAbsPath) {
    /* Determine the directory of the generated JS file */
    std::string jsDir;
    auto lastSep = jsAbsPath.find_last_of("/\\");
    if (lastSep != std::string::npos) {
        jsDir = jsAbsPath.substr(0, lastSep + 1);
    }

    /* Resolve each source path: sourceRoot + source, relative to jsDir */
    for (auto &src : _sources) {
        std::string combined = _sourceRoot + src;
        if (!combined.empty() && combined[0] != '/' && combined[0] != '\\' &&
            !(combined.size() > 1 && combined[1] == ':')) {
            /* Relative path: resolve against JS file directory */
            if (!jsDir.empty()) {
                combined = jsDir + combined;
            }
        }
        /* Normalize separators */
        std::replace(combined.begin(), combined.end(), '\\', '/');
        /* Resolve . and .. components */
        std::vector<std::string> parts;
        std::string part;
        for (size_t i = 0; i <= combined.size(); i++) {
            if (i == combined.size() || combined[i] == '/') {
                if (part == "..") {
                    if (!parts.empty() && parts.back() != "..") parts.pop_back();
                } else if (part != "." && !part.empty()) {
                    parts.push_back(part);
                }
                part.clear();
            } else {
                part += combined[i];
            }
        }
        /* Rebuild path */
        std::string resolved;
        /* Check for drive letter prefix */
        if (!parts.empty() && parts[0].size() == 2 && parts[0][1] == ':') {
            resolved = parts[0] + "/";
            for (size_t i = 1; i < parts.size(); i++) {
                if (i > 1) resolved += "/";
                resolved += parts[i];
            }
        } else {
            for (size_t i = 0; i < parts.size(); i++) {
                if (i > 0) resolved += "/";
                resolved += parts[i];
            }
        }
        /* Lowercase drive letter on Windows */
        if (resolved.size() >= 2 && resolved[1] == ':') {
            resolved[0] = static_cast<char>(tolower(resolved[0]));
        }
        src = resolved;
    }
}

SourceMap SourceMap::fromInlineSourceMap(const std::string &jsContent) {
    SourceMap result;

    /* Find the //# sourceMappingURL= comment at the end of the file */
    const std::string marker = "//# sourceMappingURL=data:application/json;base64,";
    auto pos = jsContent.rfind(marker);
    if (pos == std::string::npos) return result;

    std::string base64Data = jsContent.substr(pos + marker.size());
    /* Trim trailing whitespace/newlines */
    while (!base64Data.empty() && (base64Data.back() == '\r' || base64Data.back() == '\n' || base64Data.back() == ' '))
        base64Data.pop_back();

    std::vector<uint8_t> decoded = base64Decode(base64Data);
    std::string jsonStr(decoded.begin(), decoded.end());

    cJSON *root = cJSON_Parse(jsonStr.c_str());
    if (!root) return result;

    /* Extract version (must be 3) */
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!version || !cJSON_IsNumber(version) || version->valueint != 3) {
        cJSON_Delete(root);
        return result;
    }

    /* Extract file */
    cJSON *file = cJSON_GetObjectItemCaseSensitive(root, "file");
    if (file && cJSON_IsString(file)) {
        result._file = file->valuestring;
    }

    /* Extract sourceRoot */
    cJSON *sourceRoot = cJSON_GetObjectItemCaseSensitive(root, "sourceRoot");
    if (sourceRoot && cJSON_IsString(sourceRoot)) {
        result._sourceRoot = sourceRoot->valuestring;
    }

    /* Extract sources array */
    cJSON *sources = cJSON_GetObjectItemCaseSensitive(root, "sources");
    if (sources && cJSON_IsArray(sources)) {
        int arrSize = cJSON_GetArraySize(sources);
        for (int i = 0; i < arrSize; i++) {
            cJSON *s = cJSON_GetArrayItem(sources, i);
            if (s && cJSON_IsString(s)) {
                result._sources.push_back(s->valuestring);
            }
        }
    }

    /* Extract and parse mappings */
    cJSON *mappings = cJSON_GetObjectItemCaseSensitive(root, "mappings");
    if (mappings && cJSON_IsString(mappings)) {
        result.parseMappings(mappings->valuestring);
    }

    cJSON_Delete(root);

    /* We cannot resolve source paths here because we don't know the JS file path.
       resolveSourcePaths will be called from fromFile() after we know the path. */
    return result;
}

SourceMap SourceMap::fromFile(const std::string &jsPath) {
    /* Read the JS file content */
    std::ifstream file(jsPath);
    if (!file.is_open()) return SourceMap();

    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    /* Try inline source map first */
    SourceMap result = fromInlineSourceMap(content);

    if (!result.isValid()) {
        /* Try external .map file */
        std::string mapPath = jsPath + ".map";
        std::ifstream mapFile(mapPath);
        if (mapFile.is_open()) {
            std::stringstream ms;
            ms << mapFile.rdbuf();
            std::string mapContent = ms.str();

            cJSON *root = cJSON_Parse(mapContent.c_str());
            if (root) {
                cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
                if (version && cJSON_IsNumber(version) && version->valueint == 3) {
                    cJSON *file = cJSON_GetObjectItemCaseSensitive(root, "file");
                    if (file && cJSON_IsString(file)) result._file = file->valuestring;

                    cJSON *sourceRoot = cJSON_GetObjectItemCaseSensitive(root, "sourceRoot");
                    if (sourceRoot && cJSON_IsString(sourceRoot)) result._sourceRoot = sourceRoot->valuestring;

                    cJSON *sources = cJSON_GetObjectItemCaseSensitive(root, "sources");
                    if (sources && cJSON_IsArray(sources)) {
                        int arrSize = cJSON_GetArraySize(sources);
                        for (int i = 0; i < arrSize; i++) {
                            cJSON *s = cJSON_GetArrayItem(sources, i);
                            if (s && cJSON_IsString(s)) result._sources.push_back(s->valuestring);
                        }
                    }

                    cJSON *mappings = cJSON_GetObjectItemCaseSensitive(root, "mappings");
                    if (mappings && cJSON_IsString(mappings)) {
                        result.parseMappings(mappings->valuestring);
                    }
                }
                cJSON_Delete(root);
            }
        }
    }

    /* Resolve source paths to absolute */
    if (result.isValid()) {
        /* Normalize jsPath to absolute for resolution */
        std::string absPath = jsPath;
        if (jsPath.size() >= 2 && jsPath[1] == ':') {
            std::replace(absPath.begin(), absPath.end(), '\\', '/');
            absPath[0] = static_cast<char>(tolower(absPath[0]));
        }
        result.resolveSourcePaths(absPath);
    }

    return result;
}

/* ---- Generated → Original ---- */

const SourceMap::Mapping *SourceMap::findGeneratedMapping(int genLine, int genCol) const {
    /* Binary search: find the last mapping entry where generatedLine <= genLine
       and generatedColumn <= genCol */
    const Mapping *best = nullptr;
    int lo = 0, hi = static_cast<int>(_mappings.size()) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const Mapping &m = _mappings[mid];
        if (m.generatedLine < genLine || (m.generatedLine == genLine && m.generatedColumn <= genCol)) {
            best = &m;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

int SourceMap::originalLine(int generatedLine) const {
    return originalLine(generatedLine, 0);
}

int SourceMap::originalLine(int generatedLine, int generatedColumn) const {
    const Mapping *m = findGeneratedMapping(generatedLine, generatedColumn);
    if (!m) return generatedLine;
    /* Source map spec: if the closest mapping is on a different generated line,
       the query position is unmapped — return the same line number as the
       generated position. TypeScript doesn't emit mappings for lines identical
       to the source, so unmapped lines map to themselves. */
    if (m->generatedLine != generatedLine) return generatedLine;
    return m->originalLine;
}

int SourceMap::originalColumn(int generatedLine, int generatedColumn) const {
    const Mapping *m = findGeneratedMapping(generatedLine, generatedColumn);
    if (!m) return 0;
    if (m->generatedLine != generatedLine) return 0;
    return m->originalColumn;
}

/* ---- Original → Generated ---- */

const SourceMap::Mapping *SourceMap::findOriginalMapping(int origLine, int origCol) const {
    /* Linear search: find mapping with matching original line,
       then closest column <= origCol.
       NOTE: _mappings is sorted by generatedLine, NOT by originalLine,
       so we cannot break early when originalLine > origLine. */
    const Mapping *best = nullptr;
    for (const auto &m : _mappings) {
        if (m.originalLine != origLine) continue;
        if (m.originalColumn <= origCol) {
            best = &m;
        }
    }
    return best;
}

int SourceMap::generatedLine(int originalLine) const {
    const Mapping *m = findOriginalMapping(originalLine, 0);
    return m ? m->generatedLine : -1;
}

int SourceMap::generatedLine(int originalLine, int originalColumn) const {
    const Mapping *m = findOriginalMapping(originalLine, originalColumn);
    return m ? m->generatedLine : -1;
}

int SourceMap::generatedColumn(int originalLine, int originalColumn) const {
    const Mapping *m = findOriginalMapping(originalLine, originalColumn);
    return m ? m->generatedColumn : -1;
}

/* ---- Source path access ---- */

std::string SourceMap::sourcePath(int index) const {
    if (index >= 0 && index < static_cast<int>(_sources.size())) {
        return _sources[index];
    }
    return "";
}

std::string SourceMap::originalPath(const std::string &jsAbsPath) const {
    if (!_sources.empty()) {
        return _sources[0]; /* already resolved to absolute in resolveSourcePaths */
    }
    /* Fallback: replace .js with .ts */
    std::string result = jsAbsPath;
    if (result.size() > 3 && result.substr(result.size() - 3) == ".js") {
        result = result.substr(0, result.size() - 3) + ".ts";
    }
    return result;
}

} // namespace noix::debug
