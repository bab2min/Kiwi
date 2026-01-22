# Swift Bindings Development Guide

## Overview

This document provides technical details for developers working on the Kiwi Swift bindings.

## Architecture

The Swift bindings use a direct C interoperability approach:

```
┌─────────────────────┐
│  Swift API Layer    │  User-friendly Swift interface
│  (Kiwi.swift, etc)  │
└──────────┬──────────┘
           │
┌──────────┴──────────┐
│  CKiwi Module       │  C API bridging via module.modulemap
└──────────┬──────────┘
           │
┌──────────┴──────────┐
│  libkiwi_static.a   │  Static C++ library
└─────────────────────┘
```

## File Structure

```
bindings/swift/
├── Package.swift              # Swift Package Manager manifest
├── README.md                  # User documentation
├── CMakeLists.txt            # Build configuration
├── .gitignore                # Git ignore rules
├── Sources/
│   ├── CKiwi/                # C module for bridging
│   │   ├── module.modulemap  # C module definition
│   │   └── include/          # Symbolic links to C headers
│   │       ├── capi.h -> ../../../../../include/kiwi/capi.h
│   │       └── Macro.h -> ../../../../../include/kiwi/Macro.h
│   └── Kiwi/                 # Swift wrapper layer
│       ├── Kiwi.swift        # Main analyzer class
│       ├── KiwiBuilder.swift # Builder pattern
│       ├── Token.swift       # Token structures
│       ├── POSTag.swift      # POS tag enum
│       ├── MatchOptions.swift # Analysis options
│       ├── Dialect.swift     # Dialect flags
│       ├── Joiner.swift      # Morpheme joiner
│       ├── MorphemeSet.swift # Morpheme blacklist
│       ├── TypoTransformer.swift # Typo correction
│       ├── Errors.swift      # Error types
│       └── Internal/
│           └── HandleWrapper.swift # RAII wrapper for C handles
├── Tests/
│   └── KiwiTests/
│       └── KiwiTests.swift   # Unit tests
└── scripts/
    └── build-xcframework.sh  # XCFramework build script
```

## Key Design Patterns

### 1. RAII via HandleWrapper

C handles are wrapped in a Swift class that automatically releases resources:

```swift
internal final class HandleWrapper<H> {
    let handle: H
    private let cleanup: (H) -> Void
    
    init(_ handle: H, cleanup: @escaping (H) -> Void) {
        self.handle = handle
        self.cleanup = cleanup
    }
    
    deinit {
        cleanup(handle)
    }
}
```

### 2. Swift-Friendly Types

C types are mapped to Swift types:
- C `kiwi_h` → Swift `Kiwi` class
- C `kiwi_token_info_t` → Swift `Token` struct
- C flags → Swift `OptionSet` (MatchOptions, Dialect)
- C enums → Swift `enum` (POSTag)

### 3. Error Handling

C error codes are converted to Swift errors:

```swift
if result != 0 {
    if let errorMsg = kiwi_error() {
        let error = String(cString: errorMsg)
        kiwi_clear_error()
        throw KiwiError.operationFailed(error)
    }
}
```

## Building

### For Development (macOS)

```bash
cd bindings/swift
swift build
swift test
```

### For Production (XCFramework)

```bash
cd bindings/swift
./scripts/build-xcframework.sh
```

This creates `xcframework/Kiwi.xcframework` containing:
- iOS Device (arm64)
- iOS Simulator (arm64 + x86_64)
- macOS (arm64 + x86_64)

## Testing

### Unit Tests

Run basic unit tests:
```bash
cd bindings/swift
swift test
```

### Integration Tests

Integration tests require actual Kiwi model files. These are not included in unit tests to keep them lightweight.

## CI/CD

GitHub Actions workflow (`.github/workflows/swift.yml`) runs:
1. Swift package build and test
2. XCFramework build (on main branch)
3. Linux compatibility check

## Memory Management

All C handles are automatically released via Swift's ARC system:

- `Kiwi` → calls `kiwi_close()`
- `KiwiBuilder` → calls `kiwi_builder_close()`
- `Joiner` → calls `kiwi_joiner_close()`
- `MorphemeSet` → calls `kiwi_morphset_close()`
- `TypoTransformer` → calls `kiwi_typo_close()` (if owned)

## Thread Safety

The Swift bindings maintain the same thread safety guarantees as the underlying C API:
- Multiple `Kiwi` instances can be used concurrently
- Individual `Kiwi` instances should not be shared across threads without synchronization

## Future Enhancements

Potential areas for improvement:

1. **Binary Distribution**: 
   - Publish pre-built XCFramework via GitHub Releases
   - Update Package.swift to reference binary framework

2. **Additional Features**:
   - Word extraction APIs
   - Substring extractor
   - Pattern matching

3. **Async/Await**:
   - Swift async/await wrapper for long-running operations
   - Currently only sync APIs are provided

4. **Documentation**:
   - DocC documentation comments
   - Swift DocC build for hosted documentation

## Contributing

When adding new features:

1. Add C API function calls in appropriate Swift wrapper
2. Convert C types to Swift types appropriately
3. Add error handling
4. Update tests
5. Update README with examples
6. Update this guide if architecture changes

## Troubleshooting

### Symbol Not Found

If you get "symbol not found" errors, ensure:
- Symbolic links in `Sources/CKiwi/include/` are valid
- Headers are properly exposed in module.modulemap
- Library is correctly linked

### Build Failures

Common issues:
- Missing Git LFS files (model files)
- Incorrect symbolic link paths
- Platform-specific build settings

### Runtime Errors

Check:
- Model files are accessible
- Correct path to model directory
- Sufficient memory available

## License

Swift bindings are licensed under the same license as Kiwi.
