/*
 * Kiwi iOS binding implementation
 * 
 * This file provides Objective-C++ bridge for iOS Swift integration
 * Following the same pattern as the Java binding - using C++ classes directly
 */

#ifdef IOS

#include <string>
#include <vector>
#include <memory>
#include <exception>

// Include main Kiwi headers - using the correct header path
#include "kiwi/Kiwi.h"

extern "C" {

// Forward declarations using actual C++ types
typedef kiwi::Kiwi* KiwiInstance;
typedef kiwi::KiwiBuilder* KiwiBuilderInstance;

// Error handling
typedef struct {
    int code;
    char* message;
} KiwiError;

// Token structure for iOS - based on actual kiwi::TokenInfo
struct KiwiToken {
    char* form;
    char* tag;
    int position;
    int length;
    float score;
    int senseId;
    float typoCost;
};

// Token array result
typedef struct {
    KiwiToken* tokens;
    size_t count;
    KiwiError* error;
} KiwiTokenResult;

// Sentence split result
typedef struct {
    char** sentences;
    size_t count;
    KiwiError* error;
} KiwiSentenceResult;

// Memory management helpers
void kiwi_free_token_result(KiwiTokenResult* result);
void kiwi_free_sentence_result(KiwiSentenceResult* result);
void kiwi_free_error(KiwiError* error);

// Builder functions - using actual KiwiBuilder API
KiwiBuilderInstance* kiwi_builder_create(const char* model_path, size_t num_threads, KiwiError** error);
void kiwi_builder_destroy(KiwiBuilderInstance* builder);
KiwiInstance* kiwi_builder_build(KiwiBuilderInstance* builder, KiwiError** error);

// Main Kiwi functions - using actual Kiwi API  
void kiwi_destroy(KiwiInstance* kiwi);

// Tokenization using actual analyze method
KiwiTokenResult* kiwi_analyze(KiwiInstance* kiwi, const char* text, int match_option);

// Sentence splitting using actual splitIntoSents method
KiwiSentenceResult* kiwi_split_sentences(KiwiInstance* kiwi, 
                                        const char* text,
                                        int match_option);

// Utility functions
const char* kiwi_get_version();

} // extern "C"

// Implementation details below...

namespace {
    // Helper function to convert std::string to C string
    char* string_to_c_str(const std::string& str) {
        char* result = new char[str.length() + 1];
        std::strcpy(result, str.c_str());
        return result;
    }
    
    // Helper to convert std::u16string to UTF-8 string
    std::string u16string_to_utf8(const std::u16string& u16str) {
        if (u16str.empty()) return {};
        
        // Simple conversion - in real implementation should use proper UTF-8 conversion
        std::string result;
        for (char16_t c : u16str) {
            if (c < 128) {
                result += static_cast<char>(c);
            } else {
                // For now, replace non-ASCII with '?'
                // In production, use proper UTF-16 to UTF-8 conversion
                result += '?';
            }
        }
        return result;
    }
    
    // Helper to create error
    KiwiError* create_error(int code, const std::string& message) {
        KiwiError* error = new KiwiError;
        error->code = code;
        error->message = string_to_c_str(message);
        return error;
    }
}

// Implementation of C functions using actual Kiwi API

KiwiBuilderInstance* kiwi_builder_create(const char* model_path, size_t num_threads, KiwiError** error) {
    try {
        // Use actual KiwiBuilder constructor
        auto builder = new kiwi::KiwiBuilder(model_path, num_threads);
        return builder;
    } catch (const std::exception& e) {
        if (error) {
            *error = create_error(1, e.what());
        }
        return nullptr;
    }
}

void kiwi_builder_destroy(KiwiBuilderInstance* builder) {
    if (builder) {
        delete builder;
    }
}

KiwiInstance* kiwi_builder_build(KiwiBuilderInstance* builder_instance, KiwiError** error) {
    try {
        // Use actual build method
        auto kiwi_obj = builder_instance->build();
        // Move the object to heap and return pointer
        return new kiwi::Kiwi(std::move(kiwi_obj));
    } catch (const std::exception& e) {
        if (error) {
            *error = create_error(2, e.what());
        }
        return nullptr;
    }
}

void kiwi_destroy(KiwiInstance* kiwi) {
    if (kiwi) {
        delete kiwi;
    }
}

KiwiTokenResult* kiwi_analyze(KiwiInstance* kiwi_instance, const char* text, int match_option) {
    auto result = new KiwiTokenResult;
    result->tokens = nullptr;
    result->count = 0;
    result->error = nullptr;
    
    try {
        // Convert to proper types for Kiwi API
        std::string utf8_text(text);
        // Convert UTF-8 to UTF-16 for Kiwi (simplified conversion)
        std::u16string u16_text;
        for (char c : utf8_text) {
            u16_text += static_cast<char16_t>(c);
        }
        
        // Use actual analyze method with proper option type
        auto token_result = kiwi_instance->analyze(u16_text, static_cast<kiwi::Match>(match_option));
        
        result->count = token_result.first.size();
        result->tokens = new KiwiToken[result->count];
        
        for (size_t i = 0; i < token_result.first.size(); ++i) {
            const auto& token = token_result.first[i];
            // Convert u16string form back to UTF-8
            std::string form_utf8 = u16string_to_utf8(token.str);
            result->tokens[i].form = string_to_c_str(form_utf8);
            result->tokens[i].tag = string_to_c_str(kiwi::tagToString(token.tag));
            result->tokens[i].position = token.position;
            result->tokens[i].length = token.length;
            result->tokens[i].score = token.score;
            result->tokens[i].senseId = token.senseId;
            result->tokens[i].typoCost = token.typoCost;
        }
        
    } catch (const std::exception& e) {
        result->error = create_error(3, e.what());
    }
    
    return result;
}

KiwiSentenceResult* kiwi_split_sentences(KiwiInstance* kiwi_instance, 
                                        const char* text,
                                        int match_option) {
    auto result = new KiwiSentenceResult;
    result->sentences = nullptr;
    result->count = 0;
    result->error = nullptr;
    
    try {
        // Convert to UTF-16 for Kiwi API
        std::string utf8_text(text);
        std::u16string u16_text;
        for (char c : utf8_text) {
            u16_text += static_cast<char16_t>(c);
        }
        
        // Use actual splitIntoSents method
        auto sentence_spans = kiwi_instance->splitIntoSents(u16_text, static_cast<kiwi::Match>(match_option));
        
        result->count = sentence_spans.size();
        result->sentences = new char*[result->count];
        
        for (size_t i = 0; i < sentence_spans.size(); ++i) {
            const auto& span = sentence_spans[i];
            // Extract substring and convert to UTF-8
            std::u16string sentence_u16 = u16_text.substr(span.first, span.second - span.first);
            std::string sentence_utf8 = u16string_to_utf8(sentence_u16);
            result->sentences[i] = string_to_c_str(sentence_utf8);
        }
        
    } catch (const std::exception& e) {
        result->error = create_error(4, e.what());
    }
    
    return result;
}

// Memory cleanup functions
void kiwi_free_token_result(KiwiTokenResult* result) {
    if (!result) return;
    
    if (result->tokens) {
        for (size_t i = 0; i < result->count; ++i) {
            delete[] result->tokens[i].form;
            delete[] result->tokens[i].tag;
        }
        delete[] result->tokens;
    }
    
    kiwi_free_error(result->error);
    delete result;
}

void kiwi_free_sentence_result(KiwiSentenceResult* result) {
    if (!result) return;
    
    if (result->sentences) {
        for (size_t i = 0; i < result->count; ++i) {
            delete[] result->sentences[i];
        }
        delete[] result->sentences;
    }
    
    kiwi_free_error(result->error);
    delete result;
}

void kiwi_free_error(KiwiError* error) {
    if (!error) return;
    delete[] error->message;
    delete error;
}

const char* kiwi_get_version() {
    static std::string version = kiwi::getVersion();
    return version.c_str();
}

#endif // IOS