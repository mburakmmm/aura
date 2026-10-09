#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "paint.h"
#include "plugin.h"
#include "queue.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uiautomation.h>

#include <stdlib.h>
#include <string.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

enum {
    AURA_POINTER_DOWN = 1,
    AURA_POINTER_MOVE = 2,
    AURA_POINTER_UP = 3,
    AURA_POINTER_CANCEL = 4,
    AURA_KEY_DOWN = 5,
    AURA_KEY_UP = 6,
    AURA_RESIZE = 7,
    AURA_SCROLL = 8,
    AURA_IME_INSERT = 10,
    AURA_WINDOW_FOCUS = 13,
    AURA_FILE_DROP = 14,
    AURA_AX_ACTION = 15
};

struct AuraWindow;

class AuraProvider : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot, public IInvokeProvider {
public:
    AuraProvider(AuraWindow *owner, int index) : refs_(1), owner_(owner), index_(index) {}
    STDMETHODIMP QueryInterface(REFIID iid, void **object);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    STDMETHODIMP get_ProviderOptions(ProviderOptions *options);
    STDMETHODIMP GetPatternProvider(PATTERNID pattern, IUnknown **provider);
    STDMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT *value);
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple **provider);
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment **fragment);
    STDMETHODIMP GetRuntimeId(SAFEARRAY **ids);
    STDMETHODIMP get_BoundingRectangle(UiaRect *bounds);
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY **roots);
    STDMETHODIMP SetFocus();
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot **root);
    STDMETHODIMP ElementProviderFromPoint(double x, double y, IRawElementProviderFragment **fragment);
    STDMETHODIMP GetFocus(IRawElementProviderFragment **fragment);
    STDMETHODIMP Invoke();
    int index() const { return index_; }

private:
    LONG refs_;
    AuraWindow *owner_;
    int index_;
};

struct AuraWindow {
    int alive;
    int should_close;
    HWND hwnd;
    int text_focus;
    double scale;
    AuraQueue queue;
    AuraAx ax;
    uint8_t *pixels;
    int pixel_w;
    int pixel_h;
    int pixel_stride;
    double frame_time;
    AuraProvider *root_provider;
    AuraProvider **child_providers;
    int child_count;
    HWND plugin_hwnd[8];
    int64_t plugin_ids[8];
    char *title;
    int cursor_kind;
    int shown;
};

#define AURA_MAX_WINDOWS 32
static AuraWindow *g_windows[AURA_MAX_WINDOWS];
static char *g_clipboard = NULL;
static LARGE_INTEGER g_qpc_freq;
static int g_qpc_ready = 0;

static AuraWindow *window_from_hwnd(HWND hwnd) {
    return (AuraWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static AuraWindow *window_get(int64_t handle, int require_alive) {
    AuraWindow *window = NULL;
    if (handle <= 0 || handle > AURA_MAX_WINDOWS) {
        return NULL;
    }
    window = g_windows[handle - 1];
    if (window == NULL) {
        return NULL;
    }
    if (require_alive && !window->alive) {
        return NULL;
    }
    return window;
}

static double qpc_seconds() {
    LARGE_INTEGER now;
    if (!g_qpc_ready) {
        QueryPerformanceFrequency(&g_qpc_freq);
        if (g_qpc_freq.QuadPart <= 0) {
            g_qpc_freq.QuadPart = 1;
        }
        g_qpc_ready = 1;
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)g_qpc_freq.QuadPart;
}

static void copy_utf8(const wchar_t *wide, char **out) {
    int bytes = 0;
    char *text = NULL;
    free(*out);
    *out = NULL;
    if (wide == NULL) {
        *out = _strdup("");
        return;
    }
    bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0) {
        *out = _strdup("");
        return;
    }
    text = (char *)malloc((size_t)bytes);
    if (text == NULL) {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, bytes, NULL, NULL);
    *out = text;
}

static void release_providers(AuraWindow *window) {
    int i = 0;
    if (window->root_provider != NULL) {
        window->root_provider->Release();
        window->root_provider = NULL;
    }
    for (i = 0; i < window->child_count; i++) {
        if (window->child_providers[i] != NULL) {
            window->child_providers[i]->Release();
        }
    }
    free(window->child_providers);
    window->child_providers = NULL;
    window->child_count = 0;
}

static void publish_providers(AuraWindow *window) {
    int i = 0;
    release_providers(window);
    window->root_provider = new AuraProvider(window, -1);
    window->child_count = (int)aura_ax_count_nodes(&window->ax);
    if (window->child_count <= 0) {
        return;
    }
    window->child_providers = (AuraProvider **)calloc((size_t)window->child_count, sizeof(AuraProvider *));
    if (window->child_providers == NULL) {
        window->child_count = 0;
        return;
    }
    for (i = 0; i < window->child_count; i++) {
        window->child_providers[i] = new AuraProvider(window, i);
    }
    UiaRaiseAutomationEvent(window->root_provider, UIA_StructureChangedEventId);
}

STDMETHODIMP AuraProvider::QueryInterface(REFIID iid, void **object) {
    if (object == NULL) {
        return E_POINTER;
    }
    *object = NULL;
    if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
        *object = static_cast<IRawElementProviderSimple *>(this);
    } else if (iid == IID_IRawElementProviderFragment) {
        *object = static_cast<IRawElementProviderFragment *>(this);
    } else if (iid == IID_IRawElementProviderFragmentRoot && index_ < 0) {
        *object = static_cast<IRawElementProviderFragmentRoot *>(this);
    } else if (iid == IID_IInvokeProvider) {
        *object = static_cast<IInvokeProvider *>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) AuraProvider::AddRef() {
    return (ULONG)InterlockedIncrement(&refs_);
}

STDMETHODIMP_(ULONG) AuraProvider::Release() {
    LONG next = InterlockedDecrement(&refs_);
    if (next == 0) {
        delete this;
    }
    return (ULONG)next;
}

STDMETHODIMP AuraProvider::get_ProviderOptions(ProviderOptions *options) {
    if (options == NULL) {
        return E_POINTER;
    }
    *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

STDMETHODIMP AuraProvider::GetPatternProvider(PATTERNID pattern, IUnknown **provider) {
    if (provider == NULL) {
        return E_POINTER;
    }
    *provider = NULL;
    if (pattern == UIA_InvokePatternId) {
        *provider = static_cast<IInvokeProvider *>(this);
        AddRef();
    }
    return S_OK;
}

STDMETHODIMP AuraProvider::GetPropertyValue(PROPERTYID property, VARIANT *value) {
    const AuraNode *node = NULL;
    wchar_t wide[512];
    const char *text = "";
    if (value == NULL) {
        return E_POINTER;
    }
    VariantInit(value);
    if (index_ >= 0 && index_ < aura_ax_count_nodes(&owner_->ax)) {
        node = &owner_->ax.nodes[index_];
    }
    if (property == UIA_NamePropertyId) {
        text = node != NULL && node->label != NULL ? node->label : "Aura";
    } else if (property == UIA_ControlTypePropertyId) {
        value->vt = VT_I4;
        value->lVal = UIA_GroupControlTypeId;
        if (node != NULL && node->role != NULL && strcmp(node->role, "button") == 0) {
            value->lVal = UIA_ButtonControlTypeId;
        } else if (node != NULL && node->role != NULL && strcmp(node->role, "toggle") == 0) {
            value->lVal = UIA_CheckBoxControlTypeId;
        } else if (node != NULL && node->role != NULL && strcmp(node->role, "text") == 0) {
            value->lVal = UIA_TextControlTypeId;
        } else if (node != NULL && node->role != NULL && strcmp(node->role, "text-field") == 0) {
            value->lVal = UIA_EditControlTypeId;
        }
        return S_OK;
    } else if (property == UIA_ValueValuePropertyId || property == UIA_HelpTextPropertyId) {
        text = node != NULL && node->value != NULL ? node->value : "";
    } else if (property == UIA_ToggleToggleStatePropertyId && node != NULL) {
        value->vt = VT_I4;
        value->lVal = node->checked ? ToggleState_On : ToggleState_Off;
        return S_OK;
    } else if (property == UIA_IsEnabledPropertyId) {
        value->vt = VT_BOOL;
        value->boolVal = VARIANT_TRUE;
        return S_OK;
    } else {
        return S_OK;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, 512);
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocString(wide);
    return value->bstrVal != NULL ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP AuraProvider::get_HostRawElementProvider(IRawElementProviderSimple **provider) {
    if (provider == NULL) {
        return E_POINTER;
    }
    *provider = NULL;
    if (index_ >= 0 || owner_->hwnd == NULL) {
        return S_OK;
    }
    return UiaHostProviderFromHwnd(owner_->hwnd, provider);
}

STDMETHODIMP AuraProvider::Navigate(NavigateDirection direction, IRawElementProviderFragment **fragment) {
    int count = 0;
    if (fragment == NULL) {
        return E_POINTER;
    }
    *fragment = NULL;
    count = (int)aura_ax_count_nodes(&owner_->ax);
    if (direction == NavigateDirection_Parent && index_ >= 0 && owner_->root_provider != NULL) {
        *fragment = owner_->root_provider;
        owner_->root_provider->AddRef();
    } else if (direction == NavigateDirection_FirstChild && index_ < 0 && count > 0 && owner_->child_providers != NULL) {
        *fragment = owner_->child_providers[0];
        owner_->child_providers[0]->AddRef();
    } else if (direction == NavigateDirection_LastChild && index_ < 0 && count > 0 && owner_->child_providers != NULL) {
        *fragment = owner_->child_providers[count - 1];
        owner_->child_providers[count - 1]->AddRef();
    } else if (direction == NavigateDirection_NextSibling && index_ >= 0 && index_ + 1 < count && owner_->child_providers != NULL) {
        *fragment = owner_->child_providers[index_ + 1];
        owner_->child_providers[index_ + 1]->AddRef();
    } else if (direction == NavigateDirection_PreviousSibling && index_ > 0 && owner_->child_providers != NULL) {
        *fragment = owner_->child_providers[index_ - 1];
        owner_->child_providers[index_ - 1]->AddRef();
    }
    return S_OK;
}

STDMETHODIMP AuraProvider::GetRuntimeId(SAFEARRAY **ids) {
    int values[2];
    if (ids == NULL) {
        return E_POINTER;
    }
    values[0] = UiaAppendRuntimeId;
    values[1] = index_ + 2;
    *ids = SafeArrayCreateVector(VT_I4, 0, 2);
    if (*ids == NULL) {
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < 2; i++) {
        SafeArrayPutElement(*ids, &i, &values[i]);
    }
    return S_OK;
}

STDMETHODIMP AuraProvider::get_BoundingRectangle(UiaRect *bounds) {
    POINT origin;
    double scale = 1.0;
    if (bounds == NULL) {
        return E_POINTER;
    }
    memset(bounds, 0, sizeof(*bounds));
    origin.x = 0;
    origin.y = 0;
    ClientToScreen(owner_->hwnd, &origin);
    scale = owner_->scale < 1.0 ? 1.0 : owner_->scale;
    if (index_ < 0) {
        RECT rect;
        GetClientRect(owner_->hwnd, &rect);
        bounds->left = origin.x;
        bounds->top = origin.y;
        bounds->width = (rect.right - rect.left);
        bounds->height = (rect.bottom - rect.top);
        return S_OK;
    }
    bounds->left = origin.x + aura_ax_bound(&owner_->ax, index_, 0) * scale;
    bounds->top = origin.y + aura_ax_bound(&owner_->ax, index_, 1) * scale;
    bounds->width = aura_ax_bound(&owner_->ax, index_, 2) * scale;
    bounds->height = aura_ax_bound(&owner_->ax, index_, 3) * scale;
    return S_OK;
}

STDMETHODIMP AuraProvider::GetEmbeddedFragmentRoots(SAFEARRAY **roots) {
    if (roots == NULL) {
        return E_POINTER;
    }
    *roots = NULL;
    return S_OK;
}

STDMETHODIMP AuraProvider::SetFocus() {
    ::SetFocus(owner_->hwnd);
    return S_OK;
}

STDMETHODIMP AuraProvider::get_FragmentRoot(IRawElementProviderFragmentRoot **root) {
    if (root == NULL) {
        return E_POINTER;
    }
    *root = owner_->root_provider;
    if (*root != NULL) {
        owner_->root_provider->AddRef();
    }
    return S_OK;
}

STDMETHODIMP AuraProvider::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment **fragment) {
    POINT origin;
    int i = 0;
    int count = 0;
    double local_x = 0.0;
    double local_y = 0.0;
    if (fragment == NULL) {
        return E_POINTER;
    }
    *fragment = this;
    AddRef();
    origin.x = 0;
    origin.y = 0;
    ClientToScreen(owner_->hwnd, &origin);
    local_x = x - origin.x;
    local_y = y - origin.y;
    count = (int)aura_ax_count_nodes(&owner_->ax);
    for (i = count - 1; i >= 0; i--) {
        double left = aura_ax_bound(&owner_->ax, i, 0);
        double top = aura_ax_bound(&owner_->ax, i, 1);
        double width = aura_ax_bound(&owner_->ax, i, 2);
        double height = aura_ax_bound(&owner_->ax, i, 3);
        if (local_x >= left && local_y >= top && local_x < left + width && local_y < top + height && owner_->child_providers != NULL) {
            (*fragment)->Release();
            *fragment = owner_->child_providers[i];
            owner_->child_providers[i]->AddRef();
            return S_OK;
        }
    }
    return S_OK;
}

STDMETHODIMP AuraProvider::GetFocus(IRawElementProviderFragment **fragment) {
    if (fragment == NULL) {
        return E_POINTER;
    }
    *fragment = this;
    AddRef();
    return S_OK;
}

STDMETHODIMP AuraProvider::Invoke() {
    int64_t press = -1;
    if (index_ >= 0) {
        press = aura_ax_press_node(&owner_->ax, index_);
    }
    if (press >= 0) {
        aura_queue_push(&owner_->queue, AURA_AX_ACTION, 0, 0, press, 0);
        return S_OK;
    }
    return UIA_E_ELEMENTNOTAVAILABLE;
}

static void paint_window(AuraWindow *window) {
    BITMAPINFO info;
    HDC dc = NULL;
    RECT rect;
    if (window->pixels == NULL || window->hwnd == NULL) {
        return;
    }
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = window->pixel_w;
    info.bmiHeader.biHeight = -window->pixel_h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    GetClientRect(window->hwnd, &rect);
    dc = GetDC(window->hwnd);
    if (dc != NULL) {
        StretchDIBits(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top, 0, 0, window->pixel_w, window->pixel_h, window->pixels, &info, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(window->hwnd, dc);
    }
}

static LRESULT CALLBACK aura_wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    AuraWindow *window = window_from_hwnd(hwnd);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *created = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)created->lpCreateParams);
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    if (window == NULL || !window->alive) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    if (message == WM_CLOSE) {
        window->should_close = 1;
        return 0;
    }
    if (message == WM_SETFOCUS) {
        aura_queue_push(&window->queue, AURA_WINDOW_FOCUS, 0, 0, 0, 0);
        return 0;
    }
    if (message == WM_KILLFOCUS) {
        aura_queue_push(&window->queue, AURA_POINTER_CANCEL, 0, 0, 0, 0);
        return 0;
    }
    if (message == WM_SIZE) {
        aura_queue_push(&window->queue, AURA_RESIZE, LOWORD(lparam), HIWORD(lparam), 0, 0);
        return 0;
    }
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) {
        SetFocus(hwnd);
        aura_queue_push(&window->queue, AURA_POINTER_DOWN, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0, message == WM_RBUTTONDOWN ? 2 : 1);
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        aura_queue_push(&window->queue, AURA_POINTER_MOVE, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0, (wparam & MK_LBUTTON) ? 1 : 0);
        return 0;
    }
    if (message == WM_LBUTTONUP || message == WM_RBUTTONUP) {
        aura_queue_push(&window->queue, AURA_POINTER_UP, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0, message == WM_RBUTTONUP ? 2 : 1);
        return 0;
    }
    if (message == WM_MOUSEWHEEL) {
        aura_queue_push(&window->queue, AURA_SCROLL, 0, GET_WHEEL_DELTA_WPARAM(wparam), 0, 0);
        return 0;
    }
    if (message == WM_KEYDOWN || message == WM_KEYUP) {
        int64_t mods = 0;
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            mods |= 2;
        }
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            mods |= 1;
        }
        aura_queue_push(&window->queue, message == WM_KEYDOWN ? AURA_KEY_DOWN : AURA_KEY_UP, 0, 0, (int64_t)wparam, mods);
        return 0;
    }
    if (message == WM_CHAR) {
        wchar_t chars[2];
        char *utf8 = NULL;
        chars[0] = (wchar_t)wparam;
        chars[1] = 0;
        copy_utf8(chars, &utf8);
        aura_queue_push_text(&window->queue, AURA_IME_INSERT, utf8 != NULL ? utf8 : "");
        free(utf8);
        return 0;
    }
    if (message == WM_DROPFILES) {
        HDROP drop = (HDROP)wparam;
        wchar_t path[MAX_PATH];
        POINT point;
        char *utf8 = NULL;
        if (DragQueryFileW(drop, 0, path, MAX_PATH) > 0) {
            point.x = 0;
            point.y = 0;
            DragQueryPoint(drop, &point);
            copy_utf8(path, &utf8);
            aura_queue_push_text(&window->queue, AURA_FILE_DROP, utf8 != NULL ? utf8 : "");
            if (window->queue.count > 0) {
                window->queue.events[window->queue.count - 1].x = point.x;
                window->queue.events[window->queue.count - 1].y = point.y;
            }
            free(utf8);
        }
        DragFinish(drop);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        BeginPaint(hwnd, &paint);
        paint_window(window);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_GETOBJECT) {
        if ((long)lparam == UiaRootObjectId && window->root_provider != NULL) {
            return UiaReturnRawElementProvider(hwnd, wparam, lparam, window->root_provider);
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void ensure_class() {
    static int ready = 0;
    WNDCLASSW klass;
    if (ready) {
        return;
    }
    memset(&klass, 0, sizeof(klass));
    klass.lpfnWndProc = aura_wndproc;
    klass.hInstance = GetModuleHandleW(NULL);
    klass.lpszClassName = L"AuraWindow";
    klass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    RegisterClassW(&klass);
    ready = 1;
}

static void copy_frame_pixels(AuraWindow *window) {
    const uint8_t *source = aura_paint_pixels();
    int64_t width = aura_paint_pixel_width();
    int64_t height = aura_paint_pixel_height();
    int64_t stride = aura_paint_stride();
    size_t bytes = 0;
    uint8_t *next = NULL;
    if (source == NULL || width <= 0 || height <= 0 || stride <= 0) {
        return;
    }
    bytes = (size_t)stride * (size_t)height;
    next = (uint8_t *)malloc(bytes);
    if (next == NULL) {
        return;
    }
    memcpy(next, source, bytes);
    for (size_t i = 0; i + 3 < bytes; i += 4) {
        uint8_t red = next[i];
        next[i] = next[i + 2];
        next[i + 2] = red;
    }
    free(window->pixels);
    window->pixels = next;
    window->pixel_w = (int)width;
    window->pixel_h = (int)height;
    window->pixel_stride = (int)stride;
}

extern "C" int64_t aura_window_create(int64_t width, int64_t height, const char *title, int64_t visible) {
    int slot = -1;
    int i = 0;
    AuraWindow *window = NULL;
    wchar_t wide_title[256];
    RECT rect;
    DWORD style = WS_OVERLAPPEDWINDOW;
    ensure_class();
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        if (g_windows[i] == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return 0;
    }
    window = (AuraWindow *)calloc(1, sizeof(AuraWindow));
    if (window == NULL) {
        return 0;
    }
    window->alive = 1;
    window->scale = 1.0;
    aura_queue_init(&window->queue);
    aura_ax_init(&window->ax);
    MultiByteToWideChar(CP_UTF8, 0, title != NULL ? title : "Aura", -1, wide_title, 256);
    rect.left = 0;
    rect.top = 0;
    rect.right = (LONG)width;
    rect.bottom = (LONG)height;
    AdjustWindowRect(&rect, style, FALSE);
    window->hwnd = CreateWindowExW(0, L"AuraWindow", wide_title, style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, GetModuleHandleW(NULL), window);
    window->title = _strdup(title != NULL ? title : "Aura");
    if (window->hwnd == NULL) {
        aura_queue_free(&window->queue);
        aura_ax_free(&window->ax);
        free(window->title);
        free(window);
        return 0;
    }
    DragAcceptFiles(window->hwnd, TRUE);
    window->scale = (double)GetDpiForWindow(window->hwnd) / 96.0;
    if (window->scale < 1.0) {
        window->scale = 1.0;
    }
    window->root_provider = new AuraProvider(window, -1);
    g_windows[slot] = window;
    window->shown = visible != 0;
    if (visible != 0) {
        ShowWindow(window->hwnd, SW_SHOW);
        UpdateWindow(window->hwnd);
    }
    return (int64_t)slot + 1;
}

extern "C" int64_t aura_window_destroy(int64_t handle) {
    AuraWindow *window = NULL;
    int i = 0;
    if (handle <= 0 || handle > AURA_MAX_WINDOWS) {
        return -1;
    }
    window = g_windows[handle - 1];
    if (window == NULL) {
        return 0;
    }
    if (!window->alive) {
        return 0;
    }
    window->alive = 0;
    for (i = 0; i < 8; i++) {
        if (window->plugin_hwnd[i] != NULL) {
            DestroyWindow(window->plugin_hwnd[i]);
            window->plugin_hwnd[i] = NULL;
        }
    }
    release_providers(window);
    if (window->hwnd != NULL) {
        DestroyWindow(window->hwnd);
        window->hwnd = NULL;
    }
    aura_queue_free(&window->queue);
    aura_ax_free(&window->ax);
    free(window->pixels);
    free(window->title);
    g_windows[handle - 1] = NULL;
    free(window);
    return 0;
}

extern "C" int64_t aura_window_poll(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    MSG message;
    if (window == NULL) {
        return -1;
    }
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return window->queue.count;
}

extern "C" int64_t aura_window_event_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->queue.count;
}

extern "C" int64_t aura_window_event_kind(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].kind;
}

extern "C" double aura_window_event_x(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].x;
}

extern "C" double aura_window_event_y(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].y;
}

extern "C" int64_t aura_window_event_key(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].key;
}

extern "C" int64_t aura_window_event_button(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].button;
}

extern "C" int64_t aura_window_events_clear(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_queue_clear(&window->queue);
    return 0;
}

extern "C" int64_t aura_window_event_text_len(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count || window->queue.events[index].text == NULL) {
        return 0;
    }
    return (int64_t)strlen(window->queue.events[index].text);
}

extern "C" int64_t aura_window_event_text_byte(int64_t handle, int64_t index, int64_t offset) {
    AuraWindow *window = window_get(handle, 1);
    const char *text = NULL;
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    text = window->queue.events[index].text;
    if (text == NULL || offset < 0 || offset >= (int64_t)strlen(text)) {
        return 0;
    }
    return (unsigned char)text[offset];
}

extern "C" int64_t aura_window_should_close(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return 1;
    }
    return window->should_close;
}

extern "C" double aura_window_width(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    RECT rect;
    if (window == NULL) {
        return -1;
    }
    GetClientRect(window->hwnd, &rect);
    return (double)(rect.right - rect.left);
}

extern "C" double aura_window_height(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    RECT rect;
    if (window == NULL) {
        return -1;
    }
    GetClientRect(window->hwnd, &rect);
    return (double)(rect.bottom - rect.top);
}

extern "C" double aura_window_scale(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->scale;
}

extern "C" double aura_frame_timestamp(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->frame_time;
}

extern "C" int64_t aura_window_wait_frame(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    DwmFlush();
    window->frame_time = qpc_seconds();
    return 0;
}

extern "C" int64_t aura_frame_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    RECT rect;
    int64_t width = 1;
    int64_t height = 1;
    if (window == NULL) {
        return -1;
    }
    GetClientRect(window->hwnd, &rect);
    width = (int64_t)((rect.right - rect.left) * window->scale);
    height = (int64_t)((rect.bottom - rect.top) * window->scale);
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    return aura_paint_begin(width, height, window->scale);
}

extern "C" int64_t aura_frame_end(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (aura_paint_end() < 0) {
        return -1;
    }
    copy_frame_pixels(window);
    InvalidateRect(window->hwnd, NULL, FALSE);
    UpdateWindow(window->hwnd);
    return 0;
}

extern "C" int64_t aura_cmd_clear(int64_t handle, int64_t argb) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_clear(argb);
}

extern "C" int64_t aura_cmd_rect(int64_t handle, double x, double y, double w, double h, int64_t argb) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_rect(x, y, w, h, argb);
}

extern "C" int64_t aura_cmd_text(int64_t handle, double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_text(x, y, text, size, weight, argb, family);
}

extern "C" int64_t aura_cmd_opacity(int64_t handle, double opacity) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_opacity(opacity);
}

extern "C" int64_t aura_cmd_opacity_pop(int64_t handle) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_opacity_pop();
}

extern "C" int64_t aura_cmd_clip(int64_t handle, double x, double y, double w, double h) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_clip(x, y, w, h);
}

extern "C" int64_t aura_cmd_clip_pop(int64_t handle) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_clip_pop();
}

extern "C" int64_t aura_cmd_image(int64_t handle, int64_t image, double x, double y, double w, double h) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_image(image, x, y, w, h);
}

extern "C" int64_t aura_sample(int64_t handle, double x, double y) {
    AuraWindow *window = window_get(handle, 1);
    int px = 0;
    int py = 0;
    uint8_t *pixel = NULL;
    int a = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    if (window == NULL || window->pixels == NULL) {
        return -1;
    }
    px = (int)(x * window->scale);
    py = (int)(y * window->scale);
    if (px < 0 || py < 0 || px >= window->pixel_w || py >= window->pixel_h) {
        return -1;
    }
    pixel = window->pixels + (size_t)py * (size_t)window->pixel_stride + (size_t)px * 4;
    r = pixel[2];
    g = pixel[1];
    b = pixel[0];
    a = pixel[3];
    if (a > 0 && a < 255) {
        r = r * 255 / a;
        g = g * 255 / a;
        b = b * 255 / a;
    }
    return ((int64_t)a << 24) | ((int64_t)r << 16) | ((int64_t)g << 8) | (int64_t)b;
}

extern "C" int64_t aura_post_mouse(int64_t handle, int64_t kind, double x, double y, int64_t button) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (button < 1) {
        button = 1;
    }
    aura_queue_push(&window->queue, (int)kind, x, y, 0, button);
    return 0;
}

extern "C" int64_t aura_post_text(int64_t handle, const char *utf8) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_queue_push_text(&window->queue, AURA_IME_INSERT, utf8);
    return 0;
}

class AuraDropSource : public IDropSource {
public:
    AuraDropSource() : refs(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
        if (object == NULL) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *object = static_cast<IDropSource *>(this);
            AddRef();
            return S_OK;
        }
        *object = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG left = InterlockedDecrement(&refs);
        if (left == 0) {
            delete this;
        }
        return (ULONG)left;
    }

    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) {
            return DRAGDROP_S_CANCEL;
        }
        if ((keys & MK_LBUTTON) == 0) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD effect) override {
        (void)effect;
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    LONG refs;
};

class AuraFileData : public IDataObject {
public:
    explicit AuraFileData(HGLOBAL held) : refs(1), memory(held) {}

    ~AuraFileData() {
        if (memory != NULL) {
            GlobalFree(memory);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
        if (object == NULL) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *object = static_cast<IDataObject *>(this);
            AddRef();
            return S_OK;
        }
        *object = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&refs);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        LONG left = InterlockedDecrement(&refs);
        if (left == 0) {
            delete this;
        }
        return (ULONG)left;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *format, STGMEDIUM *medium) override {
        SIZE_T size = 0;
        void *from = NULL;
        void *to = NULL;
        HGLOBAL copy = NULL;
        if (format == NULL || medium == NULL || memory == NULL) {
            return E_INVALIDARG;
        }
        if (format->cfFormat != CF_HDROP || (format->tymed & TYMED_HGLOBAL) == 0) {
            return DV_E_FORMATETC;
        }
        size = GlobalSize(memory);
        copy = GlobalAlloc(GMEM_MOVEABLE, size);
        if (copy == NULL) {
            return E_OUTOFMEMORY;
        }
        from = GlobalLock(memory);
        to = GlobalLock(copy);
        if (from == NULL || to == NULL) {
            if (from != NULL) {
                GlobalUnlock(memory);
            }
            if (to != NULL) {
                GlobalUnlock(copy);
            }
            GlobalFree(copy);
            return E_OUTOFMEMORY;
        }
        memcpy(to, from, size);
        GlobalUnlock(memory);
        GlobalUnlock(copy);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = copy;
        medium->pUnkForRelease = NULL;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *format, STGMEDIUM *medium) override {
        (void)format;
        (void)medium;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *format) override {
        if (format != NULL && format->cfFormat == CF_HDROP && (format->tymed & TYMED_HGLOBAL) != 0) {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *in, FORMATETC *out) override {
        (void)in;
        (void)out;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *format, STGMEDIUM *medium, BOOL release) override {
        (void)format;
        (void)medium;
        (void)release;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **enumerator) override {
        FORMATETC format;
        if (direction != DATADIR_GET || enumerator == NULL) {
            return E_NOTIMPL;
        }
        format.cfFormat = CF_HDROP;
        format.ptd = NULL;
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex = -1;
        format.tymed = TYMED_HGLOBAL;
        return SHCreateStdEnumFmtEtc(1, &format, enumerator);
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *format, DWORD advf, IAdviseSink *sink, DWORD *connection) override {
        (void)format;
        (void)advf;
        (void)sink;
        (void)connection;
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD connection) override {
        (void)connection;
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **enumerator) override {
        (void)enumerator;
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    LONG refs;
    HGLOBAL memory;
};

static HGLOBAL hdrop_for_path(const char *path) {
    wchar_t wide[MAX_PATH * 4];
    int chars = MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH * 4);
    SIZE_T bytes = 0;
    HGLOBAL memory = NULL;
    DROPFILES *drop = NULL;
    if (chars <= 0) {
        return NULL;
    }
    bytes = sizeof(DROPFILES) + (size_t)chars * sizeof(wchar_t) + sizeof(wchar_t);
    memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (memory == NULL) {
        return NULL;
    }
    drop = (DROPFILES *)GlobalLock(memory);
    if (drop == NULL) {
        GlobalFree(memory);
        return NULL;
    }
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    memcpy((char *)drop + sizeof(DROPFILES), wide, (size_t)chars * sizeof(wchar_t));
    GlobalUnlock(memory);
    return memory;
}

extern "C" int64_t aura_drag_file(int64_t handle, const char *path) {
    AuraWindow *window = window_get(handle, 1);
    HGLOBAL memory = NULL;
    AuraFileData *data = NULL;
    AuraDropSource *source = NULL;
    DWORD effect = 0;
    HRESULT result = E_FAIL;
    if (window == NULL || path == NULL || path[0] == 0 || window->shown == 0) {
        return -1;
    }
    OleInitialize(NULL);
    memory = hdrop_for_path(path);
    if (memory == NULL) {
        return -1;
    }
    data = new AuraFileData(memory);
    source = new AuraDropSource();
    result = DoDragDrop(data, source, DROPEFFECT_COPY, &effect);
    data->Release();
    source->Release();
    if (FAILED(result)) {
        return -1;
    }
    return 0;
}

extern "C" int64_t aura_post_drop(int64_t handle, double x, double y, const char *path) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_queue_push_text(&window->queue, AURA_FILE_DROP, path);
    if (window->queue.count > 0) {
        window->queue.events[window->queue.count - 1].x = x;
        window->queue.events[window->queue.count - 1].y = y;
    }
    return 0;
}

extern "C" int64_t aura_post_ax(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    int64_t press = 0;
    if (window == NULL) {
        return -1;
    }
    press = aura_ax_press_node(&window->ax, index);
    if (press < 0) {
        return -1;
    }
    aura_queue_push(&window->queue, AURA_AX_ACTION, 0, 0, press, 0);
    return 0;
}

extern "C" int64_t aura_text_focus(int64_t handle, int64_t enabled) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    window->text_focus = enabled != 0;
    if (enabled != 0) {
        SetFocus(window->hwnd);
    }
    return 0;
}

extern "C" int64_t aura_text_selection(int64_t handle, int64_t start, int64_t length) {
    (void)start;
    (void)length;
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return 0;
}

extern "C" int64_t aura_text_contents(int64_t handle, const char *utf8) {
    (void)utf8;
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return 0;
}

extern "C" int64_t aura_text_caret(int64_t handle, double x, double y, double height) {
    (void)x;
    (void)y;
    (void)height;
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return 0;
}

extern "C" int64_t aura_clipboard_set(const char *text) {
    int chars = 0;
    HGLOBAL memory = NULL;
    wchar_t *wide = NULL;
    if (!OpenClipboard(NULL)) {
        return -1;
    }
    EmptyClipboard();
    chars = MultiByteToWideChar(CP_UTF8, 0, text != NULL ? text : "", -1, NULL, 0);
    memory = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)chars * sizeof(wchar_t));
    if (memory == NULL) {
        CloseClipboard();
        return -1;
    }
    wide = (wchar_t *)GlobalLock(memory);
    if (wide == NULL) {
        GlobalFree(memory);
        CloseClipboard();
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, text != NULL ? text : "", -1, wide, chars);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == NULL) {
        GlobalFree(memory);
        CloseClipboard();
        return -1;
    }
    CloseClipboard();
    return 0;
}

extern "C" int64_t aura_clipboard_len(void) {
    HANDLE memory = NULL;
    const wchar_t *wide = NULL;
    if (!OpenClipboard(NULL)) {
        free(g_clipboard);
        g_clipboard = _strdup("");
        return 0;
    }
    memory = GetClipboardData(CF_UNICODETEXT);
    wide = memory != NULL ? (const wchar_t *)GlobalLock(memory) : NULL;
    copy_utf8(wide, &g_clipboard);
    if (wide != NULL) {
        GlobalUnlock(memory);
    }
    CloseClipboard();
    if (g_clipboard == NULL) {
        return 0;
    }
    return (int64_t)strlen(g_clipboard);
}

extern "C" int64_t aura_clipboard_byte(int64_t offset) {
    if (g_clipboard == NULL || offset < 0 || offset >= (int64_t)strlen(g_clipboard)) {
        return 0;
    }
    return (unsigned char)g_clipboard[offset];
}

extern "C" int64_t aura_set_cursor(int64_t handle, int64_t kind) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    window->cursor_kind = (int)kind;
    SetCursor(LoadCursorW(NULL, kind == 1 ? IDC_IBEAM : IDC_ARROW));
    return 0;
}

extern "C" int64_t aura_window_title_len(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (window->title == NULL) {
        return 0;
    }
    return (int64_t)strlen(window->title);
}

extern "C" int64_t aura_window_title_byte(int64_t handle, int64_t offset) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || window->title == NULL || offset < 0 || offset >= (int64_t)strlen(window->title)) {
        return 0;
    }
    return (unsigned char)window->title[offset];
}

extern "C" int64_t aura_cursor(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->cursor_kind;
}

extern "C" int64_t aura_ax_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_begin_nodes(&window->ax);
}

extern "C" int64_t aura_ax_add(int64_t handle, const char *role, const char *label, const char *value, int64_t checked, double x, double y, double w, double h, int64_t press) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_add_node(&window->ax, role, label, value, checked, x, y, w, h, press);
}

extern "C" int64_t aura_ax_commit(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (aura_ax_commit_nodes(&window->ax) < 0) {
        return -1;
    }
    publish_providers(window);
    return 0;
}

extern "C" int64_t aura_ax_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_count_nodes(&window->ax);
}

extern "C" int64_t aura_ax_text_len(int64_t handle, int64_t index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_text_len_node(&window->ax, index, field);
}

extern "C" int64_t aura_ax_text_byte(int64_t offset) {
    int i = 0;
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        if (g_windows[i] != NULL && g_windows[i]->ax.query != NULL) {
            return aura_ax_text_byte_node(&g_windows[i]->ax, offset);
        }
    }
    return 0;
}

extern "C" int64_t aura_ax_checked(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_checked_node(&window->ax, index);
}

extern "C" int64_t aura_ax_press(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_press_node(&window->ax, index);
}

extern "C" double aura_ax_x(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 0);
}

extern "C" double aura_ax_y(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 1);
}

extern "C" double aura_ax_w(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 2);
}

extern "C" double aura_ax_h(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 3);
}

extern "C" int64_t aura_plugin_place(int64_t handle, int64_t plugin, double x, double y, double w, double h) {
    AuraWindow *window = window_get(handle, 1);
    int slot = -1;
    int i = 0;
    if (window == NULL || aura_plugin_check(plugin) < 0) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        if (window->plugin_ids[i] == plugin) {
            slot = i;
            break;
        }
        if (slot < 0 && window->plugin_ids[i] == 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        return -1;
    }
    if (window->plugin_hwnd[slot] == NULL) {
        window->plugin_hwnd[slot] = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, (int)x, (int)y, (int)w, (int)h, window->hwnd, NULL, GetModuleHandleW(NULL), NULL);
        window->plugin_ids[slot] = plugin;
    } else {
        MoveWindow(window->plugin_hwnd[slot], (int)x, (int)y, (int)w, (int)h, TRUE);
    }
    return aura_plugin_frame(plugin, (int64_t)w, (int64_t)h);
}
