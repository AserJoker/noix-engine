#include "DapBridgeUtils.h"
#include "debug/DapBridge.h"
#include "cJSON.h"
#include "core/Logger.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <climits>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

namespace noix::debug {

/* ---- JSON helpers ---- */

cJSON *json_get(cJSON *obj, const char *key) {
    return obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : nullptr;
}

int json_get_int(cJSON *obj, const char *key, int def) {
    cJSON *v = json_get(obj, key);
    return v && cJSON_IsNumber(v) ? v->valueint : def;
}

const char *json_get_str(cJSON *obj, const char *key, const char *def) {
    cJSON *v = json_get(obj, key);
    return v && cJSON_IsString(v) ? v->valuestring : def;
}

bool json_get_bool(cJSON *obj, const char *key, bool def) {
    cJSON *v = json_get(obj, key);
    return v && cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}

/* ---- Path helpers ---- */

/* Convert a potentially relative path to absolute, using CWD.
   Returns a thread-local static buffer -- use immediately or copy. */
const char *toAbsolutePath(const char *path) {
    if (!path || !path[0]) return path;
    if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':'))
        return path; /* already absolute */
    static thread_local char buf[_MAX_PATH];
    if (_fullpath(buf, path, _MAX_PATH)) return buf;
    return path;
}

/* Normalize a path for comparison: convert to absolute and normalize separators to '/' */
std::string normalizePath(const char *path) {
    if (!path || !path[0]) return "";
    std::string abs(toAbsolutePath(path));
    std::replace(abs.begin(), abs.end(), '\\', '/');
    /* Lowercase drive letter on Windows for consistency */
    if (abs.size() >= 2 && abs[1] == ':') abs[0] = (char)tolower(abs[0]);
    return abs;
}

/* ---- Minimal module support ---- */

/* Load a file into a malloc'd buffer. Caller must free with js_free(). */
char *dap_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename) {
    FILE *f;
    fopen_s(&f, filename, "rb");
    if (!f) {
        *pbuf_len = 0;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)js_malloc(ctx, len + 1);
    if (!buf) {
        fclose(f);
        *pbuf_len = 0;
        return nullptr;
    }
    size_t nread = fread(buf, 1, len, f);
    fclose(f);
    buf[nread] = '\0';
    *pbuf_len = nread;
    return buf;
}

/* Set import.meta.url and import.meta.main on a module.
   Simplified version of js_module_set_import_meta from quickjs-libc. */
int dap_set_import_meta(JSContext *ctx, JSValueConst func_val, bool is_main) {
    JSModuleDef *m;
    JSValue meta_obj;
    JSAtom module_name_atom;
    const char *module_name;
    char url[1024];

    assert(JS_VALUE_GET_TAG(func_val) == JS_TAG_MODULE);
    m = (JSModuleDef *)JS_VALUE_GET_PTR(func_val);

    module_name_atom = JS_GetModuleName(ctx, m);
    module_name = JS_AtomToCString(ctx, module_name_atom);
    JS_FreeAtom(ctx, module_name_atom);
    if (!module_name)
        return -1;

    /* Build file:// URL */
#ifdef _WIN32
    snprintf(url, sizeof(url), "file:///%s", module_name);
    /* Replace backslashes with forward slashes for a proper URI */
    for (char *p = url + 8; *p; p++) {
        if (*p == '\\') *p = '/';
    }
#else
    snprintf(url, sizeof(url), "file://%s", module_name);
#endif
    JS_FreeCString(ctx, module_name);

    meta_obj = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta_obj))
        return -1;
    JS_DefinePropertyValueStr(ctx, meta_obj, "url",
                              JS_NewString(ctx, url),
                              JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta_obj, "main",
                              JS_NewBool(ctx, is_main),
                              JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta_obj);
    return 0;
}

/* Custom module loader that compiles modules with JS_EVAL_FLAG_DEBUG_INFO
   so that breakpoints work in imported modules.
   Receives DapBridge* as opaque to access scriptPath. */
JSModuleDef *dap_module_loader(JSContext *ctx, const char *module_name,
                               void *opaque) {
    auto *bridge = static_cast<DapBridge *>(opaque);

    /* Resolve relative module paths against the main script directory.
       QuickJS default normalizer strips './' but doesn't handle Windows
       backslash paths, so we get a bare filename like 'dap_multifile_mod.js'. */
    std::string resolved_path;
    if (module_name[0] != '/' && module_name[0] != '\\' &&
        !(module_name[0] && module_name[1] == ':')) {
        /* Relative path -- resolve against main script directory */
        std::string base_dir = bridge->scriptPath;
        auto last_sep = base_dir.find_last_of("/\\");
        if (last_sep != std::string::npos)
            base_dir = base_dir.substr(0, last_sep + 1);
        else
            base_dir = "";
        resolved_path = base_dir + module_name;
    } else {
        resolved_path = module_name;
    }

    size_t buf_len;
    char *buf = dap_load_file(ctx, &buf_len, resolved_path.c_str());
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s'", resolved_path.c_str());
        return nullptr;
    }

    /* Compile with DEBUG_INFO so breakpoints work in imported modules.
       Use resolved absolute path as the filename so breakpoint matching works. */
    JSValue val = JS_Eval(ctx, buf, buf_len, resolved_path.c_str(),
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_FLAG_DEBUG_INFO);
    js_free(ctx, buf);

    if (JS_IsException(val)) {
        return nullptr;
    }

    JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(val);

    /* Set import.meta */
    dap_set_import_meta(ctx, val, false);

    /* Notify about loaded source */
    {
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "reason", "new");
        cJSON *src = cJSON_CreateObject();
        cJSON_AddStringToObject(src, "name", resolved_path.c_str());
        cJSON_AddStringToObject(src, "path", resolved_path.c_str());
        cJSON_AddNumberToObject(src, "sourceReference", 0);
        cJSON_AddItemToObject(body, "source", src);
        bridge->pushEvent("loadedSource", body);
    }

    /* The module is already referenced by the module system, free our ref */
    JS_FreeValue(ctx, val);
    return m;
}

/* ---- formatJSValue: format a JSValue into DAP variable fields ---- */

void formatJSValue(JSContext *ctx, ObjectRefStore &refs, cJSON *v, JSValue val,
                   const char *valueKey) {

    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", d);
        cJSON_AddStringToObject(v, valueKey, buf);
        cJSON_AddStringToObject(v, "type", "number");
    } else if (JS_IsBool(val)) {
        cJSON_AddStringToObject(v, valueKey, JS_ToBool(ctx, val) ? "true" : "false");
        cJSON_AddStringToObject(v, "type", "boolean");
    } else if (JS_IsString(val)) {
        const char *s = JS_ToCString(ctx, val);
        cJSON_AddStringToObject(v, valueKey, s ? s : "\"\"");
        cJSON_AddStringToObject(v, "type", "string");
        JS_FreeCString(ctx, s);
    } else if (JS_IsNull(val)) {
        cJSON_AddStringToObject(v, valueKey, "null");
        cJSON_AddStringToObject(v, "type", "null");
    } else if (JS_IsUndefined(val)) {
        cJSON_AddStringToObject(v, valueKey, "undefined");
        cJSON_AddStringToObject(v, "type", "undefined");
    } else if (JS_VALUE_GET_TAG(val) == JS_TAG_UNINITIALIZED) {
        /* TDZ -- let/const variable not yet initialized */
        cJSON_AddStringToObject(v, valueKey, "<uninitialized>");
        cJSON_AddStringToObject(v, "type", "undefined");
    } else if (JS_IsObject(val)) {
        /* Check for specific object types */
        if (JS_IsArray(val)) {
            /* Get array length */
            JSAtom lengthAtom = JS_NewAtom(ctx, "length");
            JSValue lenVal = JS_GetProperty(ctx, val, lengthAtom);
            uint32_t len = 0;
            if (JS_IsNumber(lenVal)) {
                int32_t i32;
                if (JS_ToInt32(ctx, &i32, lenVal) == 0)
                    len = (uint32_t)i32;
            }
            JS_FreeValue(ctx, lenVal);
            JS_FreeAtom(ctx, lengthAtom);
            char buf[64];
            snprintf(buf, sizeof(buf), "Array(%u)", len);
            cJSON_AddStringToObject(v, valueKey, buf);
            cJSON_AddStringToObject(v, "type", "object");
        } else if (JS_IsFunction(ctx, val)) {
            cJSON_AddStringToObject(v, valueKey, "function");
            cJSON_AddStringToObject(v, "type", "function");
        } else {
            cJSON_AddStringToObject(v, valueKey, "Object");
            cJSON_AddStringToObject(v, "type", "object");
        }
        int objRef = refs.add(JS_DupValue(ctx, val));
        cJSON_AddNumberToObject(v, "variablesReference", objRef);
    } else {
        cJSON_AddStringToObject(v, valueKey, "[unknown]");
    }
}

} // namespace noix::debug
