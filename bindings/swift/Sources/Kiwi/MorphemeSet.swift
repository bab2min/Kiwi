import Foundation
import CKiwi

/// Set of morphemes (used as blacklist in analysis)
public final class MorphemeSet {
    private var wrapper: HandleWrapper<kiwi_morphset_h>?
    
    internal init(handle: kiwi_morphset_h) {
        self.wrapper = HandleWrapper(handle) { kiwi_morphset_close($0) }
    }
    
    internal var handle: kiwi_morphset_h? {
        return wrapper?.handle
    }
    
    /// Add a morpheme to the set
    /// - Parameters:
    ///   - form: Form of the morpheme
    ///   - tag: Part-of-speech tag (nil to match all tags)
    /// - Returns: Number of morphemes added
    /// - Throws: KiwiError if operation fails
    @discardableResult
    public func add(form: String, tag: POSTag? = nil) throws -> Int {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        let tagStr = tag?.description
        let result = kiwi_morphset_add(handle, form, tagStr)
        
        if result < 0 {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to add morpheme to set")
        }
        
        return Int(result)
    }
}
