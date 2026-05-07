import Hooks

final class Box { var x = 42 }

// Keep the original trigger: _swift_allocObject replacement turns on the
// runtime swizzle path, so later retains route through
// _swift_retain_adapterImpl -> _swift_retain (our hook).
install_hooks()

let boxes = (0..<32).map { i -> Box in
    let box = Box()
    box.x = i
    return box
}

let labels = boxes.map { "box:\($0.x)" }

for iteration in 0..<200_000 {
    let rendered = labels
        .enumerated()
        .map { index, label in "\(index)=\(label)" }
        .joined(separator: " | ")

    if iteration % 50_000 == 0 {
        print(rendered.prefix(80))
    }
}

remove_hooks()
print("done")
