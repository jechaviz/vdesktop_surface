#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void *(__cdecl *vds_create_fn)(uint64_t, int, int, int, int, const char *, int);
typedef int (__cdecl *vds_navigate_fn)(void *, const char *);
typedef int (__cdecl *vds_bounds_fn)(void *, int, int, int, int);
typedef int (__cdecl *vds_visible_fn)(void *, int);
typedef void (__cdecl *vds_destroy_fn)(void *);
typedef int (__cdecl *vds_probe_fn)(void *, uint64_t *, char *, int, char *, int, char *, int);
typedef const char *(__cdecl *vds_error_fn)(void *);

static HMODULE g_vds_web_module = NULL;
static vds_create_fn g_vds_create = NULL;
static vds_navigate_fn g_vds_navigate = NULL;
static vds_bounds_fn g_vds_bounds = NULL;
static vds_visible_fn g_vds_visible = NULL;
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

    vds_create_fn create_fn = (vds_create_fn)GetProcAddress(module, "vds_webview2_create");
    vds_navigate_fn navigate_fn = (vds_navigate_fn)GetProcAddress(module, "vds_webview2_navigate");
    vds_bounds_fn bounds_fn = (vds_bounds_fn)GetProcAddress(module, "vds_webview2_set_bounds");
    vds_visible_fn visible_fn = (vds_visible_fn)GetProcAddress(module, "vds_webview2_set_visible");
    vds_destroy_fn destroy_fn = (vds_destroy_fn)GetProcAddress(module, "vds_webview2_destroy");
    vds_probe_fn probe_fn = (vds_probe_fn)GetProcAddress(module, "vds_webview2_probe");
    vds_error_fn error_fn = (vds_error_fn)GetProcAddress(module, "vds_webview2_last_error");
    if (!create_fn || !navigate_fn || !bounds_fn || !visible_fn || !destroy_fn || !probe_fn || !error_fn) {
        FreeLibrary(module);
        return 0;
    }

    g_vds_web_module = module;
    g_vds_create = create_fn;
    g_vds_navigate = navigate_fn;
    g_vds_bounds = bounds_fn;
    g_vds_visible = visible_fn;
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
