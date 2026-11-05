#include <android/bitmap.h>
#include <android/log.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "android_surface.h"
#include "app_runtime.h"
#include "ax_platform.h"
#include "build_identity.h"
#include "events.h"
#include "shell.h"

static JavaVM *g_vm = NULL;
static jobject g_activity = NULL;
static jmethodID g_invalidate = NULL;
static jmethodID g_focus = NULL;
static jmethodID g_text_state = NULL;
static jmethodID g_text_geometry = NULL;
static jmethodID g_text_configuration = NULL;
static jmethodID g_text_boundaries = NULL;
static jmethodID g_back_result = NULL;
static jmethodID g_open_overlay = NULL;
static jmethodID g_close_overlay = NULL;
static pthread_mutex_t g_activity_mu = PTHREAD_MUTEX_INITIALIZER;
static char *g_clip = NULL;
static size_t g_clip_length = 0;
#define AURA_ANDROID_SLOTS 4
static AuraAndroidSurfaceMachine g_android_surface[AURA_ANDROID_SLOTS];

static int android_slot(int slot) {
    if (slot < 0 || slot >= AURA_ANDROID_SLOTS) {
        return -1;
    }
    return slot;
}

static void post_android_surface(int slot, const AuraAndroidSurfacePlan *plan, jlong timestamp_ns) {
    int index = 0;
    if (android_slot(slot) < 0) {
        return;
    }
    for (index = 0; index < plan->count; index += 1) {
        const AuraAndroidSurfaceEvent *event = &plan->events[index];
        aura_shell_surface(slot, event->kind, event->generation, event->width, event->height, AURA_SURFACE_FORMAT_RGBA8888, event->generation, (uint64_t)timestamp_ns);
    }
}

static int jni_failed(JNIEnv *env) {
    if (env != NULL && (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return 1;
    }
    return 0;
}

static void runtime_log(int is_error, const char *message) {
    __android_log_print(is_error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "aura", "%s", message != NULL ? message : "runtime");
}

static JNIEnv *attach_env(int *attached) {
    JNIEnv *env = NULL;
    if (attached != NULL) {
        *attached = 0;
    }
    if (g_vm == NULL) {
        return NULL;
    }
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0) {
        return NULL;
    }
    if (attached != NULL) {
        *attached = 1;
    }
    return env;
}

static void detach_env(int attached) {
    if (attached && g_vm != NULL) {
        (*g_vm)->DetachCurrentThread(g_vm);
    }
}

static jstring java_string_from_utf8(JNIEnv *env, const char *text) {
    const unsigned char *bytes = (const unsigned char *)(text != NULL ? text : "");
    size_t length = strlen((const char *)bytes);
    jchar *units = (jchar *)malloc((length + 1) * sizeof(jchar));
    size_t i = 0;
    jsize count = 0;
    jstring result = NULL;
    if (units == NULL) {
        return NULL;
    }
    while (i < length) {
        uint32_t code = 0xfffd;
        size_t used = 1;
        unsigned char b0 = bytes[i];
        if (b0 < 0x80) {
            code = b0;
        } else if (b0 >= 0xc2 && b0 <= 0xdf && i + 1 < length && (bytes[i + 1] & 0xc0) == 0x80) {
            code = ((uint32_t)(b0 & 0x1f) << 6) | (uint32_t)(bytes[i + 1] & 0x3f);
            used = 2;
        } else if (b0 >= 0xe0 && b0 <= 0xef && i + 2 < length && (bytes[i + 1] & 0xc0) == 0x80 && (bytes[i + 2] & 0xc0) == 0x80) {
            uint32_t candidate = ((uint32_t)(b0 & 0x0f) << 12) | ((uint32_t)(bytes[i + 1] & 0x3f) << 6) | (uint32_t)(bytes[i + 2] & 0x3f);
            if (candidate >= 0x800 && !(candidate >= 0xd800 && candidate <= 0xdfff)) {
                code = candidate;
                used = 3;
            }
        } else if (b0 >= 0xf0 && b0 <= 0xf4 && i + 3 < length && (bytes[i + 1] & 0xc0) == 0x80 && (bytes[i + 2] & 0xc0) == 0x80 && (bytes[i + 3] & 0xc0) == 0x80) {
            uint32_t candidate = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(bytes[i + 1] & 0x3f) << 12) | ((uint32_t)(bytes[i + 2] & 0x3f) << 6) | (uint32_t)(bytes[i + 3] & 0x3f);
            if (candidate >= 0x10000 && candidate <= 0x10ffff) {
                code = candidate;
                used = 4;
            }
        }
        if (code <= 0xffff) {
            units[count++] = (jchar)code;
        } else {
            code -= 0x10000;
            units[count++] = (jchar)(0xd800 + (code >> 10));
            units[count++] = (jchar)(0xdc00 + (code & 0x3ff));
        }
        i += used;
    }
    result = (*env)->NewString(env, units, count);
    free(units);
    return result;
}

static char *utf8_from_java_string(JNIEnv *env, jstring value, size_t *out_length) {
    const jchar *units = NULL;
    jsize length = 0;
    char *out = NULL;
    size_t cursor = 0;
    jsize i = 0;
    if (out_length != NULL) {
        *out_length = 0;
    }
    if (value == NULL) {
        return strdup("");
    }
    length = (*env)->GetStringLength(env, value);
    units = (*env)->GetStringChars(env, value, NULL);
    if (units == NULL) {
        return NULL;
    }
    out = (char *)malloc((size_t)length * 4u + 1u);
    if (out == NULL) {
        (*env)->ReleaseStringChars(env, value, units);
        return NULL;
    }
    while (i < length) {
        uint32_t code = units[i++];
        if (code >= 0xd800 && code <= 0xdbff && i < length && units[i] >= 0xdc00 && units[i] <= 0xdfff) {
            code = 0x10000 + ((code - 0xd800) << 10) + (units[i++] - 0xdc00);
        } else if (code >= 0xd800 && code <= 0xdfff) {
            code = 0xfffd;
        }
        if (code < 0x80) {
            out[cursor++] = (char)code;
        } else if (code < 0x800) {
            out[cursor++] = (char)(0xc0 | (code >> 6));
            out[cursor++] = (char)(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out[cursor++] = (char)(0xe0 | (code >> 12));
            out[cursor++] = (char)(0x80 | ((code >> 6) & 0x3f));
            out[cursor++] = (char)(0x80 | (code & 0x3f));
        } else {
            out[cursor++] = (char)(0xf0 | (code >> 18));
            out[cursor++] = (char)(0x80 | ((code >> 12) & 0x3f));
            out[cursor++] = (char)(0x80 | ((code >> 6) & 0x3f));
            out[cursor++] = (char)(0x80 | (code & 0x3f));
        }
    }
    out[cursor] = 0;
    if (out_length != NULL) {
        *out_length = cursor;
    }
    (*env)->ReleaseStringChars(env, value, units);
    return out;
}

static void on_ui(void (*fn)(JNIEnv *env, void *ctx), void *ctx) {
    int attached = 0;
    JNIEnv *env = attach_env(&attached);
    if (env == NULL) {
        return;
    }
    fn(env, ctx);
    if (jni_failed(env)) {
        __android_log_print(ANDROID_LOG_ERROR, "aura", "jni exception cleared");
    }
    detach_env(attached);
}

static jobject activity_local(JNIEnv *env) {
    jobject local = NULL;
    pthread_mutex_lock(&g_activity_mu);
    if (g_activity != NULL) {
        local = (*env)->NewLocalRef(env, g_activity);
    }
    pthread_mutex_unlock(&g_activity_mu);
    return local;
}

static void overlay_call(JNIEnv *env, void *ctx, jmethodID method) {
    jobject activity = activity_local(env);
    int slot = (int)(intptr_t)ctx;
    if (activity != NULL && method != NULL && android_slot(slot) > 0) {
        (*env)->CallVoidMethod(env, activity, method, (jint)slot);
    }
    if (activity != NULL) {
        (*env)->DeleteLocalRef(env, activity);
    }
}

static void open_overlay_call(JNIEnv *env, void *ctx) {
    jmethodID method = NULL;
    pthread_mutex_lock(&g_activity_mu);
    method = g_open_overlay;
    pthread_mutex_unlock(&g_activity_mu);
    overlay_call(env, ctx, method);
}

static void close_overlay_call(JNIEnv *env, void *ctx) {
    jmethodID method = NULL;
    pthread_mutex_lock(&g_activity_mu);
    method = g_close_overlay;
    pthread_mutex_unlock(&g_activity_mu);
    overlay_call(env, ctx, method);
}

void aura_host_open(int slot, double width, double height, const char *title, int visible) {
    (void)width;
    (void)height;
    (void)title;
    (void)visible;
    if (slot <= 0) {
        return;
    }
    on_ui(open_overlay_call, (void *)(intptr_t)slot);
}

void aura_host_close(int slot) {
    if (slot <= 0) {
        return;
    }
    on_ui(close_overlay_call, (void *)(intptr_t)slot);
}

static void present_call(JNIEnv *env, void *ctx) {
    jobject activity = activity_local(env);
    jmethodID invalidate = NULL;
    int slot = (int)(intptr_t)ctx;
    pthread_mutex_lock(&g_activity_mu);
    invalidate = g_invalidate;
    pthread_mutex_unlock(&g_activity_mu);
    if (activity != NULL && invalidate != NULL && android_slot(slot) >= 0) {
        (*env)->CallVoidMethod(env, activity, invalidate, (jint)slot);
    }
    if (activity != NULL) {
        (*env)->DeleteLocalRef(env, activity);
    }
}

void aura_host_present(int slot) {
    on_ui(present_call, (void *)(intptr_t)slot);
}

void aura_host_accessibility(int slot) {
    __android_log_print(ANDROID_LOG_INFO, "aura", "aura-ax %d", aura_shell_ax_count(slot));
    on_ui(present_call, (void *)(intptr_t)slot);
}

void aura_host_accessibility_update(int slot, int change) {
    (void)change;
    aura_host_accessibility(slot);
}

typedef struct AuraAndroidFocus {
    int slot;
    int enabled;
} AuraAndroidFocus;

static void focus_call(JNIEnv *env, void *ctx) {
    AuraAndroidFocus *request = (AuraAndroidFocus *)ctx;
    jobject activity = activity_local(env);
    jmethodID focus = NULL;
    pthread_mutex_lock(&g_activity_mu);
    focus = g_focus;
    pthread_mutex_unlock(&g_activity_mu);
    if (activity != NULL && focus != NULL && request != NULL) {
        (*env)->CallVoidMethod(env, activity, focus, (jint)request->slot, request->enabled ? JNI_TRUE : JNI_FALSE);
    }
    if (activity != NULL) {
        (*env)->DeleteLocalRef(env, activity);
    }
}

void aura_host_text_focus(int slot, int enabled) {
    AuraAndroidFocus request;
    request.slot = slot;
    request.enabled = enabled;
    on_ui(focus_call, &request);
}

typedef struct AuraAndroidTextState {
    int slot;
    const char *text;
    int64_t generation;
    int64_t revision;
    int64_t selection_start;
    int64_t selection_end;
    int64_t composing_start;
    int64_t composing_end;
} AuraAndroidTextState;

static void text_state_call(JNIEnv *env, void *ctx) {
    AuraAndroidTextState *state = (AuraAndroidTextState *)ctx;
    jobject activity = activity_local(env);
    jmethodID method = NULL;
    jstring text = NULL;
    pthread_mutex_lock(&g_activity_mu);
    method = g_text_state;
    pthread_mutex_unlock(&g_activity_mu);
    if (activity != NULL && method != NULL && state != NULL) {
        text = java_string_from_utf8(env, state->text);
        if (text != NULL) {
            (*env)->CallVoidMethod(env, activity, method, (jint)state->slot, text, (jlong)state->generation, (jlong)state->revision, (jint)state->selection_start, (jint)state->selection_end, (jint)state->composing_start, (jint)state->composing_end);
            (*env)->DeleteLocalRef(env, text);
        }
    }
    if (activity != NULL) {
        (*env)->DeleteLocalRef(env, activity);
    }
}

void aura_host_text_state(int slot, const char *text, int64_t generation, int64_t revision, int64_t selection_start, int64_t selection_end, int64_t composing_start, int64_t composing_end) {
    AuraAndroidTextState state;
    state.slot = slot;
    state.text = text;
    state.generation = generation;
    state.revision = revision;
    state.selection_start = selection_start;
    state.selection_end = selection_end;
    state.composing_start = composing_start;
    state.composing_end = composing_end;
    on_ui(text_state_call, &state);
}

typedef struct AuraAndroidTextGeometry {
    int slot;
    double caret_x;
    double caret_y;
    double caret_width;
    double caret_height;
    double editable_x;
    double editable_y;
    double editable_width;
    double editable_height;
} AuraAndroidTextGeometry;

static void text_geometry_call(JNIEnv *env, void *ctx) {
    AuraAndroidTextGeometry *geometry = (AuraAndroidTextGeometry *)ctx;
    jobject activity = activity_local(env);
    jmethodID method = NULL;
    pthread_mutex_lock(&g_activity_mu);
    method = g_text_geometry;
    pthread_mutex_unlock(&g_activity_mu);
    if (activity != NULL && method != NULL && geometry != NULL) {
        (*env)->CallVoidMethod(env, activity, method, (jint)geometry->slot, (jfloat)geometry->caret_x, (jfloat)geometry->caret_y, (jfloat)geometry->caret_width, (jfloat)geometry->caret_height, (jfloat)geometry->editable_x, (jfloat)geometry->editable_y, (jfloat)geometry->editable_width, (jfloat)geometry->editable_height);
    }
    if (activity != NULL) {
        (*env)->DeleteLocalRef(env, activity);
    }
}

void aura_host_text_geometry(int slot, double caret_x, double caret_y, double caret_width, double caret_height, double editable_x, double editable_y, double editable_width, double editable_height) {
    AuraAndroidTextGeometry geometry;
    geometry.slot = slot;
    geometry.caret_x = caret_x;
    geometry.caret_y = caret_y;
    geometry.caret_width = caret_width;
    geometry.caret_height = caret_height;
    geometry.editable_x = editable_x;
    geometry.editable_y = editable_y;
    geometry.editable_width = editable_width;
    geometry.editable_height = editable_height;
    on_ui(text_geometry_call, &geometry);
}

typedef struct AuraBoundaryUpload {
    int slot;
    int64_t revision;
    int count;
    jint *offsets;
    jfloat *xs;
    jfloat *ys;
    jfloat *heights;
} AuraBoundaryUpload;

static void text_boundary_call(JNIEnv *env, void *ctx) {
    AuraBoundaryUpload *upload = (AuraBoundaryUpload *)ctx;
    jobject activity = activity_local(env);
    jmethodID method = NULL;
    jintArray offsets = NULL;
    jfloatArray xs = NULL;
    jfloatArray ys = NULL;
    jfloatArray heights = NULL;
    pthread_mutex_lock(&g_activity_mu);
    method = g_text_boundaries;
    pthread_mutex_unlock(&g_activity_mu);
    if (activity == NULL || method == NULL || upload == NULL || upload->count < 0) {
        if (activity != NULL) {
            (*env)->DeleteLocalRef(env, activity);
        }
        return;
    }
    offsets = (*env)->NewIntArray(env, upload->count);
    xs = (*env)->NewFloatArray(env, upload->count);
    ys = (*env)->NewFloatArray(env, upload->count);
    heights = (*env)->NewFloatArray(env, upload->count);
    if (offsets != NULL && xs != NULL && ys != NULL && heights != NULL) {
        if (upload->count > 0) {
            (*env)->SetIntArrayRegion(env, offsets, 0, upload->count, upload->offsets);
            (*env)->SetFloatArrayRegion(env, xs, 0, upload->count, upload->xs);
            (*env)->SetFloatArrayRegion(env, ys, 0, upload->count, upload->ys);
            (*env)->SetFloatArrayRegion(env, heights, 0, upload->count, upload->heights);
        }
        (*env)->CallVoidMethod(env, activity, method, (jint)upload->slot, (jlong)upload->revision, offsets, xs, ys, heights);
    }
    if (offsets != NULL) (*env)->DeleteLocalRef(env, offsets);
    if (xs != NULL) (*env)->DeleteLocalRef(env, xs);
    if (ys != NULL) (*env)->DeleteLocalRef(env, ys);
    if (heights != NULL) (*env)->DeleteLocalRef(env, heights);
    (*env)->DeleteLocalRef(env, activity);
}

void aura_host_text_geometry_snapshot(int slot, int64_t target, int64_t revision, int count) {
    AuraBoundaryUpload upload;
    int i = 0;
    (void)target;
    memset(&upload, 0, sizeof(upload));
    upload.slot = slot;
    if (count < 0) {
        return;
    }
    upload.revision = revision;
    upload.count = count;
    if (count > 0) {
        upload.offsets = (jint *)calloc((size_t)count, sizeof(jint));
        upload.xs = (jfloat *)calloc((size_t)count, sizeof(jfloat));
        upload.ys = (jfloat *)calloc((size_t)count, sizeof(jfloat));
        upload.heights = (jfloat *)calloc((size_t)count, sizeof(jfloat));
        if (upload.offsets == NULL || upload.xs == NULL || upload.ys == NULL || upload.heights == NULL) {
            free(upload.offsets);
            free(upload.xs);
            free(upload.ys);
            free(upload.heights);
            return;
        }
        while (i < count) {
            int64_t offset = 0;
            double x = 0.0;
            double y = 0.0;
            double height = 0.0;
            if (aura_shell_text_geometry_boundary(slot, i, &offset, &x, &y, &height) != 0) {
                free(upload.offsets);
                free(upload.xs);
                free(upload.ys);
                free(upload.heights);
                return;
            }
            upload.offsets[i] = (jint)offset;
            upload.xs[i] = (jfloat)x;
            upload.ys[i] = (jfloat)y;
            upload.heights[i] = (jfloat)height;
            i += 1;
        }
    }
    on_ui(text_boundary_call, &upload);
    free(upload.offsets);
    free(upload.xs);
    free(upload.ys);
    free(upload.heights);
}

typedef struct AuraAndroidTextConfiguration {
    int slot;
    int input_type;
    int action;
    int multiline;
    int secure;
    int autocorrect;
    int capitalization;
    const char *autofill_hints;
} AuraAndroidTextConfiguration;

static void text_configuration_call(JNIEnv *env, void *ctx) {
    AuraAndroidTextConfiguration *config = (AuraAndroidTextConfiguration *)ctx;
    jobject activity = activity_local(env);
    jmethodID method = NULL;
    jstring hints = NULL;
    pthread_mutex_lock(&g_activity_mu);
    method = g_text_configuration;
    pthread_mutex_unlock(&g_activity_mu);
    if (activity != NULL && method != NULL && config != NULL) {
        hints = java_string_from_utf8(env, config->autofill_hints);
        if (hints != NULL) {
            (*env)->CallVoidMethod(env, activity, method, (jint)config->slot, (jint)config->input_type, (jint)config->action, config->multiline ? JNI_TRUE : JNI_FALSE, config->secure ? JNI_TRUE : JNI_FALSE, config->autocorrect ? JNI_TRUE : JNI_FALSE, (jint)config->capitalization, hints);
            (*env)->DeleteLocalRef(env, hints);
        }
    }
    if (activity != NULL) {
        (*env)->DeleteLocalRef(env, activity);
    }
}

void aura_host_text_configuration(int slot, int input_type, int action, int multiline, int secure, int autocorrect, int capitalization, const char *autofill_hints) {
    AuraAndroidTextConfiguration config;
    config.slot = slot;
    config.input_type = input_type;
    config.action = action;
    config.multiline = multiline;
    config.secure = secure;
    config.autocorrect = autocorrect;
    config.capitalization = capitalization;
    config.autofill_hints = autofill_hints;
    on_ui(text_configuration_call, &config);
}

static void back_result_call(JNIEnv *env, void *ctx) {
    jobject activity = activity_local(env);
    jmethodID result = NULL;
    int handled = ctx != NULL;
    pthread_mutex_lock(&g_activity_mu);
    result = g_back_result;
    pthread_mutex_unlock(&g_activity_mu);
    if (activity != NULL && result != NULL) {
        (*env)->CallVoidMethod(env, activity, result, handled ? JNI_TRUE : JNI_FALSE);
    }
    if (activity != NULL) {
        (*env)->DeleteLocalRef(env, activity);
    }
}

void aura_host_system_back_result(int slot, int handled) {
    (void)slot;
    on_ui(back_result_call, handled ? (void *)1 : NULL);
}

int aura_host_capabilities(int slot) {
    (void)slot;
    return 0;
}

int aura_host_clipboard_set(const char *text) {
    int attached = 0;
    JNIEnv *env = attach_env(&attached);
    jobject activity = NULL;
    jclass cls = NULL;
    jmethodID set = NULL;
    jstring value = NULL;
    int ok = -1;
    if (env == NULL) {
        return -1;
    }
    activity = activity_local(env);
    if (activity == NULL) {
        detach_env(attached);
        return -1;
    }
    cls = (*env)->GetObjectClass(env, activity);
    set = (*env)->GetMethodID(env, cls, "clipboardSet", "(Ljava/lang/String;)V");
    if (jni_failed(env) || set == NULL) {
        if (cls != NULL) (*env)->DeleteLocalRef(env, cls);
        (*env)->DeleteLocalRef(env, activity);
        detach_env(attached);
        return -1;
    }
    value = java_string_from_utf8(env, text != NULL ? text : "");
    if (value != NULL) {
        (*env)->CallVoidMethod(env, activity, set, value);
        ok = jni_failed(env) ? -1 : 0;
        (*env)->DeleteLocalRef(env, value);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    detach_env(attached);
    return ok;
}

const char *aura_host_clipboard_get(void) {
    int attached = 0;
    JNIEnv *env = attach_env(&attached);
    jobject activity = NULL;
    jclass cls = NULL;
    jmethodID get = NULL;
    jstring value = NULL;
    char *copy = NULL;
    size_t length = 0;
    free(g_clip);
    g_clip = NULL;
    g_clip_length = 0;
    if (env == NULL) {
        return NULL;
    }
    activity = activity_local(env);
    if (activity == NULL) {
        detach_env(attached);
        return NULL;
    }
    cls = (*env)->GetObjectClass(env, activity);
    get = (*env)->GetMethodID(env, cls, "clipboardGet", "()Ljava/lang/String;");
    if (!jni_failed(env) && get != NULL) {
        value = (jstring)(*env)->CallObjectMethod(env, activity, get);
        if (!jni_failed(env) && value != NULL) {
            copy = utf8_from_java_string(env, value, &length);
        }
    }
    if (value != NULL) {
        (*env)->DeleteLocalRef(env, value);
    }
    if (cls != NULL) {
        (*env)->DeleteLocalRef(env, cls);
    }
    (*env)->DeleteLocalRef(env, activity);
    detach_env(attached);
    g_clip = copy != NULL ? copy : strdup("");
    g_clip_length = copy != NULL ? length : 0;
    return g_clip;
}

size_t aura_host_clipboard_length(void) {
    return g_clip_length;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
    (void)vm;
    (void)reserved;
    aura_mobile_runtime_request_stop();
    free(g_clip);
    g_clip = NULL;
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeBind(JNIEnv *env, jobject activity) {
    jclass cls = (*env)->GetObjectClass(env, activity);
    pthread_mutex_lock(&g_activity_mu);
    if (g_activity != NULL) {
        (*env)->DeleteGlobalRef(env, g_activity);
    }
    g_activity = (*env)->NewGlobalRef(env, activity);
    g_invalidate = (*env)->GetMethodID(env, cls, "requestDraw", "(I)V");
    g_focus = (*env)->GetMethodID(env, cls, "setTextFocus", "(IZ)V");
    g_text_state = (*env)->GetMethodID(env, cls, "setTextState", "(ILjava/lang/String;JJIIII)V");
    g_text_geometry = (*env)->GetMethodID(env, cls, "setTextGeometry", "(IFFFFFFFF)V");
    g_text_configuration = (*env)->GetMethodID(env, cls, "setTextConfiguration", "(IIIZZZILjava/lang/String;)V");
    g_text_boundaries = (*env)->GetMethodID(env, cls, "setTextBoundaries", "(IJ[I[F[F[F)V");
    g_back_result = (*env)->GetMethodID(env, cls, "resolveSystemBack", "(Z)V");
    g_open_overlay = (*env)->GetMethodID(env, cls, "openOverlay", "(I)V");
    g_close_overlay = (*env)->GetMethodID(env, cls, "closeOverlay", "(I)V");
    if (jni_failed(env)) {
        __android_log_print(ANDROID_LOG_ERROR, "aura", "jni method lookup failed");
    }
    pthread_mutex_unlock(&g_activity_mu);
    (*env)->DeleteLocalRef(env, cls);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativePointer(JNIEnv *env, jobject activity, jint slot, jint phase, jfloat x, jfloat y, jfloat scale, jlong timestamp_ns, jint device_id, jint pointer_id, jint pointer_kind, jint buttons, jfloat pressure, jfloat tilt_x, jfloat tilt_y) {
    AuraMobileEvent event;
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return;
    }
    if (!aura_mobile_pointer_v1(&event, phase, x, y, scale, 0, (uint64_t)timestamp_ns, device_id, pointer_id, (uint32_t)pointer_kind, (uint32_t)buttons, 0, pressure, tilt_x, tilt_y)) {
        return;
    }
    aura_shell_post_event(slot, &event);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeKey(JNIEnv *env, jobject activity, jint slot, jboolean down, jint logical_key, jint modifiers, jlong timestamp_ns, jint device_id) {
    AuraPlatformEvent event;
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return;
    }
    if (!aura_mobile_key_v1(&event, down ? 1 : 0, logical_key, (uint32_t)modifiers, 0, (uint64_t)timestamp_ns, device_id)) {
        return;
    }
    aura_shell_post_event(slot, &event);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeResize(JNIEnv *env, jobject activity, jint slot, jfloat width, jfloat height, jfloat scale, jfloat safe_left, jfloat safe_top, jfloat safe_right, jfloat safe_bottom, jfloat keyboard, jfloat gesture_left, jfloat gesture_top, jfloat gesture_right, jfloat gesture_bottom, jfloat refresh_rate, jfloat font_scale, jint rotation, jfloat keyboard_duration, jint keyboard_curve, jfloatArray cutouts, jlong timestamp_ns) {
    AuraMetricsPayload metrics;
    (void)activity;
    if (android_slot(slot) < 0 || width < 1.0f || height < 1.0f || !(scale > 0.0f)) {
        return;
    }
    memset(&metrics, 0, sizeof(metrics));
    metrics.logical_width = width;
    metrics.logical_height = height;
    metrics.physical_width = (int64_t)(width * scale);
    metrics.physical_height = (int64_t)(height * scale);
    metrics.device_pixel_ratio = scale;
    metrics.refresh_rate = refresh_rate > 0.0f ? refresh_rate : 60.0;
    metrics.view_padding.left = safe_left;
    metrics.view_padding.top = safe_top;
    metrics.view_padding.right = safe_right;
    metrics.view_padding.bottom = safe_bottom;
    metrics.view_insets.bottom = keyboard;
    metrics.system_gesture_insets.left = gesture_left;
    metrics.system_gesture_insets.top = gesture_top;
    metrics.system_gesture_insets.right = gesture_right;
    metrics.system_gesture_insets.bottom = gesture_bottom;
    if (rotation == 90 || rotation == 270) {
        metrics.orientation = 2;
    } else if (rotation == 0 || rotation == 180) {
        metrics.orientation = 1;
    } else {
        metrics.orientation = width > height ? 2 : 1;
    }
    metrics.rotation_degrees = rotation;
    metrics.font_scale = font_scale > 0.0f ? font_scale : 1.0;
    metrics.keyboard_duration_s = keyboard_duration > 0.0f ? keyboard_duration : 0.0;
    metrics.keyboard_curve = keyboard_curve;
    if (env != NULL && cutouts != NULL) {
        jsize n = (*env)->GetArrayLength(env, cutouts);
        jfloat *values = (*env)->GetFloatArrayElements(env, cutouts, NULL);
        int rects = n / 4;
        int i = 0;
        if (rects > AURA_MAX_DISPLAY_CUTOUTS) {
            metrics.cutout_truncated = 1;
            rects = AURA_MAX_DISPLAY_CUTOUTS;
        }
        metrics.cutout_count = rects;
        if (values != NULL) {
            while (i < rects) {
                metrics.cutouts[i].x = values[i * 4];
                metrics.cutouts[i].y = values[i * 4 + 1];
                metrics.cutouts[i].width = values[i * 4 + 2];
                metrics.cutouts[i].height = values[i * 4 + 3];
                i += 1;
            }
            (*env)->ReleaseFloatArrayElements(env, cutouts, values, JNI_ABORT);
        }
        jni_failed(env);
    }
    aura_shell_metrics_v1(slot, &metrics, (uint64_t)timestamp_ns);
}

JNIEXPORT jboolean JNICALL Java_com_noxlang_aura_AuraHost_nativeEnvironment(JNIEnv *env, jobject activity, jint slot, jint brightness, jint high_contrast, jint reduce_motion, jint accessible_navigation, jint text_direction, jint bold_text, jfloat font_scale, jstring locales, jlong timestamp_ns) {
    AuraEnvironmentPayload payload;
    char *utf = NULL;
    size_t length = 0;
    int accepted = 0;
    (void)activity;
    if (android_slot(slot) < 0) {
        return JNI_FALSE;
    }
    memset(&payload, 0, sizeof(payload));
    payload.brightness = brightness;
    payload.high_contrast = high_contrast;
    payload.reduce_motion = reduce_motion;
    payload.accessible_navigation = accessible_navigation;
    payload.text_direction = text_direction;
    payload.bold_text = bold_text;
    payload.font_scale = font_scale > 0.0f ? font_scale : 1.0f;
    if (locales != NULL) {
        utf = utf8_from_java_string(env, locales, &length);
    }
    accepted = aura_shell_environment(slot, &payload, utf != NULL ? utf : "", (uint64_t)timestamp_ns);
    free(utf);
    return accepted != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeMemoryPressure(JNIEnv *env, jobject activity, jint level, jlong timestamp_ns) {
    int slot = 0;
    (void)env;
    (void)activity;
    for (slot = 0; slot < AURA_ANDROID_SLOTS; slot += 1) {
        aura_shell_memory_pressure(slot, level, (uint64_t)timestamp_ns);
    }
    if (level >= AURA_MEMORY_PRESSURE_CRITICAL) {
        aura_storage_purge_cache();
    }
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeVsync(JNIEnv *env, jobject activity) {
    (void)env;
    (void)activity;
    aura_shell_vsync(0);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeLifecycle(JNIEnv *env, jobject activity, jint slot, jint state, jint reason, jlong timestamp_ns) {
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return;
    }
    aura_shell_lifecycle(slot, state, reason, (uint64_t)timestamp_ns);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeSystemBack(JNIEnv *env, jobject activity, jint slot, jint phase, jfloat progress, jlong timestamp_ns) {
    AuraPlatformEvent event;
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return;
    }
    aura_platform_event_init(&event, AURA_SYSTEM_BACK, 0, (uint64_t)timestamp_ns);
    event.key_code = phase;
    event.x = progress;
    aura_shell_post_event(slot, &event);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeRequestClose(JNIEnv *env, jobject activity, jint slot) {
    (void)env;
    (void)activity;
    aura_shell_request_close(slot);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativePlatformRoute(JNIEnv *env, jobject activity, jint source, jstring location, jlong timestamp_ns) {
    const char *utf = NULL;
    (void)activity;
    if (location == NULL) {
        return;
    }
    utf = (*env)->GetStringUTFChars(env, location, NULL);
    if (utf != NULL) {
        aura_shell_platform_route(0, source, utf, (uint64_t)timestamp_ns);
        (*env)->ReleaseStringUTFChars(env, location, utf);
    }
}

JNIEXPORT jboolean JNICALL Java_com_noxlang_aura_AuraHost_nativeSurfaceAttach(JNIEnv *env, jobject activity, jint slot, jint width, jint height, jlong timestamp_ns) {
    AuraAndroidSurfacePlan plan;
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return JNI_FALSE;
    }
    aura_android_surface_attach(&g_android_surface[slot], width, height, &plan);
    post_android_surface(slot, &plan, timestamp_ns);
    return plan.attached ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_noxlang_aura_AuraHost_nativeSurfaceResize(JNIEnv *env, jobject activity, jint slot, jint width, jint height, jlong timestamp_ns) {
    AuraAndroidSurfacePlan plan;
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return JNI_FALSE;
    }
    aura_android_surface_resize(&g_android_surface[slot], width, height, &plan);
    post_android_surface(slot, &plan, timestamp_ns);
    return plan.attached ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_noxlang_aura_AuraHost_nativeSurfaceDetach(JNIEnv *env, jobject activity, jint slot, jlong timestamp_ns) {
    AuraAndroidSurfacePlan plan;
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return JNI_FALSE;
    }
    aura_android_surface_detach(&g_android_surface[slot], &plan);
    post_android_surface(slot, &plan, timestamp_ns);
    return plan.attached ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeTextEdit(JNIEnv *env, jobject activity, jint slot, jint operation, jstring text, jint first, jint second, jlong timestamp_ns) {
    char *utf = NULL;
    size_t length = 0;
    (void)activity;
    if (android_slot(slot) < 0) {
        return;
    }
    if (text != NULL) {
        utf = utf8_from_java_string(env, text, &length);
    }
    aura_shell_text_edit_n(slot, operation, utf != NULL ? utf : "", utf != NULL ? length : 0, first, second, (uint64_t)timestamp_ns);
    if (utf != NULL) {
        free(utf);
    }
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeBlit(JNIEnv *env, jobject activity, jint slot, jobject bitmap) {
    AndroidBitmapInfo info;
    void *pixels = NULL;
    uint8_t *copy = NULL;
    int width = 0;
    int height = 0;
    int stride = 0;
    (void)activity;
    if (android_slot(slot) < 0 || bitmap == NULL || AndroidBitmap_getInfo(env, bitmap, &info) != ANDROID_BITMAP_RESULT_SUCCESS) {
        return;
    }
    if (aura_shell_copy_pixels(slot, &copy, &width, &height, &stride) != 0) {
        return;
    }
    if (AndroidBitmap_lockPixels(env, bitmap, &pixels) != ANDROID_BITMAP_RESULT_SUCCESS) {
        free(copy);
        return;
    }
    {
        static int logged = 0;
        if (!logged) {
            __android_log_print(ANDROID_LOG_INFO, "aura", "blit view %ux%u fmt %d paint %dx%d", info.width, info.height, info.format, width, height);
            logged = 1;
        }
    }
    if (info.format == ANDROID_BITMAP_FORMAT_RGBA_8888 && info.width > 0 && info.height > 0) {
        uint32_t row = 0;
        uint32_t col = 0;
        for (row = 0; row < info.height; row++) {
            uint8_t *dst = (uint8_t *)pixels + (size_t)row * info.stride;
            int sy = (int)((uint64_t)row * (uint64_t)height / info.height);
            const uint8_t *src = copy + (size_t)sy * (size_t)stride;
            if ((int)info.width == width) {
                memcpy(dst, src, (size_t)width * 4);
                continue;
            }
            for (col = 0; col < info.width; col++) {
                int sx = (int)((uint64_t)col * (uint64_t)width / info.width);
                memcpy(dst + (size_t)col * 4, src + (size_t)sx * 4, 4);
            }
        }
    }
    AndroidBitmap_unlockPixels(env, bitmap);
    free(copy);
}

JNIEXPORT jint JNICALL Java_com_noxlang_aura_AuraHost_nativeAxCount(JNIEnv *env, jobject activity, jint slot) {
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return 0;
    }
    return aura_shell_ax_count(slot);
}

JNIEXPORT jstring JNICALL Java_com_noxlang_aura_AuraHost_nativeAxLabel(JNIEnv *env, jobject activity, jint slot, jint index) {
    const char *label = android_slot(slot) < 0 ? "" : aura_shell_ax_label(slot, index);
    (void)activity;
    return (*env)->NewStringUTF(env, label != NULL ? label : "");
}

JNIEXPORT jstring JNICALL Java_com_noxlang_aura_AuraHost_nativeAxValue(JNIEnv *env, jobject activity, jint slot, jint index) {
    const char *value = android_slot(slot) < 0 ? "" : aura_shell_ax_value(slot, index);
    (void)activity;
    return (*env)->NewStringUTF(env, value != NULL ? value : "");
}

JNIEXPORT jstring JNICALL Java_com_noxlang_aura_AuraHost_nativeAxRole(JNIEnv *env, jobject activity, jint slot, jint index) {
    const char *role = android_slot(slot) < 0 ? "" : aura_shell_ax_role(slot, index);
    (void)activity;
    return (*env)->NewStringUTF(env, role != NULL ? role : "");
}

JNIEXPORT jstring JNICALL Java_com_noxlang_aura_AuraHost_nativeAxClassName(JNIEnv *env, jobject activity, jint slot, jint index) {
    const char *role = android_slot(slot) < 0 ? "" : aura_shell_ax_role(slot, index);
    (void)activity;
    return (*env)->NewStringUTF(env, aura_ax_android_class(role));
}

JNIEXPORT jint JNICALL Java_com_noxlang_aura_AuraHost_nativeAxChecked(JNIEnv *env, jobject activity, jint slot, jint index) {
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return 0;
    }
    return aura_shell_ax_checked(slot, index);
}

JNIEXPORT jlong JNICALL Java_com_noxlang_aura_AuraHost_nativeAxPress(JNIEnv *env, jobject activity, jint slot, jint index) {
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return 0;
    }
    return (jlong)aura_shell_ax_press(slot, index);
}

JNIEXPORT jint JNICALL Java_com_noxlang_aura_AuraHost_nativeAxActivate(JNIEnv *env, jobject activity, jint slot, jint index) {
    (void)env;
    (void)activity;
    if (android_slot(slot) < 0) {
        return 0;
    }
    return aura_shell_activate_ax(slot, index);
}

JNIEXPORT jfloatArray JNICALL Java_com_noxlang_aura_AuraHost_nativeAxFrame(JNIEnv *env, jobject activity, jint slot, jint index) {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    jfloat raw[4];
    jfloatArray out = NULL;
    (void)activity;
    if (android_slot(slot) >= 0) {
        aura_shell_ax_frame(slot, index, &x, &y, &w, &h);
    }
    raw[0] = (jfloat)x;
    raw[1] = (jfloat)y;
    raw[2] = (jfloat)w;
    raw[3] = (jfloat)h;
    out = (*env)->NewFloatArray(env, 4);
    (*env)->SetFloatArrayRegion(env, out, 0, 4, raw);
    return out;
}

static const char *jni_utf(JNIEnv *env, jstring value) {
    if (value == NULL) {
        return NULL;
    }
    return (*env)->GetStringUTFChars(env, value, NULL);
}

static void jni_release(JNIEnv *env, jstring value, const char *utf) {
    if (value != NULL && utf != NULL) {
        (*env)->ReleaseStringUTFChars(env, value, utf);
    }
}

JNIEXPORT jstring JNICALL Java_com_noxlang_aura_AuraHost_nativeAssetVersion(JNIEnv *env, jobject activity) {
    (void)activity;
    return (*env)->NewStringUTF(env, AURA_VERSION);
}

JNIEXPORT jboolean JNICALL Java_com_noxlang_aura_AuraHost_nativeAssetStampCurrent(JNIEnv *env, jobject activity, jstring path) {
    const char *utf = NULL;
    int current = 0;
    (void)activity;
    utf = jni_utf(env, path);
    if (utf == NULL) {
        return JNI_FALSE;
    }
    current = aura_storage_stamp_current(utf);
    jni_release(env, path, utf);
    return current != 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeStart(JNIEnv *env, jobject activity, jstring documents, jstring support, jstring cache, jstring temp_dir, jstring assets) {
    AuraStorageSpec spec;
    (void)activity;
    spec.documents = jni_utf(env, documents);
    spec.support = jni_utf(env, support);
    spec.cache = jni_utf(env, cache);
    spec.temp_dir = jni_utf(env, temp_dir);
    spec.assets = jni_utf(env, assets);
    aura_mobile_runtime_start(&spec, runtime_log);
    jni_release(env, documents, spec.documents);
    jni_release(env, support, spec.support);
    jni_release(env, cache, spec.cache);
    jni_release(env, temp_dir, spec.temp_dir);
    jni_release(env, assets, spec.assets);
}

JNIEXPORT void JNICALL Java_com_noxlang_aura_AuraHost_nativeShutdown(JNIEnv *env, jobject activity, jboolean stop_runtime) {
    if (stop_runtime) {
        aura_mobile_runtime_request_stop();
    }
    pthread_mutex_lock(&g_activity_mu);
    if (g_activity != NULL && (*env)->IsSameObject(env, g_activity, activity)) {
        (*env)->DeleteGlobalRef(env, g_activity);
        g_activity = NULL;
        g_invalidate = NULL;
        g_focus = NULL;
        g_text_state = NULL;
        g_text_geometry = NULL;
        g_text_configuration = NULL;
        g_text_boundaries = NULL;
        g_back_result = NULL;
        g_open_overlay = NULL;
        g_close_overlay = NULL;
    }
    pthread_mutex_unlock(&g_activity_mu);
}
