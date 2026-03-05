#pragma once

// Install hooks on _swift_allocObject and _swift_retain.
// Hooking allocObject triggers CALL_IMPL_CHECK in the Swift runtime,
// which activates the swizzling flag and routes swift_retain (PLT)
// through _swift_retain_adapterImpl → _swift_retain (our hook).
void install_hooks(void);

// Remove hooks and restore original function pointers.
void remove_hooks(void);

// Show SwiftSpareBitsMask masking math for a given pointer value.
// Demonstrates why tagged pointers (bits 48-59) survive the adapter's mask.
void show_mask_demo(void);
