import Foundation
import CKiwi

/// Build options for KiwiBuilder
public struct BuildOptions: OptionSet {
    public let rawValue: Int32
    
    public init(rawValue: Int32) {
        self.rawValue = rawValue
    }
    
    /// Integrate allomorphs
    public static let integrateAllomorph = BuildOptions(rawValue: 1)
    
    /// Load default dictionary
    public static let loadDefaultDict = BuildOptions(rawValue: 2)
    
    /// Load typo dictionary
    public static let loadTypoDict = BuildOptions(rawValue: 4)
    
    /// Load multi-dict
    public static let loadMultiDict = BuildOptions(rawValue: 8)
    
    /// Default build options
    public static let `default`: BuildOptions = [
        .integrateAllomorph,
        .loadDefaultDict,
        .loadTypoDict,
        .loadMultiDict
    ]
}

/// Builder class for creating Kiwi instances
public final class KiwiBuilder {
    private var wrapper: HandleWrapper<kiwi_builder_h>?
    
    /// Initialize KiwiBuilder with model path
    /// - Parameters:
    ///   - modelPath: Path to the model directory
    ///   - numThreads: Number of threads to use (-1 for automatic)
    ///   - options: Build options
    ///   - enabledDialects: Enabled dialects
    /// - Throws: KiwiError if initialization fails
    public init(
        modelPath: String,
        numThreads: Int = -1,
        options: BuildOptions = .default,
        enabledDialects: Dialect = .standard
    ) throws {
        let handle = kiwi_builder_init(
            modelPath,
            Int32(numThreads),
            Int32(options.rawValue),
            Int32(enabledDialects.rawValue)
        )
        
        guard let handle = handle else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to initialize KiwiBuilder")
        }
        
        self.wrapper = HandleWrapper(handle) { kiwi_builder_close($0) }
    }
    
    /// Initialize KiwiBuilder from a Bundle
    /// - Parameters:
    ///   - bundle: Bundle containing the model files
    ///   - modelDirectory: Name of the model directory in the bundle (default: "KiwiModels")
    ///   - numThreads: Number of threads to use (-1 for automatic)
    ///   - options: Build options
    ///   - enabledDialects: Enabled dialects
    /// - Throws: KiwiError if initialization fails or model not found
    public convenience init(
        bundle: Bundle,
        modelDirectory: String = "KiwiModels",
        numThreads: Int = -1,
        options: BuildOptions = .default,
        enabledDialects: Dialect = .standard
    ) throws {
        guard let modelPath = bundle.resourcePath?
            .appending("/\(modelDirectory)") else {
            throw KiwiError.modelNotFound("Model directory not found in bundle")
        }
        
        try self.init(
            modelPath: modelPath,
            numThreads: numThreads,
            options: options,
            enabledDialects: enabledDialects
        )
    }
    
    /// Add a user word to the dictionary
    /// - Parameters:
    ///   - word: Word to add
    ///   - tag: Part-of-speech tag
    ///   - score: Score for the word (default: 0)
    /// - Returns: true if successful
    /// - Throws: KiwiError if operation fails
    @discardableResult
    public func addWord(_ word: String, tag: POSTag, score: Float = 0) throws -> Bool {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        let result = kiwi_builder_add_word(handle, word, tag.description, score)
        
        if result != 0 {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            return false
        }
        
        return true
    }
    
    /// Load user dictionary from file
    /// - Parameter dictPath: Path to the dictionary file
    /// - Returns: Number of words added
    /// - Throws: KiwiError if operation fails
    @discardableResult
    public func loadDict(_ dictPath: String) throws -> Int {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        let result = kiwi_builder_load_dict(handle, dictPath)
        
        if result < 0 {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to load dictionary")
        }
        
        return Int(result)
    }
    
    /// Build a Kiwi instance
    /// - Parameters:
    ///   - typoTransformer: Optional typo transformer
    ///   - typoCostThreshold: Threshold for typo cost (default: 2.5)
    /// - Returns: A new Kiwi instance
    /// - Throws: KiwiError if build fails
    public func build(
        typoTransformer: TypoTransformer? = nil,
        typoCostThreshold: Float = 2.5
    ) throws -> Kiwi {
        guard let handle = wrapper?.handle else {
            throw KiwiError.invalidHandle
        }
        
        let typoHandle = typoTransformer?.handle
        let kiwiHandle = kiwi_builder_build(handle, typoHandle, typoCostThreshold)
        
        guard let kiwiHandle = kiwiHandle else {
            if let errorMsg = kiwi_error() {
                let error = String(cString: errorMsg)
                kiwi_clear_error()
                throw KiwiError.operationFailed(error)
            }
            throw KiwiError.operationFailed("Failed to build Kiwi")
        }
        
        return Kiwi(handle: kiwiHandle)
    }
}
