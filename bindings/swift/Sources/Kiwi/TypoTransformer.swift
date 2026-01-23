import Foundation
import CKiwi

/// Typo transformer for automatic typo correction
public final class TypoTransformer {
    internal let handle: kiwi_typo_h
    private let shouldClose: Bool
    
    internal init(handle: kiwi_typo_h, shouldClose: Bool = true) {
        self.handle = handle
        self.shouldClose = shouldClose
    }
    
    /// Create a new empty typo transformer
    /// - Throws: KiwiError if creation fails
    public init() throws {
        guard let handle = kiwi_typo_init() else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to create typo transformer")
        }
        
        self.handle = handle
        self.shouldClose = true
    }
    
    /// Get the default basic typo transformer
    /// - Returns: A typo transformer with basic typo set
    /// - Throws: KiwiError if creation fails
    public static func basic() throws -> TypoTransformer {
        guard let handle = kiwi_typo_get_basic() else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to get basic typo transformer")
        }
        
        return TypoTransformer(handle: handle, shouldClose: false)
    }
    
    /// Typo set types
    public enum TypoSet: Int32 {
        case withoutTypo = 0
        case basicTypoSet = 1
        case continualTypoSet = 2
        case basicTypoSetWithContinual = 3
        case lengtheningTypoSet = 4
        case basicTypoSetWithContinualAndLengthening = 5
    }
    
    /// Get default typo transformer with specified typo set
    /// - Parameter typoSet: The typo set to use
    /// - Returns: A typo transformer
    /// - Throws: KiwiError if creation fails
    public static func `default`(_ typoSet: TypoSet = .basicTypoSet) throws -> TypoTransformer {
        guard let handle = kiwi_typo_get_default(typoSet.rawValue) else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to get default typo transformer")
        }
        
        return TypoTransformer(handle: handle, shouldClose: false)
    }
    
    /// Copy this typo transformer
    /// - Returns: A new typo transformer with the same configuration
    /// - Throws: KiwiError if copy fails
    public func copy() throws -> TypoTransformer {
        guard let newHandle = kiwi_typo_copy(handle) else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to copy typo transformer")
        }
        
        return TypoTransformer(handle: newHandle, shouldClose: true)
    }
    
    deinit {
        if shouldClose {
            kiwi_typo_close(handle)
        }
    }
}
