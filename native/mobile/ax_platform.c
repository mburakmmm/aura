#include "ax_platform.h"

#include <string.h>

static int same_role(const char *role, const char *expected) {
    return role != NULL && strcmp(role, expected) == 0;
}

const char *aura_ax_ios_trait(const char *role) {
    if (same_role(role, "button") || same_role(role, "toggle")) {
        return "button";
    }
    if (same_role(role, "text")) {
        return "static-text";
    }
    if (same_role(role, "text-field")) {
        return "text-field";
    }
    if (same_role(role, "image")) {
        return "image";
    }
    if (same_role(role, "adjustable")) {
        return "adjustable";
    }
    return "none";
}

const char *aura_ax_android_class(const char *role) {
    if (same_role(role, "button")) {
        return "android.widget.Button";
    }
    if (same_role(role, "toggle")) {
        return "android.widget.Switch";
    }
    if (same_role(role, "text")) {
        return "android.widget.TextView";
    }
    if (same_role(role, "text-field")) {
        return "android.widget.EditText";
    }
    if (same_role(role, "image")) {
        return "android.widget.ImageView";
    }
    if (same_role(role, "scroll-view")) {
        return "android.widget.ScrollView";
    }
    if (same_role(role, "adjustable")) {
        return "android.widget.SeekBar";
    }
    return "android.view.View";
}
