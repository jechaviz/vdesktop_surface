#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (__cdecl *vds_abi_fn)(void);
typedef void *(__cdecl *vds_create_fn)(uint64_t, int, int, int, int, const char *, int);
typedef int (__cdecl *vds_navigate_fn)(void *, const char *);
typedef int (__cdecl *vds_bounds_fn)(void *, int, int, int, int);
typedef int (__cdecl *vds_visible_fn)(void *, int);
typedef int (__cdecl *vds_eval_action_fn)(void *, const char *, const char *);
typedef int (__cdecl *vds_set_cookie_fn)(void *, const char *, const char *, const char *,
    const char *, int, int, const char *, int64_t);
typedef void (__cdecl *vds_destroy_fn)(void *);
typedef int (__cdecl *vds_probe_fn)(void *, uint64_t *, char *, int, char *, int, char *, int,
    char *, int, char *, int, uint64_t *, char *, int, int *, char *, int,
    uint64_t *, char *, int, char *, int, char *, int, char *, int, int64_t *, int64_t *);
typedef const char *(__cdecl *vds_error_fn)(void *);

static HMODULE g_vds_web_module = NULL;
static vds_create_fn g_vds_create = NULL;
static vds_navigate_fn g_vds_navigate = NULL;
static vds_bounds_fn g_vds_bounds = NULL;
static vds_visible_fn g_vds_visible = NULL;
static vds_eval_action_fn g_vds_eval_action = NULL;
static vds_set_cookie_fn g_vds_set_cookie = NULL;
static vds_destroy_fn g_vds_destroy = NULL;
static vds_probe_fn g_vds_probe = NULL;
static vds_error_fn g_vds_error = NULL;
static char g_vds_load_error[512] = {0};

static void vds_set_load_error(const char *text) {
    if (!text) text = "";
    strncpy(g_vds_load_error, text, sizeof(g_vds_load_error) - 1);
    g_vds_load_error[sizeof(g_vds_load_error) - 1] = 0;
}

static int vds_load_from(const char *path) {
    if (!path || !path[0]) return 0;
    HMODULE module = LoadLibraryA(path);
    if (!module) return 0;

    vds_abi_fn abi_fn = (vds_abi_fn)GetProcAddress(module, "vds_webview2_abi_version");
    vds_create_fn create_fn = (vds_create_fn)GetProcAddress(module, "vds_webview2_create");
    vds_navigate_fn navigate_fn = (vds_navigate_fn)GetProcAddress(module, "vds_webview2_navigate");
    vds_bounds_fn bounds_fn = (vds_bounds_fn)GetProcAddress(module, "vds_webview2_set_bounds");
    vds_visible_fn visible_fn = (vds_visible_fn)GetProcAddress(module, "vds_webview2_set_visible");
    vds_eval_action_fn eval_action_fn =
        (vds_eval_action_fn)GetProcAddress(module, "vds_webview2_eval_action");
    vds_set_cookie_fn set_cookie_fn =
        (vds_set_cookie_fn)GetProcAddress(module, "vds_webview2_set_cookie");
    vds_destroy_fn destroy_fn = (vds_destroy_fn)GetProcAddress(module, "vds_webview2_destroy");
    vds_probe_fn probe_fn = (vds_probe_fn)GetProcAddress(module, "vds_webview2_probe");
    vds_error_fn error_fn = (vds_error_fn)GetProcAddress(module, "vds_webview2_last_error");
    if (!abi_fn || abi_fn() < 6 || !create_fn || !navigate_fn || !bounds_fn || !visible_fn
        || !eval_action_fn || !destroy_fn || !probe_fn || !error_fn) {
        FreeLibrary(module);
        return 0;
    }

    g_vds_web_module = module;
    g_vds_create = create_fn;
    g_vds_navigate = navigate_fn;
    g_vds_bounds = bounds_fn;
    g_vds_visible = visible_fn;
    g_vds_eval_action = eval_action_fn;
    g_vds_set_cookie = set_cookie_fn;
    g_vds_destroy = destroy_fn;
    g_vds_probe = probe_fn;
    g_vds_error = error_fn;
    vds_set_load_error("");
    return 1;
}

static int vds_ensure_loaded(void) {
    if (g_vds_web_module) return 1;

    char explicit_path[MAX_PATH * 4] = {0};
    DWORD explicit_len = GetEnvironmentVariableA("VDESKTOP_WEBVIEW2_DLL",
        explicit_path, (DWORD)sizeof(explicit_path));
    if (explicit_len > 0 && explicit_len < sizeof(explicit_path)) {
        if (vds_load_from(explicit_path)) return 1;
        vds_set_load_error("failed to load VDESKTOP_WEBVIEW2_DLL");
        return 0;
    }

    char exe_path[MAX_PATH * 4] = {0};
    DWORD exe_len = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
    if (exe_len > 0 && exe_len < sizeof(exe_path)) {
        char *slash = strrchr(exe_path, '\\');
        if (slash) {
            slash[1] = 0;
            strncat(exe_path, "vdesktop_webview2.dll",
                sizeof(exe_path) - strlen(exe_path) - 1);
            if (vds_load_from(exe_path)) return 1;
        }
    }

    vds_set_load_error(
        "vdesktop_webview2.dll not found; copy it beside the executable or set VDESKTOP_WEBVIEW2_DLL");
    return 0;
}

int vdesktop_surface_web_available(void) {
    return vds_ensure_loaded();
}

void *vdesktop_surface_web_create(uint64_t parent_handle, int x, int y, int w, int h,
    const char *url, int debug) {
    if (!vds_ensure_loaded()) return NULL;
    return g_vds_create(parent_handle, x, y, w, h, url, debug);
}

int vdesktop_surface_web_navigate(void *handle, const char *url) {
    if (!handle || !vds_ensure_loaded()) return 0;
    return g_vds_navigate(handle, url);
}

int vdesktop_surface_web_set_bounds(void *handle, int x, int y, int w, int h) {
    if (!handle || !vds_ensure_loaded()) return 0;
    return g_vds_bounds(handle, x, y, w, h);
}

int vdesktop_surface_web_set_visible(void *handle, int visible) {
    if (!handle || !vds_ensure_loaded()) return 0;
    return g_vds_visible(handle, visible);
}

int vdesktop_surface_web_eval_action(void *handle, const char *action_id, const char *script) {
    if (!handle || !vds_ensure_loaded()) return 0;
    return g_vds_eval_action(handle, action_id, script);
}

int vdesktop_surface_web_set_cookie(void *handle, const char *name, const char *value,
    const char *domain, const char *path, int secure, int http_only,
    const char *same_site, int64_t expires_unix) {
    if (!handle || !vds_ensure_loaded() || !g_vds_set_cookie) return 0;
    return g_vds_set_cookie(handle, name, value, domain, path, secure, http_only,
        same_site, expires_unix);
}

int vdesktop_surface_web_probe(void *handle, uint64_t *load_count, char *url, int url_cap,
    char *title, int title_cap, char *text, int text_cap, char *controls, int controls_cap,
    char *structure, int structure_cap, uint64_t *action_count, char *action_id,
    int action_id_cap, int *action_ok, char *action_message, int action_message_cap,
    uint64_t *download_count, char *download_url, int download_url_cap,
    char *download_path, int download_path_cap, char *download_mime, int download_mime_cap,
    char *download_state, int download_state_cap, int64_t *download_bytes,
    int64_t *download_total) {
    if (!handle || !vds_ensure_loaded()) return 0;
    return g_vds_probe(handle, load_count, url, url_cap, title, title_cap, text, text_cap,
        controls, controls_cap, structure, structure_cap, action_count, action_id,
        action_id_cap, action_ok, action_message, action_message_cap, download_count,
        download_url, download_url_cap, download_path, download_path_cap, download_mime,
        download_mime_cap, download_state, download_state_cap, download_bytes, download_total);
}

void vdesktop_surface_web_destroy(void *handle) {
    if (handle && vds_ensure_loaded()) g_vds_destroy(handle);
}

const char *vdesktop_surface_web_last_error(void *handle) {
    if (handle && g_vds_error) {
        const char *error = g_vds_error(handle);
        if (error && error[0]) return error;
    }
    return g_vds_load_error;
}
