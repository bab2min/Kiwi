/*
 * Kiwi iOS binding implementation
 * 
 * This file provides C++ to Objective-C++ bridge for iOS Swift integration
 * Based on the iOS roadmap created for issue #221
 */

#ifdef IOS

#include <string>
#include <vector>
#include <memory>
#include <exception>

// Include main Kiwi headers
#include "kiwi/Kiwi.h"
#include "kiwi/KiwiBuilder.h"

extern "C" {

// Forward declarations for iOS-specific types
typedef struct KiwiInstance KiwiInstance;
typedef struct KiwiBuilderInstance KiwiBuilderInstance;
typedef struct KiwiToken KiwiToken;

// Error handling
typedef struct {
    int code;
    char* message;
} KiwiError;

// Token structure for iOS
struct KiwiToken {
    char* form;
    char* tag;
    int position;
    int length;
    float score;
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

// Builder functions
KiwiBuilderInstance* kiwi_builder_create(const char* model_path, KiwiError** error);
KiwiBuilderInstance* kiwi_builder_create_with_options(const char* model_path, 
                                                      int num_threads, 
                                                      int build_option,
                                                      KiwiError** error);
void kiwi_builder_destroy(KiwiBuilderInstance* builder);

KiwiInstance* kiwi_builder_build(KiwiBuilderInstance* builder, KiwiError** error);

// Main Kiwi functions
KiwiInstance* kiwi_create(const char* model_path, KiwiError** error);
void kiwi_destroy(KiwiInstance* kiwi);

// Tokenization
KiwiTokenResult* kiwi_tokenize(KiwiInstance* kiwi, const char* text, int match_option);
KiwiTokenResult* kiwi_tokenize_with_options(KiwiInstance* kiwi, 
                                           const char* text, 
                                           int match_option,
                                           int top_n,
                                           bool split_complex);

// Sentence splitting
KiwiSentenceResult* kiwi_split_sentences(KiwiInstance* kiwi, 
                                        const char* text,
                                        int min_length,
                                        int max_length);

// Utility functions
const char* kiwi_get_version();
int kiwi_get_arch_type();

} // extern "C"

// Implementation details below...

namespace {
    // Helper function to convert std::string to C string
    char* string_to_c_str(const std::string& str) {
        char* result = new char[str.length() + 1];
        std::strcpy(result, str.c_str());
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

// Implementation of C functions

KiwiInstance* kiwi_create(const char* model_path, KiwiError** error) {
    try {
        auto kiwi = kiwi::Kiwi::create(model_path);
        return reinterpret_cast<KiwiInstance*>(kiwi.release());
    } catch (const std::exception& e) {
        if (error) {
            *error = create_error(1, e.what());
        }
        return nullptr;
    }
}

void kiwi_destroy(KiwiInstance* kiwi) {
    if (kiwi) {
        delete reinterpret_cast<kiwi::Kiwi*>(kiwi);
    }
}

KiwiTokenResult* kiwi_tokenize(KiwiInstance* kiwi_instance, const char* text, int match_option) {
    auto result = new KiwiTokenResult;
    result->tokens = nullptr;
    result->count = 0;
    result->error = nullptr;
    
    try {
        auto kiwi = reinterpret_cast<kiwi::Kiwi*>(kiwi_instance);
        auto tokens = kiwi->analyze(text, static_cast<kiwi::Match>(match_option));
        
        result->count = tokens.size();
        result->tokens = new KiwiToken[result->count];
        
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            result->tokens[i].form = string_to_c_str(token.str);
            result->tokens[i].tag = string_to_c_str(kiwi::tagToString(token.tag));
            result->tokens[i].position = token.start;
            result->tokens[i].length = token.len;
            result->tokens[i].score = token.score;
        }
        
    } catch (const std::exception& e) {
        result->error = create_error(2, e.what());
    }
    
    return result;
}

KiwiSentenceResult* kiwi_split_sentences(KiwiInstance* kiwi_instance, 
                                        const char* text,
                                        int min_length,
                                        int max_length) {
    auto result = new KiwiSentenceResult;
    result->sentences = nullptr;
    result->count = 0;
    result->error = nullptr;
    
    try {
        auto kiwi = reinterpret_cast<kiwi::Kiwi*>(kiwi_instance);
        auto sentences = kiwi->splitIntoSents(text, min_length, max_length);
        
        result->count = sentences.size();
        result->sentences = new char*[result->count];
        
        for (size_t i = 0; i < sentences.size(); ++i) {
            result->sentences[i] = string_to_c_str(sentences[i]);
        }
        
    } catch (const std::exception& e) {
        result->error = create_error(3, e.what());
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
    return kiwi::Kiwi::getVersion().c_str();
}

int kiwi_get_arch_type() {
    return static_cast<int>(kiwi::Kiwi::getArchType());
}

// Builder implementation
KiwiBuilderInstance* kiwi_builder_create(const char* model_path, KiwiError** error) {
    try {
        auto builder = std::make_unique<kiwi::KiwiBuilder>(model_path);
        return reinterpret_cast<KiwiBuilderInstance*>(builder.release());
    } catch (const std::exception& e) {
        if (error) {
            *error = create_error(4, e.what());
        }
        return nullptr;
    }
}

void kiwi_builder_destroy(KiwiBuilderInstance* builder) {
    if (builder) {
        delete reinterpret_cast<kiwi::KiwiBuilder*>(builder);
    }
}

KiwiInstance* kiwi_builder_build(KiwiBuilderInstance* builder_instance, KiwiError** error) {
    try {
        auto builder = reinterpret_cast<kiwi::KiwiBuilder*>(builder_instance);
        auto kiwi = builder->build();
        return reinterpret_cast<KiwiInstance*>(kiwi.release());
    } catch (const std::exception& e) {
        if (error) {
            *error = create_error(5, e.what());
        }
        return nullptr;
    }
}

#endif // IOS