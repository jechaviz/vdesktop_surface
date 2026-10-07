#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <new>
#include <string>

#include "webview/webview.h"

struct VdsWebView2Surface {
    HWND child = NULL;
    webview_t view = nullptr;
    std::string last_error;
};

static thread_local std::string g_vds_webview2_error;

static void vds_set_error(VdsWebView2Surface *state, const char *message) {
    const char *text = message ? message : "";
    g_vds_webview2_error = text;
    if (state) state->last_error = text;
}

static int vds_ok(webview_error_t result) {
    return result == WEBVIEW_ERROR_OK ? 1 : 0;
}

extern "C" __declspec(dllexport)
void *vds_webview2_create(uint64_t parent_handle, int x, int y, int width, int height,
    const char *url, int debug) {
    HWND parent = reinterpret_cast<HWND>(static_cast<uintptr_t>(parent_handle));
    if (!parent || !IsWindow(parent) || width <= 0 || height <= 0) {
        vds_set_error(nullptr, "invalid parent window or bounds");
        return nullptr;
    }

    auto *state = new (std::nothrow) VdsWebView2Surface();
    if (!state) {
        vds_set_error(nullptr, "allocation failed");
        return nullptr;
    }

    state->child = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        x, y, width, height, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!state->child) {
        vds_set_error(state, "failed to create child host window");
        delete state;
        return nullptr;
    }

    HWND child_handle = state->child;
    state->view = webview_create(debug, &child_handle);
    if (!state->view) {
        vds_set_error(state, "webview_create failed; WebView2 runtime may be unavailable");
        DestroyWindow(state->child);
        delete state;
        return nullptr;
    }

    if (!vds_ok(webview_set_size(state->view, width, height, WEBVIEW_HINT_NONE))) {
        vds_set_error(state, "webview_set_size failed");
    }
    if (url && url[0]) {
        if (!vds_ok(webview_navigate(state->view, url))) {
            vds_set_error(state, "initial navigation failed");
        }
    }
    ShowWindow(state->child, SW_SHOW);
    UpdateWindow(state->child);
    return state;
}

extern "C" __declspec(dllexport)
int vds_webview2_navigate(void *handle, const char *url) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state || !state->view || !url || !url[0]) return 0;
    const auto result = webview_navigate(state->view, url);
    if (!vds_ok(result)) {
        vds_set_error(state, "navigation failed");
        return 0;
    }
    return 1;
}

extern "C" __declspec(dllexport)
int vds_webview2_set_bounds(void *handle, int x, int y, int width, int height) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state || !state->child || width <= 0 || height <= 0) return 0;
    if (!MoveWindow(state->child, x, y, width, height, TRUE)) {
        vds_set_error(state, "MoveWindow failed");
        return 0;
    }
    if (state->view && !vds_ok(webview_set_size(state->view, width, height, WEBVIEW_HINT_NONE))) {
        vds_set_error(state, "webview resize failed");
        return 0;
    }
    return 1;
}

extern "C" __declspec(dllexport)
int vds_webview2_set_visible(void *handle, int visible) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state || !state->child) return 0;
    ShowWindow(state->child, visible ? SW_SHOW : SW_HIDE);
    return 1;
}

extern "C" __declspec(dllexport)
void vds_webview2_destroy(void *handle) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state) return;
    if (state->view) {
        webview_destroy(state->view);
        state->view = nullptr;
    }
    if (state->child && IsWindow(state->child)) {
        DestroyWindow(state->child);
        state->child = NULL;
    }
    delete state;
}

extern "C" __declspec(dllexport)
const char *vds_webview2_last_error(void *handle) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (state && !state->last_error.empty()) return state->last_error.c_str();
    return g_vds_webview2_error.c_str();
}
