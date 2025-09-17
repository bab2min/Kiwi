/*
 * KiwiSwift - Swift wrapper for Kiwi Korean morphological analyzer
 * 
 * Fixed to use the actual Kiwi C++ API correctly
 */

import Foundation

// MARK: - Error Types

public enum KiwiError: Error {
    case initializationFailed(String)
    case analysisFailed(String)
    case sentenceSplitFailed(String)
    case invalidModelPath
    case unknownError(String)
    
    var localizedDescription: String {
        switch self {
        case .initializationFailed(let message):
            return "Kiwi initialization failed: \(message)"
        case .analysisFailed(let message):
            return "Analysis failed: \(message)"
        case .sentenceSplitFailed(let message):
            return "Sentence splitting failed: \(message)"
        case .invalidModelPath:
            return "Invalid model path provided"
        case .unknownError(let message):
            return "Unknown error: \(message)"
        }
    }
}

// MARK: - Match Options

public struct MatchOptions: OptionSet {
    public let rawValue: Int
    
    public init(rawValue: Int) {
        self.rawValue = rawValue
    }
    
    public static let none = MatchOptions([])
    public static let allWithNormalizing = MatchOptions(rawValue: 1)
    public static let all = MatchOptions(rawValue: 2)
    public static let normalizeOnly = MatchOptions(rawValue: 4)
    public static let joinNoun = MatchOptions(rawValue: 8)
}

// MARK: - Token Structure

public struct Token {
    public let form: String
    public let tag: String
    public let position: Int
    public let length: Int
    public let score: Float
    public let senseId: Int
    public let typoCost: Float
    
    public init(form: String, tag: String, position: Int, length: Int, score: Float, senseId: Int = 0, typoCost: Float = 0) {
        self.form = form
        self.tag = tag
        self.position = position
        self.length = length
        self.score = score
        self.senseId = senseId
        self.typoCost = typoCost
    }
}

// MARK: - Kiwi Builder

public class KiwiBuilder {
    private var builderPtr: OpaquePointer?
    
    public init(modelPath: String, numThreads: Int = 1) throws {
        var error: UnsafeMutablePointer<KiwiError>?
        
        builderPtr = kiwi_builder_create(modelPath, numThreads, &error)
        
        if let error = error {
            let message = String(cString: error.pointee.message)
            kiwi_free_error(error)
            throw KiwiError.initializationFailed(message)
        }
        
        guard builderPtr != nil else {
            throw KiwiError.initializationFailed("Failed to create KiwiBuilder")
        }
    }
    
    deinit {
        if let ptr = builderPtr {
            kiwi_builder_destroy(ptr)
        }
    }
    
    public func build() throws -> Kiwi {
        guard let ptr = builderPtr else {
            throw KiwiError.initializationFailed("Builder is not initialized")
        }
        
        var error: UnsafeMutablePointer<KiwiError>?
        let kiwiPtr = kiwi_builder_build(ptr, &error)
        
        if let error = error {
            let message = String(cString: error.pointee.message)
            kiwi_free_error(error)
            throw KiwiError.initializationFailed(message)
        }
        
        guard let kiwiPtr = kiwiPtr else {
            throw KiwiError.initializationFailed("Failed to build Kiwi instance")
        }
        
        return Kiwi(kiwiPtr: kiwiPtr)
    }
}

// MARK: - Main Kiwi Class

public class Kiwi {
    private var kiwiPtr: OpaquePointer?
    
    // Private initializer for internal use
    internal init(kiwiPtr: OpaquePointer) {
        self.kiwiPtr = kiwiPtr
    }
    
    // Public convenience initializer using KiwiBuilder
    public convenience init(modelPath: String, numThreads: Int = 1) throws {
        let builder = try KiwiBuilder(modelPath: modelPath, numThreads: numThreads)
        let kiwi = try builder.build()
        self.init(kiwiPtr: kiwi.kiwiPtr!)
        kiwi.kiwiPtr = nil // Transfer ownership
    }
    
    deinit {
        if let ptr = kiwiPtr {
            kiwi_destroy(ptr)
        }
    }
    
    // MARK: - Analysis (Fixed method name)
    
    public func analyze(_ text: String, options: MatchOptions = .allWithNormalizing) throws -> [Token] {
        guard let ptr = kiwiPtr else {
            throw KiwiError.analysisFailed("Kiwi instance is not initialized")
        }
        
        let result = kiwi_analyze(ptr, text, Int32(options.rawValue))
        defer { kiwi_free_token_result(result) }
        
        if let error = result?.pointee.error {
            let message = String(cString: error.pointee.message)
            throw KiwiError.analysisFailed(message)
        }
        
        guard let tokens = result?.pointee.tokens else {
            return []
        }
        
        let count = result?.pointee.count ?? 0
        var swiftTokens: [Token] = []
        
        for i in 0..<count {
            let token = tokens[i]
            let swiftToken = Token(
                form: String(cString: token.form),
                tag: String(cString: token.tag),
                position: Int(token.position),
                length: Int(token.length),
                score: token.score,
                senseId: Int(token.senseId),
                typoCost: token.typoCost
            )
            swiftTokens.append(swiftToken)
        }
        
        return swiftTokens
    }
    
    // Keep old method name for compatibility
    public func tokenize(_ text: String, options: MatchOptions = .allWithNormalizing) throws -> [Token] {
        return try analyze(text, options: options)
    }
    
    // MARK: - Sentence Splitting
    
    public func splitSentences(_ text: String, options: MatchOptions = .allWithNormalizing) throws -> [String] {
        guard let ptr = kiwiPtr else {
            throw KiwiError.sentenceSplitFailed("Kiwi instance is not initialized")
        }
        
        let result = kiwi_split_sentences(ptr, text, Int32(options.rawValue))
        defer { kiwi_free_sentence_result(result) }
        
        if let error = result?.pointee.error {
            let message = String(cString: error.pointee.message)
            throw KiwiError.sentenceSplitFailed(message)
        }
        
        guard let sentences = result?.pointee.sentences else {
            return []
        }
        
        let count = result?.pointee.count ?? 0
        var swiftSentences: [String] = []
        
        for i in 0..<count {
            if let sentence = sentences[i] {
                swiftSentences.append(String(cString: sentence))
            }
        }
        
        return swiftSentences
    }
    
    // MARK: - Utility Methods
    
    public static var version: String {
        return String(cString: kiwi_get_version())
    }
}

// MARK: - Extension for iOS-specific functionality

extension Kiwi {
    /// Load model from app bundle
    public convenience init(bundleModelPath: String, numThreads: Int = 1) throws {
        guard let bundlePath = Bundle.main.path(forResource: bundleModelPath, ofType: nil) else {
            throw KiwiError.invalidModelPath
        }
        try self.init(modelPath: bundlePath, numThreads: numThreads)
    }
    
    /// Analyze with completion handler for async usage
    public func analyze(_ text: String, 
                       options: MatchOptions = .allWithNormalizing,
                       completion: @escaping (Result<[Token], KiwiError>) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let tokens = try self.analyze(text, options: options)
                DispatchQueue.main.async {
                    completion(.success(tokens))
                }
            } catch let error as KiwiError {
                DispatchQueue.main.async {
                    completion(.failure(error))
                }
            } catch {
                DispatchQueue.main.async {
                    completion(.failure(.unknownError(error.localizedDescription)))
                }
            }
        }
    }
    
    /// Tokenize with completion handler for async usage (compatibility method)
    public func tokenize(_ text: String, 
                        options: MatchOptions = .allWithNormalizing,
                        completion: @escaping (Result<[Token], KiwiError>) -> Void) {
        analyze(text, options: options, completion: completion)
    }
}