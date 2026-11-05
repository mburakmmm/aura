#include "android_surface.h"
#include "events.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void expect_int(const char *label, int actual, int expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %d expected %d\n", label, actual, expected);
        failures += 1;
    }
}

static void expect_event(const char *label, const AuraAndroidSurfaceEvent *event, int kind, int generation, int width, int height) {
    expect_int(label, event->kind, kind);
    expect_int(label, event->generation, generation);
    expect_int(label, event->width, width);
    expect_int(label, event->height, height);
}

int main(void) {
    AuraAndroidSurfaceMachine machine;
    AuraAndroidSurfacePlan plan;
    memset(&machine, 0, sizeof(machine));

    aura_android_surface_resize(&machine, 100, 200, &plan);
    expect_int("resize before attach", plan.count, 0);
    expect_int("still detached", plan.attached, 0);
    aura_android_surface_detach(&machine, &plan);
    expect_int("detach before attach", plan.count, 0);

    aura_android_surface_attach(&machine, 0, 0, &plan);
    expect_int("create count", plan.count, 1);
    expect_int("attached", plan.attached, 1);
    expect_event("create", &plan.events[0], AURA_SURFACE_CREATED, 1, 0, 0);

    aura_android_surface_resize(&machine, 0, 40, &plan);
    expect_int("zero width ignored", plan.count, 0);
    expect_int("generation held", machine.generation, 1);
    aura_android_surface_resize(&machine, 1080, 2400, &plan);
    expect_int("change count", plan.count, 1);
    expect_event("change", &plan.events[0], AURA_SURFACE_CHANGED, 1, 1080, 2400);

    aura_android_surface_attach(&machine, 800, 1600, &plan);
    expect_int("recreate count", plan.count, 2);
    expect_event("replace destroy", &plan.events[0], AURA_SURFACE_DESTROYED, 1, 1080, 2400);
    expect_event("replace create", &plan.events[1], AURA_SURFACE_CREATED, 2, 800, 1600);
    expect_int("still attached", plan.attached, 1);

    aura_android_surface_detach(&machine, &plan);
    expect_int("destroy count", plan.count, 1);
    expect_event("destroy", &plan.events[0], AURA_SURFACE_DESTROYED, 2, 800, 1600);
    expect_int("detached", plan.attached, 0);
    aura_android_surface_detach(&machine, &plan);
    expect_int("second detach", plan.count, 0);
    aura_android_surface_resize(&machine, 10, 10, &plan);
    expect_int("resize after detach", plan.count, 0);

    aura_android_surface_attach(&machine, 12, 12, &plan);
    expect_event("next generation", &plan.events[0], AURA_SURFACE_CREATED, 3, 12, 12);

    if (failures != 0) {
        fprintf(stderr, "android surface: %d failures\n", failures);
        return 1;
    }
    printf("android surface ok\n");
    return 0;
}
