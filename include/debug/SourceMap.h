#pragma once

/*
 * SourceMap — Source Map v3 parser and line/column mapper.
 *
 * Parses inline source maps (//# sourceMappingURL=data:application/json;base64,...)
 * and external .map files. Provides bidirectional line/column translation between
 * generated (JS) and original (e.g., TypeScript) source positions.
 *
 * Reference: https://sourcemaps.info/spec.html
 */

#include <string>
#include <vector>

namespace noix::debug {

class SourceMap {
public:
    struct Mapping {
        int generatedLine;    /* 1-based line in generated file */
        int generatedColumn;  /* 0-based column in generated file */
        int sourceIndex;      /* index into _sources */
        int originalLine;     /* 1-based line in original source */
        int originalColumn;   /* 0-based column in original source */
    };

    /* Create an empty/invalid SourceMap */
    SourceMap() = default;

    /* Parse an inline source map from JS file content.
       Extracts the //# sourceMappingURL=data:application/json;base64,... comment. */
    static SourceMap fromInlineSourceMap(const std::string &jsContent);

    /* Load and parse a source map from a .js file on disk.
       Tries inline source map first, then falls back to .js.map sidecar file. */
    static SourceMap fromFile(const std::string &jsPath);

    bool isValid() const { return !_mappings.empty(); }

    /* ---- Generated → Original mapping ---- */

    /* Find the original line for a generated line (1-based).
       Returns -1 if no mapping exists for this line. */
    int originalLine(int generatedLine) const;

    /* Find original line and column for a generated position.
       Returns -1 for either if no mapping exists. */
    int originalLine(int generatedLine, int generatedColumn) const;
    int originalColumn(int generatedLine, int generatedColumn) const;

    /* ---- Original → Generated mapping ---- */

    /* Find the generated line for an original line (1-based).
       Returns the generated line number, or -1 if no mapping exists. */
    int generatedLine(int originalLine) const;

    /* Find generated line and column for an original position.
       Returns -1 for either if no mapping exists. */
    int generatedLine(int originalLine, int originalColumn) const;
    int generatedColumn(int originalLine, int originalColumn) const;

    /* ---- Source path access ---- */

    /* Number of original sources in the source map */
    int sourceCount() const { return static_cast<int>(_sources.size()); }

    /* Get original source path by index (already resolved to absolute path) */
    std::string sourcePath(int index) const;

    /* Given a JS absolute path, resolve the first original source's absolute path.
       Uses sourceRoot + sources[0] resolved relative to the JS file's directory. */
    std::string originalPath(const std::string &jsAbsPath) const;

private:
    std::vector<Mapping> _mappings;
    std::vector<std::string> _sources;  /* original source paths (resolved to absolute) */
    std::string _file;                   /* generated file name */
    std::string _sourceRoot;             /* sourceRoot prefix */

    /* Internal: decode VLQ mappings string and populate _mappings */
    void parseMappings(const std::string &mappingsStr);

    /* Internal: resolve all _sources paths to absolute using sourceRoot and
       the directory of the generated file */
    void resolveSourcePaths(const std::string &jsAbsPath);

    /* Internal: find mapping entry for a generated position (binary search) */
    const Mapping *findGeneratedMapping(int genLine, int genCol) const;

    /* Internal: find mapping entry for an original position (linear search) */
    const Mapping *findOriginalMapping(int origLine, int origCol) const;
};

} // namespace noix::debug
