/*
 * Kiwi iOS Framework Header
 * 
 * Objective-C header for Swift integration
 * Fixed to use actual Kiwi C++ API correctly
 */

#ifndef KIWI_IOS_H
#define KIWI_IOS_H

#import <Foundation/Foundation.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations using actual C++ types
typedef struct kiwi_Kiwi* KiwiInstance;
typedef struct kiwi_KiwiBuilder* KiwiBuilderInstance;

// Error structure
typedef struct {
    int code;
    char* _Nullable message;
} KiwiError;

// Token structure - based on actual kiwi::TokenInfo
typedef struct {
    char* _Nonnull form;
    char* _Nonnull tag;
    int position;
    int length;
    float score;
    int senseId;
    float typoCost;
} KiwiToken;

// Result structures
typedef struct {
    KiwiToken* _Nullable tokens;
    size_t count;
    KiwiError* _Nullable error;
} KiwiTokenResult;

typedef struct {
    char* _Nullable * _Nullable sentences;
    size_t count;
    KiwiError* _Nullable error;
} KiwiSentenceResult;

// Match options (corresponds to kiwi::Match)
typedef NS_ENUM(NSInteger, KiwiMatchOption) {
    KiwiMatchNone = 0,
    KiwiMatchAllWithNormalizing = 1,
    KiwiMatchAll = 2,
    KiwiMatchNormalizeOnly = 4,
    KiwiMatchJoinNoun = 8
};

// Memory management
void kiwi_free_token_result(KiwiTokenResult* _Nullable result);
void kiwi_free_sentence_result(KiwiSentenceResult* _Nullable result);
void kiwi_free_error(KiwiError* _Nullable error);

// Builder functions - using actual KiwiBuilder API
KiwiBuilderInstance* _Nullable kiwi_builder_create(const char* _Nonnull model_path, size_t num_threads, KiwiError* _Nullable * _Nullable error);
void kiwi_builder_destroy(KiwiBuilderInstance* _Nullable builder);
KiwiInstance* _Nullable kiwi_builder_build(KiwiBuilderInstance* _Nonnull builder, KiwiError* _Nullable * _Nullable error);

// Main Kiwi functions
void kiwi_destroy(KiwiInstance* _Nullable kiwi);

// Analysis using actual analyze method
KiwiTokenResult* _Nonnull kiwi_analyze(KiwiInstance* _Nonnull kiwi, const char* _Nonnull text, int match_option);

// Sentence splitting using actual splitIntoSents method
KiwiSentenceResult* _Nonnull kiwi_split_sentences(KiwiInstance* _Nonnull kiwi, 
                                                  const char* _Nonnull text,
                                                  int match_option);

// Utility functions
const char* _Nonnull kiwi_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* KIWI_IOS_H */