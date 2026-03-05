import Hooks

class Box { var x = 42 }

// Hooking _swift_allocObject triggers CALL_IMPL_CHECK in the Swift runtime,
// which sets the swizzling flag so that swift_retain (PLT) routes through
// _swift_retain_adapterImpl → _swift_retain (our hook).
install_hooks()

let b = Box()    // first allocation fires CALL_IMPL_CHECK → swizzling active
print(b.x)       // triggers Character retain through the adapter → crash on Linux ARM64 / Swift 6.3
