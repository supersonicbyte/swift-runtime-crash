#include <stdio.h>
#include <stdint.h>
#include "Hooks.h"

// ── Swift runtime opaque types ───────────────────────────────────────────────
typedef struct HeapObject_s   HeapObject;
typedef struct HeapMetadata_s HeapMetadata;

// The Swift runtime exports these as MUTABLE function pointers.
// Writing to them redirects all calls through our hooks.
//
// On macOS: the compiler emits GOT-indirect calls to these pointers directly.
//   e.g.  ldr  x8, [_swift_retain@GOTPAGEOFF]
//         blr  x8
//
// On Linux: the compiler emits PLT calls to swift_retain (no underscore).
//   e.g.  bl   swift_retain          ; resolves to PLT stub
//   When swizzling is active the PLT stub detours through
//   _swift_retain_adapterImpl → _swift_retain (this pointer, our hook).
extern HeapObject * (*_swift_allocObject)(HeapMetadata const *metadata,
                                          size_t requiredSize,
                                          size_t requiredAlignmentMask);
extern HeapObject * (*_swift_retain)(HeapObject *object);
extern HeapObject * (*_swift_release)(HeapObject *object);

// ── Saved originals ──────────────────────────────────────────────────────────
static HeapObject * (*orig_alloc)(HeapMetadata const *, size_t, size_t) = NULL;
static HeapObject * (*orig_retain)(HeapObject *) = NULL;
static HeapObject * (*orig_release)(HeapObject *) = NULL;

// ── Re-entrancy guard ────────────────────────────────────────────────────────
// printf itself may trigger retains (lazy globals, string formatting).
// Without this guard those retains would recurse back into our hook → crash.
static _Thread_local int hook_depth = 0;

// ── Tagged pointer detection ─────────────────────────────────────────────────
//
// On Linux ARM64 with Swift 6.3, when swizzling is active, the adapter
// (_swift_retain_adapterImpl) masks pointer bits with SwiftSpareBitsMask
// before forwarding to _swift_retain (our hook):
//
//   SwiftSpareBitsMask = 0xF000000000000007   (top 4 bits + low 3 bits)
//   masked = ptr & ~SwiftSpareBitsMask
//          = ptr &  0x0FFFFFFFFFFFFFF8
//
// Tagged/bridge pointers encode metadata in bits 48-59 (e.g. 0x0500000000000060).
// These bits are NOT covered by SwiftSpareBitsMask, so they survive masking.
// _swift_retain_adapterImpl passes them unchanged to _swift_retain (us).
//
// On Linux ARM64 with 48-bit VA, all valid user-space heap pointers satisfy:
//   bits 48-63 == 0   →   (uintptr_t)ptr >> 48 == 0
// So checking bits 48+ is a reliable way to identify non-heap pointers.
//
#if defined(__linux__) && (defined(__aarch64__) || defined(__arm64__))
#  define IS_TAGGED(p) ((uintptr_t)(p) >> 48)
#else
#  define IS_TAGGED(p) 0
#endif

// ── Allocation hook ──────────────────────────────────────────────────────────
//
// Hooking _swift_allocObject is what ACTIVATES swizzling on Linux.
// Inside swift_allocObject (the PLT entry) there is a CALL_IMPL_CHECK macro:
//
//   if (_swift_allocObject != &__swift_allocObject_) {
//       _swift_enableSwizzlingFlag = true;
//       _swift_retain_adapter      = _swift_retain_adapterImpl;
//   }
//
// Once this fires, swift_retain (PLT) starts routing through the adapter.
// On macOS this mechanism doesn't matter — the compiler calls _swift_retain
// (the pointer) directly, so the adapter is never in the way.
//
static HeapObject * my_alloc(HeapMetadata const *meta,
                              size_t size,
                              size_t align) {
    HeapObject *ret = orig_alloc(meta, size, align);
    if (hook_depth == 0) {
        hook_depth = 1;
        printf("[alloc]   %p   size=%-4zu  "
               "(this call triggers CALL_IMPL_CHECK → swizzling active on Linux)\n",
               (void *)ret, size);
        hook_depth = 0;
    }
    return ret;
}

// ── Retain hook ──────────────────────────────────────────────────────────────
static HeapObject * my_retain(HeapObject *object) {
    int tagged = IS_TAGGED(object);

    if (hook_depth == 0) {
        hook_depth = 1;
        if (tagged) {
            // bits 48-63 are set — this is a tagged/bridge pointer.
            // SwiftSpareBitsMask only covers bits 60-63 and 0-2, so these
            // bits were NOT cleared by the adapter. Calling orig_retain here
            // would reach _swift_retain_: isValidPointerForNativeRetain
            // returns true (bit 63 = 0, value is positive), then it tries
            // to read refCounts at ptr+8 → crash at unmapped address.
            printf("[retain]  %p  <<< TAGGED (high byte 0x%02llx, bits 48-63 set)"
                   " — skipping orig to avoid crash\n",
                   (void *)object,
                   (unsigned long long)((uintptr_t)object >> 48));
        } else {
            printf("[retain]  %p\n", (void *)object);
        }
        hook_depth = 0;
    }

    // GUARD: tagged pointers are not heap objects.
    // Returning without calling orig is safe — retain is a no-op for them.
    if (tagged) return object;

    return orig_retain(object);
}

// ── Release hook ─────────────────────────────────────────────────────────────
static HeapObject * my_release(HeapObject *object) {
    int tagged = IS_TAGGED(object);

    if (hook_depth == 0) {
        hook_depth = 1;
        if (tagged) {
            printf("[release] %p  <<< TAGGED — skipping orig\n", (void *)object);
        } else {
            printf("[release] %p\n", (void *)object);
        }
        hook_depth = 0;
    }

    if (tagged) return object;
    return orig_release(object);
}

// ── Install / remove ─────────────────────────────────────────────────────────
void install_hooks(void) {
    printf("┌─ install_hooks ──────────────────────────────────────────────────\n");
    printf("│  _swift_allocObject  before: %p\n", (void *)_swift_allocObject);
    printf("│  _swift_retain       before: %p\n", (void *)_swift_retain);
    printf("│  _swift_release      before: %p\n", (void *)_swift_release);

    orig_alloc   = _swift_allocObject;
    orig_retain  = _swift_retain;
    orig_release = _swift_release;

    _swift_allocObject = my_alloc;
    _swift_retain      = my_retain;
    _swift_release     = my_release;

    printf("│\n");
    printf("│  _swift_allocObject  after:  %p  (→ my_alloc)\n",  (void *)_swift_allocObject);
    printf("│  _swift_retain       after:  %p  (→ my_retain)\n", (void *)_swift_retain);
    printf("│  _swift_release      after:  %p  (→ my_release)\n",(void *)_swift_release);
    printf("└──────────────────────────────────────────────────────────────────\n\n");
}

void remove_hooks(void) {
    _swift_allocObject = orig_alloc;
    _swift_retain      = orig_retain;
    _swift_release     = orig_release;
    printf("\n┌─ remove_hooks ───────────────────────────────────────────────────\n");
    printf("│  pointers restored to originals\n");
    printf("└──────────────────────────────────────────────────────────────────\n");
}

// ── Mask demo ─────────────────────────────────────────────────────────────────
void show_mask_demo(void) {
    // Two example tagged pointer values seen in real crashes:
    uintptr_t examples[] = { 0x0500000000000060ULL, 0x0900000000000060ULL };
    uintptr_t spare      = 0xF000000000000007ULL;   // SwiftSpareBitsMask on ARM64

    printf("┌─ SwiftSpareBitsMask masking demo ────────────────────────────────\n");
    printf("│  SwiftSpareBitsMask  = 0x%016llx  (top 4 bits + low 3 bits)\n",
           (unsigned long long)spare);
    printf("│  ~mask               = 0x%016llx  (what the adapter ANDs with ptr)\n",
           (unsigned long long)~spare);
    printf("│\n");

    for (int i = 0; i < 2; i++) {
        uintptr_t ptr    = examples[i];
        uintptr_t masked = ptr & ~spare;
        int survives = (masked >> 48) != 0;
        printf("│  ptr    = 0x%016llx\n", (unsigned long long)ptr);
        printf("│  masked = 0x%016llx  bits 48-59 %s\n",
               (unsigned long long)masked,
               survives ? "STILL SET → crash" : "cleared (safe)");
        printf("│\n");
    }

    printf("│  Conclusion: bits 48-59 are NOT in SwiftSpareBitsMask.\n");
    printf("│  Tagged pointers with metadata in those bits survive masking\n");
    printf("│  unchanged and crash _swift_retain_ at ptr+8 (unmapped).\n");
    printf("└──────────────────────────────────────────────────────────────────\n\n");
}
