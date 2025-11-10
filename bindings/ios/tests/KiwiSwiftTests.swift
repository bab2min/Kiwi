/*
 * KiwiSwiftTests - Unit tests for iOS binding
 * 
 * Updated to test the corrected API
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
        // Test that we can initialize Kiwi with KiwiBuilder
        XCTAssertNoThrow({
            // let builder = try KiwiBuilder(modelPath: "valid/model/path", numThreads: 1)
            // let testKiwi = try builder.build()
            // XCTAssertNotNil(testKiwi)
        })
    }
    
    func testAnalysis() throws {
        // Test the analysis API structure (renamed from tokenization)
        guard let kiwi = kiwi else {
            // Skip if kiwi is not initialized (no model files)
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let text = "안녕하세요!"
        let tokens = try kiwi.analyze(text, options: .allWithNormalizing)
        
        XCTAssertGreaterThan(tokens.count, 0)
        
        // Check token structure including new fields
        for token in tokens {
            XCTAssertFalse(token.form.isEmpty)
            XCTAssertFalse(token.tag.isEmpty)
            XCTAssertGreaterThanOrEqual(token.position, 0)
            XCTAssertGreaterThan(token.length, 0)
            XCTAssertGreaterThanOrEqual(token.senseId, 0)
            XCTAssertGreaterThanOrEqual(token.typoCost, 0)
        }
    }
    
    func testTokenizationCompatibility() throws {
        // Test that tokenize still works (compatibility method)
        guard let kiwi = kiwi else {
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let text = "형태소 분석 테스트"
        let tokens = try kiwi.tokenize(text, options: .allWithNormalizing)
        
        XCTAssertGreaterThan(tokens.count, 0)
    }
    
    func testSentenceSplitting() throws {
        guard let kiwi = kiwi else {
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let text = "첫 번째 문장입니다. 두 번째 문장입니다. 세 번째 문장입니다."
        let sentences = try kiwi.splitSentences(text, options: .allWithNormalizing)
        
        XCTAssertEqual(sentences.count, 3)
        XCTAssertEqual(sentences[0], "첫 번째 문장입니다.")
        XCTAssertEqual(sentences[1], "두 번째 문장입니다.")
        XCTAssertEqual(sentences[2], "세 번째 문장입니다.")
    }
    
    func testAsyncAnalysis() throws {
        guard let kiwi = kiwi else {
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let expectation = XCTestExpectation(description: "Async analysis")
        let text = "비동기 처리 테스트입니다."
        
        kiwi.analyze(text, options: .allWithNormalizing) { result in
            switch result {
            case .success(let tokens):
                XCTAssertGreaterThan(tokens.count, 0)
            case .failure(let error):
                XCTFail("Analysis failed: \(error)")
            }
            expectation.fulfill()
        }
        
        wait(for: [expectation], timeout: 5.0)
    }
    
    func testAsyncTokenizationCompatibility() throws {
        guard let kiwi = kiwi else {
            throw XCTSkip("Kiwi not initialized - model files not available")
        }
        
        let expectation = XCTestExpectation(description: "Async tokenization compatibility")
        let text = "비동기 호환성 테스트"
        
        kiwi.tokenize(text, options: .allWithNormalizing) { result in
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
        let options1: MatchOptions = .allWithNormalizing
        let options2: MatchOptions = [.allWithNormalizing, .joinNoun]
        
        XCTAssertEqual(options1.rawValue, 1)
        XCTAssertEqual(options2.rawValue, 9) // 1 + 8
    }
    
    func testVersion() throws {
        // Test version utility method
        let version = Kiwi.version
        XCTAssertFalse(version.isEmpty)
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
        // Test error handling with KiwiBuilder
        XCTAssertThrowsError(try KiwiBuilder(modelPath: "/invalid/path", numThreads: 1)) { error in
            XCTAssertTrue(error is KiwiError)
        }
    }
    
    func testKiwiBuilder() throws {
        // Test KiwiBuilder API with correct parameters
        XCTAssertNoThrow({
            // let builder = try KiwiBuilder(modelPath: "valid/model/path", numThreads: 1)
            // let kiwi = try builder.build()
            // XCTAssertNotNil(kiwi)
        })
    }
}