import XCTest
@testable import Kiwi

final class KiwiTests: XCTestCase {
    
    func testPOSTagDescription() {
        XCTAssertEqual(POSTag.nng.description, "NNG")
        XCTAssertEqual(POSTag.nnp.description, "NNP")
        XCTAssertEqual(POSTag.vv.description, "VV")
        XCTAssertEqual(POSTag.jks.description, "JKS")
    }
    
    func testPOSTagFromString() {
        XCTAssertEqual(POSTag(string: "NNG"), .nng)
        XCTAssertEqual(POSTag(string: "VV"), .vv)
        XCTAssertEqual(POSTag(string: "nng"), .nng)
        XCTAssertNil(POSTag(string: "INVALID"))
    }
    
    func testMatchOptionsBasic() {
        let options: MatchOptions = [.url, .email]
        XCTAssertTrue(options.contains(.url))
        XCTAssertTrue(options.contains(.email))
        XCTAssertFalse(options.contains(.hashtag))
    }
    
    func testMatchOptionsAll() {
        let options = MatchOptions.all
        XCTAssertTrue(options.contains(.url))
        XCTAssertTrue(options.contains(.email))
        XCTAssertTrue(options.contains(.hashtag))
        XCTAssertTrue(options.contains(.mention))
    }
    
    func testDialectOptions() {
        let dialects: Dialect = [.gyeonggi, .jeju]
        XCTAssertTrue(dialects.contains(.gyeonggi))
        XCTAssertTrue(dialects.contains(.jeju))
        XCTAssertFalse(dialects.contains(.gangwon))
    }
    
    func testTokenInitialization() {
        let token = Token(
            form: "테스트",
            tag: .nng,
            position: 0,
            length: 3,
            score: 1.0
        )
        
        XCTAssertEqual(token.form, "테스트")
        XCTAssertEqual(token.tag, .nng)
        XCTAssertEqual(token.position, 0)
        XCTAssertEqual(token.length, 3)
        XCTAssertEqual(token.score, 1.0)
    }
    
    func testTokenDescription() {
        let token = Token(form: "테스트", tag: .nng)
        XCTAssertEqual(token.description, "테스트/NNG")
    }
    
    func testVersion() {
        let version = Kiwi.version
        XCTAssertFalse(version.isEmpty)
        XCTAssertNotEqual(version, "unknown")
    }
    
    // MARK: - Integration Tests
    
    func testKiwiBuilderAndTokenize() throws {
        // Try to find model path - in CI it should be at ../../models/cong/base
        let modelPath = "../../models/cong/base"
        
        // Skip test if model not available (for local development without models)
        let fileManager = FileManager.default
        guard fileManager.fileExists(atPath: modelPath) else {
            print("Model not found at \(modelPath), skipping integration test")
            return
        }
        
        // Create builder and build Kiwi instance
        let builder = try KiwiBuilder(modelPath: modelPath, numThreads: 1)
        let kiwi = try builder.build()
        
        // Test basic tokenization
        let text = "안녕하세요"
        let tokens = try kiwi.tokenize(text)
        
        // Verify we got some tokens
        XCTAssertFalse(tokens.isEmpty, "Tokenization should return tokens")
        
        // Verify token structure
        for token in tokens {
            XCTAssertFalse(token.form.isEmpty, "Token form should not be empty")
            XCTAssertGreaterThanOrEqual(token.position, 0, "Token position should be non-negative")
            XCTAssertGreaterThan(token.length, 0, "Token length should be positive")
        }
    }
    
    func testKiwiAnalyze() throws {
        let modelPath = "../../models/cong/base"
        
        guard FileManager.default.fileExists(atPath: modelPath) else {
            print("Model not found, skipping test")
            return
        }
        
        let builder = try KiwiBuilder(modelPath: modelPath, numThreads: 1)
        let kiwi = try builder.build()
        
        // Test analyze with multiple results
        let text = "형태소 분석"
        let results = try kiwi.analyze(text, topN: 2)
        
        // Should have at least one result
        XCTAssertFalse(results.isEmpty, "Analysis should return results")
        
        // First result should have tokens
        if let firstResult = results.first {
            XCTAssertFalse(firstResult.tokens.isEmpty, "Result should have tokens")
            XCTAssertGreaterThan(firstResult.score, 0, "Result should have positive score")
        }
    }
    
    func testSplitIntoSentences() throws {
        let modelPath = "../../models/cong/base"
        
        guard FileManager.default.fileExists(atPath: modelPath) else {
            print("Model not found, skipping test")
            return
        }
        
        let builder = try KiwiBuilder(modelPath: modelPath, numThreads: 1)
        let kiwi = try builder.build()
        
        // Test sentence splitting
        let text = "안녕하세요. 키위입니다. 형태소 분석을 합니다."
        let sentences = try kiwi.splitIntoSentences(text)
        
        // Should have 3 sentences
        XCTAssertEqual(sentences.count, 3, "Should split into 3 sentences")
        
        // Verify sentence structure
        for sentence in sentences {
            XCTAssertFalse(sentence.text.isEmpty, "Sentence text should not be empty")
            XCTAssertGreaterThanOrEqual(sentence.start, 0, "Sentence start should be non-negative")
            XCTAssertGreaterThan(sentence.length, 0, "Sentence length should be positive")
        }
    }
    
    func testJoiner() throws {
        let modelPath = "../../models/cong/base"
        
        guard FileManager.default.fileExists(atPath: modelPath) else {
            print("Model not found, skipping test")
            return
        }
        
        let builder = try KiwiBuilder(modelPath: modelPath, numThreads: 1)
        let kiwi = try builder.build()
        
        // Test joiner
        let joiner = kiwi.createJoiner()
        try joiner.add(form: "형태소", tag: .nng)
        try joiner.add(form: "분석", tag: .nng)
        
        let joined = try joiner.join()
        XCTAssertFalse(joined.isEmpty, "Joined text should not be empty")
    }
    
    // Note: Integration tests that require model files are not included
    // as they would require access to actual Kiwi model files
}
