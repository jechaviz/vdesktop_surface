#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <new>
#include <string>
#include <vector>
#include <cstring>

#include "webview/webview.h"

struct VdsWebView2Surface {
    HWND child = NULL;
    webview_t view = nullptr;
    std::string last_error;
    std::string url;
    std::string title;
    std::string visible_text;
    std::string controls_text;
    uint64_t load_count = 0;
    uint64_t action_count = 0;
    std::string action_id;
    bool action_ok = false;
    std::string action_message;
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

static int vds_hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

static std::string vds_percent_decode(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int hi = vds_hex(value[i + 1]);
            int lo = vds_hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

static std::vector<std::string> vds_state_args(const char *request) {
    std::vector<std::string> out;
    if (!request) return out;
    std::string raw(request);
    if (raw.size() < 4 || raw.front() != '[' || raw.back() != ']') return out;
    size_t pos = 1;
    while (pos < raw.size() - 1) {
        if (raw[pos] != '"') return {};
        size_t end = raw.find('"', pos + 1);
        if (end == std::string::npos) return {};
        out.push_back(vds_percent_decode(raw.substr(pos + 1, end - pos - 1)));
        pos = end + 1;
        if (pos >= raw.size() - 1) break;
        if (raw[pos] != ',') return {};
        ++pos;
    }
    return out;
}

static void vds_state_callback(const char *seq, const char *request, void *arg) {
    auto *state = static_cast<VdsWebView2Surface *>(arg);
    if (!state) return;
    auto values = vds_state_args(request);
    if (values.size() >= 3) {
        state->url = values[0];
        state->title = values[1];
        state->visible_text = values[2];
        state->controls_text = values.size() >= 4 ? values[3] : "";
        state->load_count++;
        state->last_error.clear();
    }
    if (state->view && seq) {
        webview_return(state->view, seq, 0, "null");
    }
}

static void vds_action_callback(const char *seq, const char *request, void *arg) {
    auto *state = static_cast<VdsWebView2Surface *>(arg);
    if (!state) return;
    auto values = vds_state_args(request);
    if (values.size() >= 3) {
        state->action_id = values[0];
        state->action_ok = values[1] == "ok";
        state->action_message = values[2];
        state->action_count++;
    }
    if (state->view && seq) {
        webview_return(state->view, seq, 0, "null");
    }
}

static void vds_copy_text(const std::string &value, char *out, int cap) {
    if (!out || cap <= 0) return;
    size_t count = value.size();
    if (count > static_cast<size_t>(cap - 1)) count = static_cast<size_t>(cap - 1);
    if (count > 0) memcpy(out, value.data(), count);
    out[count] = 0;
}

extern "C" __declspec(dllexport)
int vds_webview2_abi_version(void) {
    return 2;
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

    if (!vds_ok(webview_bind(state->view, "__vds_state", vds_state_callback, state))) {
        vds_set_error(state, "failed to bind browser state probe");
    }
    if (!vds_ok(webview_bind(state->view, "__vds_action", vds_action_callback, state))) {
        vds_set_error(state, "failed to bind browser action receipt");
    }
    const char *probe_js =
        "(function(){"
        "function clean(v){return String(v||'').replace(/[\\t\\r\\n]+/g,' ').trim();}"
        "function send(){"
        "var t=(document.body&&document.body.innerText)||'';"
        "if(t.length>12000)t=t.slice(0,12000);"
        "var q='a[href],button,input:not([type=hidden]),textarea,select,[role=button],[role=link],[contenteditable=true]';"
        "var nodes=[...document.querySelectorAll(q)].filter(function(e){var r=e.getBoundingClientRect();return r.width>0&&r.height>0;}).slice(0,128);"
        "var controls=nodes.map(function(e,i){"
        "var id=e.id||('web-'+i);"
        "if(!e.id)e.setAttribute('data-hebrowser-control',id);"
        "var selector=e.id?('#'+CSS.escape(e.id)):('[data-hebrowser-control='+id+']');"
        "var role=e.getAttribute('role')||e.tagName.toLowerCase();"
        "var label=e.getAttribute('aria-label')||e.getAttribute('title')||e.getAttribute('placeholder')||e.innerText||e.value||'';"
        "var name=e.getAttribute('name')||'';"
        "var value=(e.type==='password')?'':(e.value||'');"
        "var href=e.href||'';"
        "var disabled=(e.disabled||e.getAttribute('aria-disabled')==='true')?'1':'0';"
        "var checked=(('checked' in e)&&e.checked)?'1':'0';"
        "return [id,role,label,name,value,selector,href,disabled,checked].map(clean).join('\\t');"
        "}).join('\\n');"
        "window.__vds_state(encodeURIComponent(location.href),"
        "encodeURIComponent(document.title||''),encodeURIComponent(t),encodeURIComponent(controls));"
        "}"
        "window.__vds_probe_state=send;"
        "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',send,{once:true});}"
        "else{setTimeout(send,0);}"
        "window.addEventListener('load',send,{once:true});"
        "})();";
    if (!vds_ok(webview_init(state->view, probe_js))) {
        vds_set_error(state, "failed to install browser state probe");
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

static std::string vds_js_string(const char *value) {
    std::string out;
    for (const char *p = value ? value : ""; *p; ++p) {
        switch (*p) {
        case '\\': out += "\\\\"; break;
        case '\'': out += "\\'"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out.push_back(*p); break;
        }
    }
    return out;
}

extern "C" __declspec(dllexport)
int vds_webview2_eval_action(void *handle, const char *action_id, const char *script) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state || !state->view || !action_id || !action_id[0] || !script) return 0;
    const std::string id = vds_js_string(action_id);
    std::string wrapped =
        "(async function(){try{" + std::string(script) +
        ";await window.__vds_action(encodeURIComponent('" + id +
        "'),'ok','');}catch(e){await window.__vds_action(encodeURIComponent('" + id +
        "'),'error',encodeURIComponent(String((e&&e.message)||e||'error')));}"
        "finally{if(window.__vds_probe_state){setTimeout(window.__vds_probe_state,0);}}})();";
    if (!vds_ok(webview_eval(state->view, wrapped.c_str()))) {
        vds_set_error(state, "action script dispatch failed");
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
int vds_webview2_probe(void *handle, uint64_t *load_count, char *url, int url_cap,
    char *title, int title_cap, char *text, int text_cap, char *controls, int controls_cap,
    uint64_t *action_count, char *action_id, int action_id_cap, int *action_ok,
    char *action_message, int action_message_cap) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state || !state->view) return 0;
    if (load_count) *load_count = state->load_count;
    if (action_count) *action_count = state->action_count;
    if (action_ok) *action_ok = state->action_ok ? 1 : 0;
    vds_copy_text(state->url, url, url_cap);
    vds_copy_text(state->title, title, title_cap);
    vds_copy_text(state->visible_text, text, text_cap);
    vds_copy_text(state->controls_text, controls, controls_cap);
    vds_copy_text(state->action_id, action_id, action_id_cap);
    vds_copy_text(state->action_message, action_message, action_message_cap);
    return state->load_count > 0 || state->action_count > 0 ? 1 : 0;
}

extern "C" __declspec(dllexport)
void vds_webview2_destroy(void *handle) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state) return;
    if (state->view) {
        webview_unbind(state->view, "__vds_action");
        webview_unbind(state->view, "__vds_state");
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
