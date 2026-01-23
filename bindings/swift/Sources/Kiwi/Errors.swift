import Foundation

/// Errors that can be thrown by Kiwi operations
public enum KiwiError: Error, LocalizedError {
    /// Invalid handle passed to function
    case invalidHandle
    
    /// Invalid index or parameter
    case invalidIndex
    
    /// Operation failed with error message
    case operationFailed(String)
    
    /// General failure
    case failure(String)
    
    /// Model file not found
    case modelNotFound(String)
    
    /// Invalid UTF-8 string
    case invalidString
    
    public var errorDescription: String? {
        switch self {
        case .invalidHandle:
            return "Invalid handle"
        case .invalidIndex:
            return "Invalid index"
        case .operationFailed(let message):
            return "Operation failed: \(message)"
        case .failure(let message):
            return message
        case .modelNotFound(let path):
            return "Model not found at path: \(path)"
        case .invalidString:
            return "Invalid UTF-8 string"
        }
    }
}
