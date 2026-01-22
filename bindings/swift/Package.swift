// swift-tools-version: 5.7
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "Kiwi",
    platforms: [
        .iOS(.v12),
        .macOS(.v10_14)
    ],
    products: [
        .library(
            name: "Kiwi",
            targets: ["Kiwi"]),
    ],
    dependencies: [],
    targets: [
        .target(
            name: "CKiwi",
            dependencies: [],
            path: "Sources/CKiwi"
        ),
        .target(
            name: "Kiwi",
            dependencies: ["CKiwi"],
            path: "Sources/Kiwi"
        ),
        .testTarget(
            name: "KiwiTests",
            dependencies: ["Kiwi"],
            path: "Tests/KiwiTests"
        ),
    ]
)
