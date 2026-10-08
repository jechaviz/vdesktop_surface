#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <new>
#include <string>
#include <vector>
#include <cstring>
#include <cctype>
#include <wrl.h>

#include "webview/webview.h"

struct VdsWebView2Surface {
    HWND child = NULL;
    webview_t view = nullptr;
    std::string last_error;
    std::string url;
    std::string title;
    std::string visible_text;
    std::string controls_text;
    std::string structure_text;
    uint64_t load_count = 0;
    uint64_t state_count = 0;
    uint64_t action_count = 0;
    std::string action_id;
    bool action_ok = false;
    std::string action_message;
    uint64_t download_count = 0;
    std::string download_url;
    std::string download_path;
    std::string download_mime;
    std::string download_state;
    int64_t download_bytes = 0;
    int64_t download_total = -1;
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
        state->structure_text = values.size() >= 5 ? values[4] : "";
        state->state_count++;
        if (values.size() >= 6 && values[5] == "load") {
            state->load_count++;
        }
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

static std::wstring vds_widen(const char *value) {
    if (!value || !value[0]) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::vector<wchar_t> buffer(static_cast<size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
        buffer.data(), needed) <= 0) {
        return std::wstring();
    }
    return std::wstring(buffer.data());
}

static std::string vds_narrow(const wchar_t *value) {
    if (!value || !value[0]) return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::vector<char> buffer(static_cast<size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
        buffer.data(), needed, nullptr, nullptr) <= 0) {
        return std::string();
    }
    return std::string(buffer.data());
}

static void vds_update_download(VdsWebView2Surface *state,
    ICoreWebView2DownloadOperation *download) {
    if (!state || !download) return;
    LPWSTR uri = nullptr;
    LPWSTR path = nullptr;
    LPWSTR mime = nullptr;
    INT64 bytes = 0;
    INT64 total = -1;
    COREWEBVIEW2_DOWNLOAD_STATE download_state = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
    if (SUCCEEDED(download->get_Uri(&uri)) && uri) state->download_url = vds_narrow(uri);
    if (SUCCEEDED(download->get_ResultFilePath(&path)) && path) state->download_path = vds_narrow(path);
    if (SUCCEEDED(download->get_MimeType(&mime)) && mime) state->download_mime = vds_narrow(mime);
    if (SUCCEEDED(download->get_BytesReceived(&bytes))) state->download_bytes = bytes;
    if (SUCCEEDED(download->get_TotalBytesToReceive(&total))) state->download_total = total;
    if (SUCCEEDED(download->get_State(&download_state))) {
        switch (download_state) {
        case COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED:
            state->download_state = "completed";
            break;
        case COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED:
            state->download_state = "interrupted";
            break;
        default:
            state->download_state = "in_progress";
            break;
        }
    }
    if (uri) CoTaskMemFree(uri);
    if (path) CoTaskMemFree(path);
    if (mime) CoTaskMemFree(mime);
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
    return 7;
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

    auto *controller = static_cast<ICoreWebView2Controller *>(
        webview_get_native_handle(state->view, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
    if (controller) {
        ICoreWebView2 *core = nullptr;
        if (SUCCEEDED(controller->get_CoreWebView2(&core)) && core) {
            ICoreWebView2_4 *core4 = nullptr;
            if (SUCCEEDED(core->QueryInterface(IID_PPV_ARGS(&core4))) && core4) {
                EventRegistrationToken download_token{};
                auto handler = Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                    [state](ICoreWebView2 *, ICoreWebView2DownloadStartingEventArgs *args) -> HRESULT {
                        if (!state || !args) return S_OK;
                        args->put_Handled(TRUE);
                        ICoreWebView2DownloadOperation *download = nullptr;
                        if (FAILED(args->get_DownloadOperation(&download)) || !download) {
                            state->download_count++;
                            state->download_state = "interrupted";
                            return S_OK;
                        }
                        state->download_count++;
                        vds_update_download(state, download);
                        EventRegistrationToken state_token{};
                        auto state_handler = Microsoft::WRL::Callback<ICoreWebView2StateChangedEventHandler>(
                            [state](ICoreWebView2DownloadOperation *current, IUnknown *) -> HRESULT {
                                vds_update_download(state, current);
                                return S_OK;
                            });
                        download->add_StateChanged(state_handler.Get(), &state_token);
                        download->Release();
                        return S_OK;
                    });
                if (FAILED(core4->add_DownloadStarting(handler.Get(), &download_token))) {
                    vds_set_error(state, "failed to subscribe to WebView2 downloads");
                }
                core4->Release();
            }
            core->Release();
        }
    }
    const char *probe_js =
        "(function(){"
        "function clean(v){return String(v||'').replace(/[\\t\\r\\n]+/g,' ').trim();}"
        "function send(kind){"
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
        "function clip(v,n){v=clean(v);return v.length>n?v.slice(0,n):v;}"
        "var desc=document.querySelector(\"meta[name=description],meta[property='og:description']\");"
        "var canonical=document.querySelector('link[rel~=canonical]');"
        "var structure={"
        "description:clip(desc&&desc.content||'',1000),"
        "language:clip(document.documentElement&&document.documentElement.lang||'',32),"
        "canonical_url:clip(canonical&&canonical.href||'',2048),"
        "headings:[...document.querySelectorAll('h1,h2,h3,h4,h5,h6')].slice(0,128).map(function(e){return {level:Number(e.tagName.slice(1))||0,text:clip(e.innerText||e.textContent||'',500)};}),"
        "links:[...document.querySelectorAll('a[href]')].slice(0,256).map(function(e){return {text:clip(e.innerText||e.textContent||'',500),href:clip(e.href||'',2048)};}),"
        "images:[...document.images].slice(0,128).map(function(e){return {alt:clip(e.alt||'',500),src:clip(e.currentSrc||e.src||'',2048)};}),"
        "tables:[...document.querySelectorAll('table')].slice(0,32).map(function(table){"
        "var rows=[...table.rows];var cols=0;rows.forEach(function(r){cols=Math.max(cols,r.cells.length);});"
        "return {caption:clip(table.caption&&table.caption.innerText||'',500),"
        "headers:[...table.querySelectorAll('th')].slice(0,32).map(function(h){return clip(h.innerText||h.textContent||'',300);}),"
        "row_count:rows.length,column_count:cols};})"
        "};"
        "var structureJson=JSON.stringify(structure);"
        "if(structureJson.length>240000){"
        "structure.links=structure.links.slice(0,96);"
        "structure.images=structure.images.slice(0,64);"
        "structure.tables=structure.tables.slice(0,16);"
        "structure.headings=structure.headings.slice(0,96);"
        "structureJson=JSON.stringify(structure);"
        "}"
        "if(structureJson.length>240000){"
        "structure.links=structure.links.slice(0,32);"
        "structure.images=structure.images.slice(0,24);"
        "structure.tables=structure.tables.slice(0,8);"
        "structure.headings=structure.headings.slice(0,64);"
        "structureJson=JSON.stringify(structure);"
        "}"
        "if(structureJson.length>240000){"
        "structure.links=[];structure.images=[];structure.tables=[];"
        "structureJson=JSON.stringify(structure);"
        "}"
        "window.__vds_state(encodeURIComponent(location.href),"
        "encodeURIComponent(document.title||''),encodeURIComponent(t),encodeURIComponent(controls),"
        "encodeURIComponent(structureJson),encodeURIComponent(kind||'state'));"
        "}"
        "window.__vds_probe_state=send;"
        "var pendingProbe=0;"
        "function schedule(kind){"
        "if(pendingProbe)clearTimeout(pendingProbe);"
        "pendingProbe=setTimeout(function(){pendingProbe=0;send(kind||'state');},35);"
        "}"
        "var push=history.pushState,replace=history.replaceState;"
        "history.pushState=function(){var r=push.apply(this,arguments);schedule('route');return r;};"
        "history.replaceState=function(){var r=replace.apply(this,arguments);schedule('route');return r;};"
        "window.addEventListener('popstate',function(){schedule('route');});"
        "window.addEventListener('hashchange',function(){schedule('route');});"
        "if(window.MutationObserver){"
        "var observer=new MutationObserver(function(){schedule('state');});"
        "var startObserver=function(){if(document.documentElement)observer.observe(document.documentElement,{subtree:true,childList:true,attributes:true,characterData:true});};"
        "if(document.documentElement)startObserver();else document.addEventListener('DOMContentLoaded',startObserver,{once:true});"
        "}"
        "if(document.readyState==='complete'){setTimeout(function(){send('load');},0);}"
        "else{
        "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',function(){send('state');},{once:true});}"
        "window.addEventListener('load',function(){send('load');},{once:true});"
        "}"
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
int vds_webview2_set_cookie(void *handle, const char *name, const char *value,
    const char *domain, const char *path, int secure, int http_only,
    const char *same_site, int64_t expires_unix) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state || !state->view || !name || !name[0] || !domain || !domain[0]) return 0;

    auto *controller = static_cast<ICoreWebView2Controller *>(
        webview_get_native_handle(state->view, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
    if (!controller) {
        vds_set_error(state, "WebView2 browser controller is unavailable");
        return 0;
    }

    ICoreWebView2 *core = nullptr;
    HRESULT hr = controller->get_CoreWebView2(&core);
    if (FAILED(hr) || !core) {
        vds_set_error(state, "failed to acquire CoreWebView2 for cookie sync");
        return 0;
    }

    ICoreWebView2_2 *core2 = nullptr;
    hr = core->QueryInterface(IID_PPV_ARGS(&core2));
    core->Release();
    if (FAILED(hr) || !core2) {
        vds_set_error(state, "WebView2 cookie manager interface is unavailable");
        return 0;
    }

    ICoreWebView2CookieManager *manager = nullptr;
    hr = core2->get_CookieManager(&manager);
    core2->Release();
    if (FAILED(hr) || !manager) {
        vds_set_error(state, "failed to acquire WebView2 cookie manager");
        return 0;
    }

    const std::wstring wname = vds_widen(name);
    const std::wstring wvalue = vds_widen(value ? value : "");
    const std::wstring wdomain = vds_widen(domain);
    const std::wstring wpath = vds_widen(path && path[0] ? path : "/");
    if (wname.empty() || wdomain.empty()) {
        manager->Release();
        vds_set_error(state, "invalid UTF-8 cookie name or domain");
        return 0;
    }

    ICoreWebView2Cookie *cookie = nullptr;
    hr = manager->CreateCookie(wname.c_str(), wvalue.c_str(), wdomain.c_str(),
        wpath.empty() ? L"/" : wpath.c_str(), &cookie);
    if (FAILED(hr) || !cookie) {
        manager->Release();
        vds_set_error(state, "WebView2 CreateCookie failed");
        return 0;
    }

    cookie->put_IsSecure(secure ? TRUE : FALSE);
    cookie->put_IsHttpOnly(http_only ? TRUE : FALSE);
    if (expires_unix >= 0) {
        cookie->put_Expires(static_cast<double>(expires_unix));
    } else {
        cookie->put_Expires(-1.0);
    }

    std::string site = same_site ? same_site : "";
    for (char &ch : site) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    if (site == "none") {
        cookie->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_NONE);
    } else if (site == "strict") {
        cookie->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_STRICT);
    } else if (site == "lax") {
        cookie->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX);
    }

    hr = manager->AddOrUpdateCookie(cookie);
    cookie->Release();
    manager->Release();
    if (FAILED(hr)) {
        vds_set_error(state, "WebView2 AddOrUpdateCookie failed");
        return 0;
    }
    state->last_error.clear();
    return 1;
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
int vds_webview2_probe(void *handle, uint64_t *load_count, uint64_t *state_count,
    char *url, int url_cap,
    char *title, int title_cap, char *text, int text_cap, char *controls, int controls_cap,
    char *structure, int structure_cap, uint64_t *action_count, char *action_id,
    int action_id_cap, int *action_ok, char *action_message, int action_message_cap,
    uint64_t *download_count, char *download_url, int download_url_cap,
    char *download_path, int download_path_cap, char *download_mime, int download_mime_cap,
    char *download_state, int download_state_cap, int64_t *download_bytes,
    int64_t *download_total) {
    auto *state = static_cast<VdsWebView2Surface *>(handle);
    if (!state || !state->view) return 0;
    if (load_count) *load_count = state->load_count;
    if (state_count) *state_count = state->state_count;
    if (action_count) *action_count = state->action_count;
    if (action_ok) *action_ok = state->action_ok ? 1 : 0;
    if (download_count) *download_count = state->download_count;
    if (download_bytes) *download_bytes = state->download_bytes;
    if (download_total) *download_total = state->download_total;
    vds_copy_text(state->url, url, url_cap);
    vds_copy_text(state->title, title, title_cap);
    vds_copy_text(state->visible_text, text, text_cap);
    vds_copy_text(state->controls_text, controls, controls_cap);
    vds_copy_text(state->structure_text, structure, structure_cap);
    vds_copy_text(state->action_id, action_id, action_id_cap);
    vds_copy_text(state->action_message, action_message, action_message_cap);
    vds_copy_text(state->download_url, download_url, download_url_cap);
    vds_copy_text(state->download_path, download_path, download_path_cap);
    vds_copy_text(state->download_mime, download_mime, download_mime_cap);
    vds_copy_text(state->download_state, download_state, download_state_cap);
    return state->state_count > 0 || state->action_count > 0 || state->download_count > 0 ? 1 : 0;
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
