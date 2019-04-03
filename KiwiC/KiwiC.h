#pragma once

#define KIWIERR_INVALID_HANDLE -1
#define KIWIERR_INVALID_INDEX -2

#define DECL_DLL __declspec(dllexport)

typedef struct KIWI* PKIWI;
typedef struct KIWIRESULT* PKIWIRESULT;
typedef struct KIWIWORDS* PKIWIWORDS;
typedef unsigned short kchar16_t;

/*
int (*kiwi_reader)(int id, char* buffer, void* userData)
id: id number of line to be read. if id == 0, kiwi_reader should roll back file and read lines from the begining
buffer: buffer where string data should be stored. if buffer == null, kiwi_reader provide the length of string as return value.
userData: userData from kiwi_extract~, kiwi_perform, kiwi_analyzeM functions.
*/
typedef int(*kiwi_reader)(int, char*, void*);
typedef int(*kiwi_readerW)(int, kchar16_t*, void*);


typedef int(*kiwi_receiver)(int, PKIWIRESULT, void*);

enum
{
	KIWI_LOAD_DEFAULT_DICT = 1
};

#ifdef __cplusplus  
extern "C" {
#endif 

DECL_DLL int kiwi_version();
DECL_DLL const char* kiwi_error();

DECL_DLL PKIWI kiwi_init(const char* modelPath, int numThread, int options);
DECL_DLL int kiwi_addUserWord(PKIWI handle, const char* word, const char* pos);
DECL_DLL int kiwi_loadUserDict(PKIWI handle, const char* dictPath);
DECL_DLL PKIWIWORDS kiwi_extractWords(PKIWI handle, kiwi_reader reader, void* userData, int minCnt, int maxWordLen, float minScore);
DECL_DLL PKIWIWORDS kiwi_extractFilterWords(PKIWI handle, kiwi_reader reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
DECL_DLL PKIWIWORDS kiwi_extractAddWords(PKIWI handle, kiwi_reader reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
DECL_DLL PKIWIWORDS kiwi_extractWordsW(PKIWI handle, kiwi_readerW reader, void* userData, int minCnt, int maxWordLen, float minScore);
DECL_DLL PKIWIWORDS kiwi_extractFilterWordsW(PKIWI handle, kiwi_readerW reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
DECL_DLL PKIWIWORDS kiwi_extractAddWordsW(PKIWI handle, kiwi_readerW reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold);
DECL_DLL int kiwi_prepare(PKIWI handle);
DECL_DLL PKIWIRESULT kiwi_analyzeW(PKIWI handle, const kchar16_t* text, int topN);
DECL_DLL PKIWIRESULT kiwi_analyze(PKIWI handle, const char* text, int topN);
DECL_DLL int kiwi_analyzeMW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void* userData, int topN);
DECL_DLL int kiwi_analyzeM(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void* userData, int topN);
DECL_DLL int kiwi_performW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void* userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold);
DECL_DLL int kiwi_perform(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void* userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold);
DECL_DLL int kiwi_clearCache(PKIWI handle);
DECL_DLL int kiwi_close(PKIWI handle);

DECL_DLL int kiwiResult_getSize(PKIWIRESULT result);
DECL_DLL float kiwiResult_getProb(PKIWIRESULT result, int index);
DECL_DLL int kiwiResult_getWordNum(PKIWIRESULT result, int index);
DECL_DLL const kchar16_t* kiwiResult_getWordFormW(PKIWIRESULT result, int index, int num);
DECL_DLL const kchar16_t* kiwiResult_getWordTagW(PKIWIRESULT result, int index, int num);
DECL_DLL const char* kiwiResult_getWordForm(PKIWIRESULT result, int index, int num);
DECL_DLL const char* kiwiResult_getWordTag(PKIWIRESULT result, int index, int num);
DECL_DLL int kiwiResult_getWordPosition(PKIWIRESULT result, int index, int num);
DECL_DLL int kiwiResult_getWordLength(PKIWIRESULT result, int index, int num);
DECL_DLL int kiwiResult_close(PKIWIRESULT result);

DECL_DLL int kiwiWords_getSize(PKIWIWORDS result);
DECL_DLL const kchar16_t* kiwiWords_getWordFormW(PKIWIWORDS result, int index);
DECL_DLL const char* kiwiWords_getWordForm(PKIWIWORDS result, int index);
DECL_DLL float kiwiWords_getScore(PKIWIWORDS result, int index);
DECL_DLL int kiwiWords_getFreq(PKIWIWORDS result, int index);
DECL_DLL float kiwiWords_getPosScore(PKIWIWORDS result, int index);
DECL_DLL int kiwiWords_close(PKIWIWORDS result);

#ifdef __cplusplus  
}
#endif 