#include "android_surface.h"

#include "events.h"

#include <string.h>

static void clear_plan(const AuraAndroidSurfaceMachine *machine, AuraAndroidSurfacePlan *plan) {
    memset(plan, 0, sizeof(*plan));
    plan->attached = machine->attached;
}

static void push_event(AuraAndroidSurfacePlan *plan, int kind, int generation, int width, int height) {
    AuraAndroidSurfaceEvent *event = &plan->events[plan->count];
    event->kind = kind;
    event->generation = generation;
    event->width = width;
    event->height = height;
    plan->count += 1;
}

void aura_android_surface_attach(AuraAndroidSurfaceMachine *machine, int width, int height, AuraAndroidSurfacePlan *plan) {
    clear_plan(machine, plan);
    if (machine->attached) {
        push_event(plan, AURA_SURFACE_DESTROYED, machine->generation, machine->width, machine->height);
    }
    machine->generation += 1;
    machine->attached = 1;
    machine->width = width;
    machine->height = height;
    push_event(plan, AURA_SURFACE_CREATED, machine->generation, width, height);
    plan->attached = 1;
}

void aura_android_surface_resize(AuraAndroidSurfaceMachine *machine, int width, int height, AuraAndroidSurfacePlan *plan) {
    clear_plan(machine, plan);
    if (!machine->attached || width < 1 || height < 1) {
        return;
    }
    machine->width = width;
    machine->height = height;
    push_event(plan, AURA_SURFACE_CHANGED, machine->generation, width, height);
    plan->attached = 1;
}

void aura_android_surface_detach(AuraAndroidSurfaceMachine *machine, AuraAndroidSurfacePlan *plan) {
    clear_plan(machine, plan);
    if (!machine->attached) {
        return;
    }
    push_event(plan, AURA_SURFACE_DESTROYED, machine->generation, machine->width, machine->height);
    machine->attached = 0;
    plan->attached = 0;
}
