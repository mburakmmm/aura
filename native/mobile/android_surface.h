#ifndef AURA_ANDROID_SURFACE_H
#define AURA_ANDROID_SURFACE_H

/* Android view surface transitions. The shell accepts or rejects the events
 * this machine emits; the machine itself decides which events are legal for
 * the view lifecycle. Zero-initialized state is detached at generation 0. */

typedef struct AuraAndroidSurfaceEvent {
    int kind;
    int generation;
    int width;
    int height;
} AuraAndroidSurfaceEvent;

typedef struct AuraAndroidSurfaceMachine {
    int attached;
    int generation;
    int width;
    int height;
} AuraAndroidSurfaceMachine;

typedef struct AuraAndroidSurfacePlan {
    int count;
    AuraAndroidSurfaceEvent events[2];
    int attached;
} AuraAndroidSurfacePlan;

void aura_android_surface_attach(AuraAndroidSurfaceMachine *machine, int width, int height, AuraAndroidSurfacePlan *plan);
void aura_android_surface_resize(AuraAndroidSurfaceMachine *machine, int width, int height, AuraAndroidSurfacePlan *plan);
void aura_android_surface_detach(AuraAndroidSurfaceMachine *machine, AuraAndroidSurfacePlan *plan);

#endif
