#pragma once

#define KIWIERR_INVALID_HANDLE -1
#define KIWIERR_INVALID_INDEX -2

#define DECL_DLL __declspec(dllexport)

typedef void* PKIWI;
typedef void* PKIWIRESULT;

#ifdef __cplusplus  
extern "C" {
#endif 

DECL_DLL int kiwi_version();
DECL_DLL const char* kiwi_error();

DECL_DLL PKIWI kiwi_init(const char* modelPath, int maxCache, int numThread);
DECL_DLL int kiwi_loadUserDict(PKIWI handle, const char* dictPath);
DECL_DLL int kiwi_prepare(PKIWI handle);
DECL_DLL PKIWIRESULT kiwi_analyzeW(PKIWI handle, const wchar_t* text, int topN);
DECL_DLL PKIWIRESULT kiwi_analyze(PKIWI handle, const char* text, int topN);
DECL_DLL int kiwi_clearCache(PKIWI handle);
DECL_DLL int kiwi_close(PKIWI handle);

DECL_DLL int kiwiResult_getSize(PKIWIRESULT result);
DECL_DLL float kiwiResult_getProb(PKIWIRESULT result, int index);
DECL_DLL int kiwiResult_getWordNum(PKIWIRESULT result, int index);
DECL_DLL const wchar_t* kiwiResult_getWordFormW(PKIWIRESULT result, int index, int num);
DECL_DLL const wchar_t* kiwiResult_getWordTagW(PKIWIRESULT result, int index, int num);
DECL_DLL const char* kiwiResult_getWordForm(PKIWIRESULT result, int index, int num);
DECL_DLL const char* kiwiResult_getWordTag(PKIWIRESULT result, int index, int num);
DECL_DLL int kiwiResult_getWordPosition(PKIWIRESULT result, int index, int num);
DECL_DLL int kiwiResult_getWordLength(PKIWIRESULT result, int index, int num);
DECL_DLL int kiwiResult_close(PKIWIRESULT result);

#ifdef __cplusplus  
}
#endif 