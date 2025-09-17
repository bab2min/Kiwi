/*
 * KiwiSwift - Swift wrapper for Kiwi Korean morphological analyzer
 * 
 * This implements the Swift API proposed in the iOS roadmap for issue #221
 */

import Foundation

// MARK: - Error Types

public enum KiwiError: Error {
    case initializationFailed(String)
    case tokenizationFailed(String)
    case sentenceSplitFailed(String)
    case invalidModelPath
    case unknownError(String)
    
    var localizedDescription: String {
        switch self {
        case .initializationFailed(let message):
            return "Kiwi initialization failed: \(message)"
        case .tokenizationFailed(let message):
            return "Tokenization failed: \(message)"
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
    public static let normalizeAll = MatchOptions(rawValue: 1)
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
    
    public init(form: String, tag: String, position: Int, length: Int, score: Float) {
        self.form = form
        self.tag = tag
        self.position = position
        self.length = length
        self.score = score
    }
}

// MARK: - Kiwi Builder

public class KiwiBuilder {
    private var builderPtr: OpaquePointer?
    
    public init(modelPath: String) throws {
        var error: UnsafeMutablePointer<KiwiError>?
        
        builderPtr = kiwi_builder_create(modelPath, &error)
        
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
    
    // Public convenience initializer
    public convenience init(modelPath: String) throws {
        var error: UnsafeMutablePointer<KiwiError>?
        
        let ptr = kiwi_create(modelPath, &error)
        
        if let error = error {
            let message = String(cString: error.pointee.message)
            kiwi_free_error(error)
            throw KiwiError.initializationFailed(message)
        }
        
        guard let ptr = ptr else {
            throw KiwiError.initializationFailed("Failed to create Kiwi instance")
        }
        
        self.init(kiwiPtr: ptr)
    }
    
    deinit {
        if let ptr = kiwiPtr {
            kiwi_destroy(ptr)
        }
    }
    
    // MARK: - Tokenization
    
    public func tokenize(_ text: String, options: MatchOptions = .normalizeAll) throws -> [Token] {
        guard let ptr = kiwiPtr else {
            throw KiwiError.tokenizationFailed("Kiwi instance is not initialized")
        }
        
        let result = kiwi_tokenize(ptr, text, Int32(options.rawValue))
        defer { kiwi_free_token_result(result) }
        
        if let error = result?.pointee.error {
            let message = String(cString: error.pointee.message)
            throw KiwiError.tokenizationFailed(message)
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
                score: token.score
            )
            swiftTokens.append(swiftToken)
        }
        
        return swiftTokens
    }
    
    // MARK: - Sentence Splitting
    
    public func splitSentences(_ text: String, minLength: Int = 10, maxLength: Int = 1000) throws -> [String] {
        guard let ptr = kiwiPtr else {
            throw KiwiError.sentenceSplitFailed("Kiwi instance is not initialized")
        }
        
        let result = kiwi_split_sentences(ptr, text, Int32(minLength), Int32(maxLength))
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
    
    public static var archType: Int {
        return Int(kiwi_get_arch_type())
    }
}

// MARK: - Extension for iOS-specific functionality

extension Kiwi {
    /// Load model from app bundle
    public convenience init(bundleModelPath: String) throws {
        guard let bundlePath = Bundle.main.path(forResource: bundleModelPath, ofType: nil) else {
            throw KiwiError.invalidModelPath
        }
        try self.init(modelPath: bundlePath)
    }
    
    /// Tokenize with completion handler for async usage
    public func tokenize(_ text: String, 
                        options: MatchOptions = .normalizeAll,
                        completion: @escaping (Result<[Token], KiwiError>) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let tokens = try self.tokenize(text, options: options)
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
}