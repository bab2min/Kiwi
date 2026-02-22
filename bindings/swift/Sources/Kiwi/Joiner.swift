import Foundation
import CKiwi

/// Joiner for combining morphemes into text
public final class Joiner {
    private var wrapper: HandleWrapper<kiwi_joiner_h>?
    
    internal init(handle: kiwi_joiner_h) {
        self.wrapper = HandleWrapper(handle) { kiwi_joiner_close($0) }
    }
    
    /// Add a morpheme to the joiner
    /// - Parameters:
    ///   - form: Form of the morpheme
    ///   - tag: Part-of-speech tag
    ///   - autoDetectIrregular: Automatically detect irregular conjugation (default: true)
    /// - Throws: KiwiError if operation fails
    public func add(form: String, tag: POSTag, autoDetectIrregular: Bool = true) throws {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        let result = kiwi_joiner_add(handle, form, tag.description, autoDetectIrregular ? 1 : 0)
        
        if result != 0 {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to add morpheme to joiner")
        }
    }
    
    /// Get the joined text from all added morphemes
    /// - Returns: Combined text
    /// - Throws: KiwiError if operation fails
    public func join() throws -> String {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        guard let resultPtr = kiwi_joiner_get(handle) else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to get joined text")
        }
        
        return String(cString: resultPtr)
    }
}
