// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "retain",
    targets: [
        .executableTarget(
            name: "main",
            dependencies: ["Hooks"],
            path: "Sources/main",
        ),
        .target(
            name: "Hooks",
            path: "Sources/Hooks",
            publicHeadersPath: "include"
        ),
    ]
)
