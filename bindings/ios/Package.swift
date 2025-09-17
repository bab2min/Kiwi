// swift-tools-version:5.5
// Package.swift for KiwiSwift iOS binding

import PackageDescription

let package = Package(
    name: "KiwiSwift",
    platforms: [
        .iOS(.v12),
        .macOS(.v10_15)
    ],
    products: [
        .library(
            name: "KiwiSwift",
            targets: ["KiwiSwift"]
        ),
    ],
    dependencies: [
        // No external dependencies - self-contained
    ],
    targets: [
        .target(
            name: "KiwiSwift",
            dependencies: [],
            path: "swift",
            sources: ["Kiwi.swift"],
            publicHeadersPath: "../include",
            cxxSettings: [
                .define("IOS", to: "1"),
                .define("KIWI_IOS_BINDING", to: "1"),
                .headerSearchPath("../../../include"),
                .headerSearchPath("../include"),
                .unsafeFlags(["-std=c++17", "-O3"])
            ],
            linkerSettings: [
                .linkedLibrary("c++")
            ]
        ),
        .testTarget(
            name: "KiwiSwiftTests",
            dependencies: ["KiwiSwift"],
            path: "tests"
        ),
    ],
    cxxLanguageStandard: .cxx17
)