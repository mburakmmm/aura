#ifndef AURA_MOBILE_AX_PLATFORM_H
#define AURA_MOBILE_AX_PLATFORM_H

/* Role strings match aura/semantics/platform_map.nox for nodes the host ABI publishes. */

const char *aura_ax_ios_trait(const char *role);
const char *aura_ax_android_class(const char *role);

#endif
