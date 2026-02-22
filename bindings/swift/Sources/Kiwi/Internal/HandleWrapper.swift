import Foundation

/// Internal wrapper for C handles that provides RAII-style cleanup
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
