// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "retain",
    targets: [
        .executableTarget(
            name: "main",
            dependencies: ["Hooks"],
            path: "Sources/main",
            swiftSettings: [.unsafeFlags(["-Onone"])]   // keep all calls visible in assembly
        ),
        .target(
            name: "Hooks",
            path: "Sources/Hooks",
            publicHeadersPath: "include"
        ),
    ]
)
