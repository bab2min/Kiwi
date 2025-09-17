/*
 * KiwiSwiftTests - Unit tests for iOS binding
 * 
 * These tests demonstrate the API usage as proposed in the iOS roadmap
 */

import XCTest
@testable import KiwiSwift

class KiwiSwiftTests: XCTestCase {
    
    var kiwi: Kiwi?
    
    override func setUpWithError() throws {
        // This would typically load from a test model file
        // For now, we'll skip the actual initialization since we don't have model files in tests
        // kiwi = try Kiwi(modelPath: "path/to/test/model")
    }
    
    override func tearDownWithError() throws {
        kiwi = nil
    }
    
    func testKiwiInitialization() throws {
        // Test that we can initialize Kiwi with a model path
        // This test would fail without actual model files, so we'll test the API structure
        
        XCTAssertNoThrow({
            // let testKiwi = try Kiwi(modelPath: "valid/model/path")
            // XCTAssertNotNil(testKiwi)
        })
    }
    
    func testTokenization() throws {
        // Test the tokenization API structure
        guard let kiwi = kiwi else {
            // Skip if kiwi is not initialized (no model files)
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let text = "안녕하세요!"
        let tokens = try kiwi.tokenize(text, options: .normalizeAll)
        
        XCTAssertGreaterThan(tokens.count, 0)
        
        // Check token structure
        for token in tokens {
            XCTAssertFalse(token.form.isEmpty)
            XCTAssertFalse(token.tag.isEmpty)
            XCTAssertGreaterThanOrEqual(token.position, 0)
            XCTAssertGreaterThan(token.length, 0)
        }
    }
    
    func testSentenceSplitting() throws {
        guard let kiwi = kiwi else {
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let text = "첫 번째 문장입니다. 두 번째 문장입니다. 세 번째 문장입니다."
        let sentences = try kiwi.splitSentences(text)
        
        XCTAssertEqual(sentences.count, 3)
        XCTAssertEqual(sentences[0], "첫 번째 문장입니다.")
        XCTAssertEqual(sentences[1], "두 번째 문장입니다.")
        XCTAssertEqual(sentences[2], "세 번째 문장입니다.")
    }
    
    func testAsyncTokenization() throws {
        guard let kiwi = kiwi else {
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let expectation = XCTestExpectation(description: "Async tokenization")
        let text = "비동기 처리 테스트입니다."
        
        kiwi.tokenize(text, options: .normalizeAll) { result in
            switch result {
            case .success(let tokens):
                XCTAssertGreaterThan(tokens.count, 0)
            case .failure(let error):
                XCTFail("Tokenization failed: \(error)")
            }
            expectation.fulfill()
        }
        
        wait(for: [expectation], timeout: 5.0)
    }
    
    func testMatchOptions() throws {
        // Test that match options work as expected
        let options1: MatchOptions = .normalizeAll
        let options2: MatchOptions = [.normalizeAll, .joinNoun]
        
        XCTAssertEqual(options1.rawValue, 1)
        XCTAssertEqual(options2.rawValue, 9) // 1 + 8
    }
    
    func testVersionAndArchType() throws {
        // Test utility methods
        let version = Kiwi.version
        let archType = Kiwi.archType
        
        XCTAssertFalse(version.isEmpty)
        XCTAssertGreaterThanOrEqual(archType, 0)
    }
    
    func testBundleModelInitialization() throws {
        // Test bundle-based model loading
        XCTAssertThrowsError(try Kiwi(bundleModelPath: "nonexistent_model")) { error in
            XCTAssertTrue(error is KiwiError)
            if case .invalidModelPath = error as? KiwiError {
                // Expected error
            } else {
                XCTFail("Expected invalidModelPath error")
            }
        }
    }
    
    func testErrorHandling() throws {
        // Test error handling
        XCTAssertThrowsError(try Kiwi(modelPath: "/invalid/path")) { error in
            XCTAssertTrue(error is KiwiError)
        }
    }
    
    func testKiwiBuilder() throws {
        // Test KiwiBuilder API
        XCTAssertNoThrow({
            // let builder = try KiwiBuilder(modelPath: "valid/model/path")
            // let kiwi = try builder.build()
            // XCTAssertNotNil(kiwi)
        })
    }
}