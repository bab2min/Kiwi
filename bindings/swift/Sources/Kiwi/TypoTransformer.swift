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
    
    /// Condition on the preceding morpheme for a typo definition
    public enum Condition: Int32 {
        /// No condition
        case none = 0
        /// Any preceding morpheme
        case any = 1
        /// Only after a syllable without a final consonant
        case vowel = 2
        /// Only after an applosive final consonant (all finals except ㄴ, ㄹ, ㅁ, ㅇ)
        case applosive = 8
    }

    /// Add typo definitions. Every pair of `orig` and `error` is added.
    /// - Parameters:
    ///   - orig: Correct forms
    ///   - error: Typo forms of `orig`
    ///   - cost: Cost of the typo
    ///   - condition: Condition on the preceding morpheme
    /// - Throws: KiwiError if the operation fails (e.g. on a handle from `basic()` or `default(_:)`)
    public func addTypo(orig: [String], error: [String], cost: Float, condition: Condition = .none) throws {
        let origPtrs = orig.map { strdup($0) }
        let errorPtrs = error.map { strdup($0) }
        defer {
            origPtrs.forEach { free($0) }
            errorPtrs.forEach { free($0) }
        }
        var o = origPtrs.map { $0.map { UnsafePointer($0) } }
        var e = errorPtrs.map { $0.map { UnsafePointer($0) } }
        let ret = o.withUnsafeMutableBufferPointer { ob in
            e.withUnsafeMutableBufferPointer { eb in
                kiwi_typo_add(handle, ob.baseAddress, Int32(orig.count), eb.baseAddress, Int32(error.count), cost, condition.rawValue)
            }
        }
        try check(ret, "Failed to add typo")
    }

    /// Set the cost of continual typos (연철). Disabled by default (infinite cost).
    public func setContinualTypoCost(_ threshold: Float) throws {
        try check(kiwi_typo_set_continual_typo_cost(handle, threshold), "Failed to set continual typo cost")
    }

    /// Set the cost of lengthening typos (장음화). Disabled by default (infinite cost).
    public func setLengtheningTypoCost(_ threshold: Float) throws {
        try check(kiwi_typo_set_lengthening_typo_cost(handle, threshold), "Failed to set lengthening typo cost")
    }

    /// Set the cost of jamo-level fuzzy typos
    /// - Parameters:
    ///   - threshold: Cost of a single jamo edit. Fuzzy typos are disabled by default (infinite cost).
    ///   - realWordErrors: If true, also correct spans that are already valid dictionary forms (slower).
    ///   - minJamo: Minimum number of jamo in a dictionary form to be edited
    /// - Throws: KiwiError if the operation fails (e.g. on a handle from `basic()` or `default(_:)`)
    public func setFuzzyTypoCost(_ threshold: Float, realWordErrors: Bool = false, minJamo: Int = 3) throws {
        try check(kiwi_typo_set_fuzzy_typo_cost(handle, threshold, realWordErrors ? 1 : 0, Int32(minJamo)), "Failed to set fuzzy typo cost")
    }

    /// Add all typo definitions of another typo transformer
    public func update(from other: TypoTransformer) throws {
        try check(kiwi_typo_update(handle, other.handle), "Failed to update typo transformer")
    }

    /// Multiply all typo costs by `scale`
    public func scaleCost(_ scale: Float) throws {
        try check(kiwi_typo_scale_cost(handle, scale), "Failed to scale typo cost")
    }

    private func check(_ ret: Int32, _ fallback: String) throws {
        guard ret != 0 else { return }
        if let errorMsg = kiwi_error() {
            let error = String(cString: errorMsg)
            kiwi_clear_error()
            throw KiwiError.operationFailed(error)
        }
        throw KiwiError.operationFailed(fallback)
    }

    /// Prepare the typo transformer for use in analysis
    /// - Returns: A PreparedTypoTransformer ready for use
    /// - Throws: KiwiError if preparation fails
    public func prepare() throws -> PreparedTypoTransformer {
        guard let preparedHandle = kiwi_typo_prepare(handle) else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to prepare typo transformer")
        }

        return PreparedTypoTransformer(handle: preparedHandle)
    }

    deinit {
        if shouldClose {
            kiwi_typo_close(handle)
        }
    }
}

/// Prepared typo transformer for use in analysis
public final class PreparedTypoTransformer {
    internal let handle: kiwi_prepared_typo_h

    internal init(handle: kiwi_prepared_typo_h) {
        self.handle = handle
    }

    deinit {
        kiwi_prepared_typo_close(handle)
    }
}
