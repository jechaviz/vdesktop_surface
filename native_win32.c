#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define VDS_FLAG_MAXIMIZED 1
#define VDS_FLAG_BORDERLESS 2
#define VDS_EVENT_CLOSE 1
#define VDS_EVENT_RESIZE 2
#define VDS_EVENT_POINTER_DOWN 3
#define VDS_EVENT_POINTER_MOVE 4
#define VDS_EVENT_KEY_DOWN 5
#define VDS_EVENT_TEXT 6
#define VDS_QUEUE_CAP 128

typedef struct VdsEvent {
    int kind;
    int x;
    int y;
    int width;
    int height;
    int key;
    uint32_t codepoint;
} VdsEvent;

typedef struct VdsWindow {
    HWND hwnd;
    char *payload;
    int alive;
    HFONT font;
    VdsEvent queue[VDS_QUEUE_CAP];
    int queue_head;
    int queue_tail;
} VdsWindow;

static const wchar_t *k_vds_class = L"VDesktopSurfaceWindow";
static ATOM g_vds_class_atom = 0;

static void vds_push_event(VdsWindow *state, VdsEvent event) {
    if (!state) return;
    int next = (state->queue_tail + 1) % VDS_QUEUE_CAP;
    if (next == state->queue_head) {
        state->queue_head = (state->queue_head + 1) % VDS_QUEUE_CAP;
    }
    state->queue[state->queue_tail] = event;
    state->queue_tail = next;
}

static int vds_pop_event(VdsWindow *state, VdsEvent *out) {
    if (!state || state->queue_head == state->queue_tail) return 0;
    if (out) *out = state->queue[state->queue_head];
    state->queue_head = (state->queue_head + 1) % VDS_QUEUE_CAP;
    return 1;
}

static COLORREF vds_color(uint32_t argb) {
    BYTE r = (BYTE)((argb >> 16) & 0xff);
    BYTE g = (BYTE)((argb >> 8) & 0xff);
    BYTE b = (BYTE)(argb & 0xff);
    return RGB(r, g, b);
}

static int vds_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    return -1;
}

static void vds_unescape(const char *input, char *output, size_t cap) {
    if (!output || cap == 0) return;
    size_t o = 0;
    for (size_t i = 0; input && input[i] && o + 1 < cap; i++) {
        if (input[i] == '%' && input[i + 1] && input[i + 2]) {
            int hi = vds_hex_value(input[i + 1]);
            int lo = vds_hex_value(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                output[o++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        output[o++] = input[i];
    }
    output[o] = 0;
}

static void vds_utf8_to_wide(const char *input, wchar_t *output, int cap) {
    if (!output || cap <= 0) return;
    output[0] = 0;
    if (!input || !input[0]) return;
    MultiByteToWideChar(CP_UTF8, 0, input, -1, output, cap);
    output[cap - 1] = 0;
}

static HFONT vds_font(VdsWindow *state) {
    if (!state) return NULL;
    if (!state->font) {
        state->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    return state->font;
}

static void vds_fill(HDC dc, int x, int y, int w, int h, uint32_t argb) {
    RECT r = {x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(vds_color(argb));
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

static void vds_round_fill(HDC dc, int x, int y, int w, int h, int radius, uint32_t argb) {
    HBRUSH brush = CreateSolidBrush(vds_color(argb));
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, x, y, x + w, y + h, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
}

static void vds_stroke(HDC dc, int x, int y, int w, int h, int width, uint32_t argb) {
    HPEN pen = CreatePen(PS_SOLID, width > 0 ? width : 1, vds_color(argb));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x, y, x + w, y + h);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void vds_line(HDC dc, int x1, int y1, int x2, int y2, int width, uint32_t argb) {
    HPEN pen = CreatePen(PS_SOLID, width > 0 ? width : 1, vds_color(argb));
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void vds_text(VdsWindow *state, HDC dc, int x, int y, int w, int h,
    uint32_t argb, const char *encoded) {
    char utf8[3072];
    wchar_t wide[3072];
    vds_unescape(encoded, utf8, sizeof(utf8));
    vds_utf8_to_wide(utf8, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    RECT r = {x, y, x + w, y + h};
    HGDIOBJ old_font = SelectObject(dc, vds_font(state));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, vds_color(argb));
    DrawTextW(dc, wide, -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
    SelectObject(dc, old_font);
}

static void vds_draw_payload(VdsWindow *state, HDC dc, RECT client) {
    vds_fill(dc, 0, 0, client.right - client.left, client.bottom - client.top, 0xff0f1115u);
    if (!state || !state->payload) return;

    const char *cursor = state->payload;
    char line[4096];
    int first = 1;
    while (*cursor) {
        const char *end = strchr(cursor, '\n');
        size_t len = end ? (size_t)(end - cursor) : strlen(cursor);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, cursor, len);
        line[len] = 0;
        if (first) {
            first = 0;
        } else if (line[0]) {
            int x = 0, y = 0, w = 0, h = 0, a = 0, b = 0;
            unsigned int color = 0;
            if (line[0] == 'R') {
                if (sscanf(line, "R|%d|%d|%d|%d|%u", &x, &y, &w, &h, &color) == 5)
                    vds_fill(dc, x, y, w, h, color);
            } else if (line[0] == 'Q') {
                if (sscanf(line, "Q|%d|%d|%d|%d|%d|%u", &x, &y, &w, &h, &a, &color) == 6)
                    vds_round_fill(dc, x, y, w, h, a, color);
            } else if (line[0] == 'S') {
                if (sscanf(line, "S|%d|%d|%d|%d|%d|%u", &x, &y, &w, &h, &a, &color) == 6)
                    vds_stroke(dc, x, y, w, h, a, color);
            } else if (line[0] == 'L') {
                if (sscanf(line, "L|%d|%d|%d|%d|%d|%u", &x, &y, &a, &b, &w, &color) == 6)
                    vds_line(dc, x, y, a, b, w, color);
            } else if (line[0] == 'T') {
                char encoded[3072] = {0};
                if (sscanf(line, "T|%d|%d|%d|%d|%u|%3071[^\n]",
                    &x, &y, &w, &h, &color, encoded) == 6)
                    vds_text(state, dc, x, y, w, h, color, encoded);
            }
        }
        if (!end) break;
        cursor = end + 1;
    }
}

static void vds_paint(VdsWindow *state, HWND hwnd, HDC target) {
    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return;

    HDC mem = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, w, h);
    if (!mem || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
        vds_draw_payload(state, target, client);
        return;
    }
    HGDIOBJ old_bitmap = SelectObject(mem, bitmap);
    vds_draw_payload(state, mem, client);
    BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
}

static LRESULT CALLBACK vds_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    VdsWindow *state = (VdsWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        state = (VdsWindow *)create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        if (state) state->hwnd = hwnd;
    }

    switch (msg) {
    case WM_CLOSE:
        if (state) {
            VdsEvent event = {0};
            event.kind = VDS_EVENT_CLOSE;
            vds_push_event(state, event);
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state) state->alive = 0;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (state) {
            VdsEvent event = {0};
            event.kind = VDS_EVENT_RESIZE;
            event.width = LOWORD(lparam);
            event.height = HIWORD(lparam);
            vds_push_event(state, event);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state) {
            SetFocus(hwnd);
            VdsEvent event = {0};
            event.kind = VDS_EVENT_POINTER_DOWN;
            event.x = GET_X_LPARAM(lparam);
            event.y = GET_Y_LPARAM(lparam);
            vds_push_event(state, event);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (state) {
            VdsEvent event = {0};
            event.kind = VDS_EVENT_POINTER_MOVE;
            event.x = GET_X_LPARAM(lparam);
            event.y = GET_Y_LPARAM(lparam);
            vds_push_event(state, event);
        }
        return 0;
    case WM_KEYDOWN:
        if (state) {
            VdsEvent event = {0};
            event.kind = VDS_EVENT_KEY_DOWN;
            event.key = (int)wparam;
            vds_push_event(state, event);
        }
        return 0;
    case WM_CHAR:
        if (state) {
            VdsEvent event = {0};
            event.kind = VDS_EVENT_TEXT;
            event.codepoint = (uint32_t)wparam;
            vds_push_event(state, event);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        vds_paint(state, hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

static int vds_register_class(void) {
    if (g_vds_class_atom) return 1;
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = vds_wndproc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = k_vds_class;
    g_vds_class_atom = RegisterClassExW(&wc);
    return g_vds_class_atom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void *vdesktop_surface_open(const char *title, int width, int height, int flags, const char *payload) {
    if (!vds_register_class()) return NULL;
    VdsWindow *state = (VdsWindow *)calloc(1, sizeof(VdsWindow));
    if (!state) return NULL;
    state->alive = 1;
    state->payload = payload ? _strdup(payload) : _strdup("");

    wchar_t wide_title[512];
    vds_utf8_to_wide(title && title[0] ? title : "V Desktop", wide_title,
        (int)(sizeof(wide_title) / sizeof(wide_title[0])));

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (flags & VDS_FLAG_BORDERLESS)
        style = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

    RECT desired = {0, 0, width > 0 ? width : 1200, height > 0 ? height : 800};
    AdjustWindowRect(&desired, style, FALSE);
    int window_w = desired.right - desired.left;
    int window_h = desired.bottom - desired.top;

    HWND hwnd = CreateWindowExW(0, k_vds_class, wide_title, style,
        CW_USEDEFAULT, CW_USEDEFAULT, window_w, window_h,
        NULL, NULL, GetModuleHandleW(NULL), state);
    if (!hwnd) {
        free(state->payload);
        free(state);
        return NULL;
    }
    state->hwnd = hwnd;
    ShowWindow(hwnd, (flags & VDS_FLAG_MAXIMIZED) ? SW_MAXIMIZE : SW_SHOW);
    UpdateWindow(hwnd);
    return state;
}

int vdesktop_surface_alive(void *handle) {
    VdsWindow *state = (VdsWindow *)handle;
    return state && state->alive;
}

int vdesktop_surface_poll(void *handle, int *kind, int *x, int *y, int *width,
    int *height, int *key, uint32_t *codepoint) {
    VdsWindow *state = (VdsWindow *)handle;
    if (!state) return 0;

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            state->alive = 0;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    VdsEvent event;
    if (!vds_pop_event(state, &event)) return 0;
    if (kind) *kind = event.kind;
    if (x) *x = event.x;
    if (y) *y = event.y;
    if (width) *width = event.width;
    if (height) *height = event.height;
    if (key) *key = event.key;
    if (codepoint) *codepoint = event.codepoint;
    return 1;
}

void vdesktop_surface_set_payload(void *handle, const char *payload) {
    VdsWindow *state = (VdsWindow *)handle;
    if (!state) return;
    char *next = payload ? _strdup(payload) : _strdup("");
    if (!next) return;
    free(state->payload);
    state->payload = next;
    if (state->hwnd) InvalidateRect(state->hwnd, NULL, FALSE);
}

void vdesktop_surface_present(void *handle) {
    VdsWindow *state = (VdsWindow *)handle;
    if (state && state->hwnd) {
        InvalidateRect(state->hwnd, NULL, FALSE);
        UpdateWindow(state->hwnd);
    }
}

void vdesktop_surface_destroy(void *handle) {
    VdsWindow *state = (VdsWindow *)handle;
    if (!state) return;
    if (state->hwnd && IsWindow(state->hwnd)) DestroyWindow(state->hwnd);
    if (state->font) DeleteObject(state->font);
    free(state->payload);
    free(state);
}


uint64_t vdesktop_surface_native_handle(void *handle) {
    VdsWindow *state = (VdsWindow *)handle;
    if (!state || !state->hwnd) return 0;
    return (uint64_t)(uintptr_t)state->hwnd;
}
