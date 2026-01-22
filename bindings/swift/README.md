# Kiwi Swift Bindings

Swift bindings for Kiwi Korean morphological analyzer.

## Overview

This package provides Swift bindings for the Kiwi Korean morphological analyzer, allowing iOS and macOS applications to perform Korean language processing.

## Requirements

- iOS 12.0+ / macOS 10.14+
- Swift 5.7+

## Installation

### Swift Package Manager

Add the following to your `Package.swift` file:

```swift
dependencies: [
    .package(url: "https://github.com/bab2min/Kiwi.git", from: "0.22.0")
]
```

Or add it through Xcode:
1. File → Add Packages...
2. Enter the repository URL: `https://github.com/bab2min/Kiwi.git`
3. Select the version you want to use

## Quick Start

### Basic Usage

```swift
import Kiwi

do {
    // Create a KiwiBuilder with model path
    let builder = try KiwiBuilder(modelPath: "/path/to/model")
    
    // Build Kiwi instance
    let kiwi = try builder.build()
    
    // Tokenize text
    let tokens = try kiwi.tokenize("안녕하세요 형태소 분석기 키위입니다")
    
    for token in tokens {
        print("\(token.form)/\(token.tag.description)")
    }
    
} catch {
    print("Error: \(error)")
}
```

### Using Bundle for Model Loading

```swift
import Kiwi

do {
    // Load model from app bundle
    let builder = try KiwiBuilder(bundle: .main, modelDirectory: "KiwiModels")
    let kiwi = try builder.build()
    
    let tokens = try kiwi.tokenize("키위는 한국어 형태소 분석기입니다")
    
} catch {
    print("Error: \(error)")
}
```

### Advanced Analysis

```swift
import Kiwi

do {
    let builder = try KiwiBuilder(modelPath: "/path/to/model")
    
    // Add custom words
    try builder.addWord("키위", tag: .nng, score: 0.0)
    
    let kiwi = try builder.build()
    
    // Get multiple analysis results
    let results = try kiwi.analyze("형태소 분석", topN: 3)
    
    for result in results {
        print("Score: \(result.score)")
        for token in result.tokens {
            print("  \(token.form)/\(token.tag.description)")
        }
    }
    
} catch {
    print("Error: \(error)")
}
```

### Sentence Splitting

```swift
import Kiwi

do {
    let kiwi = try KiwiBuilder(modelPath: "/path/to/model").build()
    
    let text = "안녕하세요. 키위는 형태소 분석기입니다. 한국어를 분석합니다."
    let sentences = try kiwi.splitIntoSentences(text)
    
    for sentence in sentences {
        print("Sentence: \(sentence.text)")
    }
    
} catch {
    print("Error: \(error)")
}
```

### Using Joiner

```swift
import Kiwi

do {
    let kiwi = try KiwiBuilder(modelPath: "/path/to/model").build()
    let joiner = kiwi.createJoiner()
    
    try joiner.add(form: "가다", tag: .vv)
    try joiner.add(form: "았", tag: .ep)
    try joiner.add(form: "습니다", tag: .ef)
    
    let text = try joiner.join()
    print(text) // "갔습니다"
    
} catch {
    print("Error: \(error)")
}
```

## API Documentation

### Core Classes

- **KiwiBuilder**: Builder for creating Kiwi instances
- **Kiwi**: Main morphological analyzer
- **Token**: Represents a morphological token
- **TokenResult**: Analysis result containing multiple token candidates
- **Joiner**: Combines morphemes into text
- **MorphemeSet**: Set of morphemes (for blacklisting)
- **TypoTransformer**: Typo correction transformer

### Enums and Options

- **POSTag**: Part-of-speech tags (61 tags)
- **MatchOptions**: Analysis matching options
- **Dialect**: Korean dialect flags
- **BuildOptions**: Builder configuration options

## License

This project is licensed under the same license as the main Kiwi project.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Support

For issues and questions, please use the GitHub issue tracker.
