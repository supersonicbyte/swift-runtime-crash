import Hooks

// ─────────────────────────────────────────────────────────────────────────────
// What this demo shows:
//
// 1. install_hooks() replaces _swift_allocObject and _swift_retain with our
//    own functions. On Linux, hooking _swift_allocObject triggers
//    CALL_IMPL_CHECK inside swift_allocObject (PLT), which sets the swizzling
//    flag and routes swift_retain (PLT) through _swift_retain_adapterImpl.
//
// 2. _swift_retain_adapterImpl masks spare bits (top 4 + low 3) and forwards
//    to _swift_retain — our hook. Works fine for real heap pointers.
//
// 3. Swift 6.3 introduced CUSTOM_RR_ENTRYPOINTS (swift_retain_x19, etc.)
//    which the compiler uses so objects in any register can be retained
//    without a move. These trampolines call swift_retain (PLT) with whatever
//    value is in that register — including tagged/bridge pointers (bits 48-59
//    set). The adapter's mask doesn't cover those bits, so they arrive at our
//    hook unchanged. Without the IS_TAGGED guard we would crash.
//
// 4. On macOS none of this happens: the compiler calls _swift_retain (the
//    pointer) directly via GOT, never the PLT entry, so the adapter is
//    irrelevant and tagged pointers never reach the hook.
//
// To inspect generated assembly:
//   swift build -c release
//   # Linux — look for PLT calls:
//   objdump -d .build/release/main | grep -A2 "swift_retain"
//   # macOS — look for GOT-indirect calls:
//   objdump --disassemble .build/release/main | grep -B1 "blr"
// ─────────────────────────────────────────────────────────────────────────────

let platform: String = {
#if os(Linux)
    return "Linux"
#elseif os(macOS)
    return "macOS"
#else
    return "unknown"
#endif
}()

print("╔══════════════════════════════════════════════════════════════════╗")
print("║  swift_retain hook demo — \(platform)")
print("╚══════════════════════════════════════════════════════════════════╝\n")

// ── Step 1: show the masking math ──────────────────────────────────────────
show_mask_demo()

// ── Step 2: install hooks ──────────────────────────────────────────────────
install_hooks()

// ── Step 3: allocate a plain class instance ────────────────────────────────
// This allocation triggers CALL_IMPL_CHECK inside swift_allocObject on Linux.
// After this call the swizzling flag is true and swift_retain (PLT) routes
// through _swift_retain_adapterImpl → our hook.
print("--- class instance (heap object) ---")
class Box {
    var value: Int
    init(_ v: Int) { value = v }
}
let box = Box(42)
print("box.value = \(box.value)\n")

// ── Step 4: String operations ──────────────────────────────────────────────
// On Linux ARM64 / Swift 6.3: String bridging / internal ARC uses
// swift_retain_x19 (and similar) with tagged pointer values. Those hit the
// adapter path and arrive at our hook with bits 48-59 set.
// The IS_TAGGED guard returns early without calling orig — no crash.
print("--- String (may produce TAGGED retains on Linux ARM64 / Swift 6.3) ---")
let s = "Hello from the retain demo"
print("string = \(s)")
let upper = s.uppercased()
print("upper  = \(upper)\n")

// ── Step 5: Array of Strings ───────────────────────────────────────────────
print("--- [String] ---")
let words: [String] = ["alpha", "beta", "gamma", "delta"]
for w in words { print("  \(w)") }
print("")

// ── Step 6: closure that captures a value ─────────────────────────────────
print("--- closure capturing Box ---")
let fn: () -> Int = { box.value * 2 }
print("fn() = \(fn())\n")

// ── Step 7: remove hooks ───────────────────────────────────────────────────
remove_hooks()

print("\nDone.")
print("""

To reproduce the crash manually (Linux ARM64 / Swift 6.3 only):
  Remove the IS_TAGGED guard from my_retain() in hooks.c,
  rebuild, and run. You will see:
    Bad pointer dereference at 0x0500000000000008
  because _swift_retain_adapterImpl passes a tagged pointer (e.g.
  0x0500000000000000) to orig_retain = _swift_retain_, which calls
  isValidPointerForNativeRetain (returns true, bit 63 = 0) and then
  tries to read refCounts at ptr+8 = 0x0500000000000008 (unmapped).
""")
