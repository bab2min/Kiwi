// KiwiDLL.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Kiwi.h"

#include "KiwiC.h"

using namespace std;

int kiwi_version()
{
	return Kiwi::getVersion();
}

struct ResultBuffer
{
	vector<string> stringBuf;
};

static exception currentError;

const char* kiwi_error()
{
	return currentError.what();
}

PKIWI kiwi_init(const char * modelPath, int numThread)
{
	try
	{
		return (PKIWI)new Kiwi{ modelPath, 0, (size_t)numThread };
	}
	catch (const exception& e)
	{
		currentError = e;
		return nullptr;
	}
}

int kiwi_addUserWord(PKIWI handle, const char * word, const char * pos)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	return kiwi->addUserWord(Kiwi::toU16(word), makePOSTag(Kiwi::toU16(pos)));
}

int kiwi_loadUserDict(PKIWI handle, const char * dictPath)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->loadUserDictionary(dictPath);
	return 0;
}

PKIWIWORDS kiwi_extractWords(PKIWI handle, kiwi_reader reader, void * userData, int minCnt, int maxWordLen, float minScore)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto res = kiwi->extractWords([=](size_t id)->u16string
	{
		string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, &buf[0], userData);
		return Kiwi::toU16(buf);
	}, minCnt, maxWordLen, minScore);
	
	return (PKIWIWORDS)new pair<vector<KWordDetector::WordInfo>, ResultBuffer>{ move(res), {} };
}

PKIWIWORDS kiwi_extractFilterWords(PKIWI handle, kiwi_reader reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto res = kiwi->extractWords([=](size_t id)->u16string
	{
		string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, &buf[0], userData);
		return Kiwi::toU16(buf);
	}, minCnt, maxWordLen, minScore);
	res = kiwi->filterExtractedWords(move(res), posThreshold);
	return (PKIWIWORDS)new pair<vector<KWordDetector::WordInfo>, ResultBuffer>{ move(res),{} };
}

PKIWIWORDS kiwi_extractAddWords(PKIWI handle, kiwi_reader reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto res = kiwi->extractAddWords([=](size_t id)->u16string
	{
		string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, &buf[0], userData);
		return Kiwi::toU16(buf);
	}, minCnt, maxWordLen, minScore, posThreshold);
	return (PKIWIWORDS)new pair<vector<KWordDetector::WordInfo>, ResultBuffer>{ move(res),{} };
}

PKIWIWORDS kiwi_extractWordsW(PKIWI handle, kiwi_readerW reader, void * userData, int minCnt, int maxWordLen, float minScore)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto res = kiwi->extractWords([=](size_t id)->u16string
	{
		u16string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, (kchar16_t*)&buf[0], userData);
		return buf;
	}, minCnt, maxWordLen, minScore);

	return (PKIWIWORDS)new pair<vector<KWordDetector::WordInfo>, ResultBuffer>{ move(res),{} };
}

PKIWIWORDS kiwi_extractFilterWordsW(PKIWI handle, kiwi_readerW reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto res = kiwi->extractWords([=](size_t id)->u16string
	{
		u16string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, (kchar16_t*)&buf[0], userData);
		return buf;
	}, minCnt, maxWordLen, minScore);
	res = kiwi->filterExtractedWords(move(res), posThreshold);
	return (PKIWIWORDS)new pair<vector<KWordDetector::WordInfo>, ResultBuffer>{ move(res),{} };
}

PKIWIWORDS kiwi_extractAddWordsW(PKIWI handle, kiwi_readerW reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto res = kiwi->extractAddWords([=](size_t id)->u16string
	{
		u16string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, (kchar16_t*)&buf[0], userData);
		return buf;
	}, minCnt, maxWordLen, minScore, posThreshold);
	return (PKIWIWORDS)new pair<vector<KWordDetector::WordInfo>, ResultBuffer>{ move(res),{} };
}

int kiwi_prepare(PKIWI handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->prepare();
	return 0;
}

PKIWIRESULT kiwi_analyzeW(PKIWI handle, const kchar16_t * text, int topN)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto result = new pair<vector<KResult>, ResultBuffer>{ kiwi->analyze((const char16_t*)text, topN), {} };
	return (PKIWIRESULT)result;
}

PKIWIRESULT kiwi_analyze(PKIWI handle, const char * text, int topN)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto result = new pair<vector<KResult>, ResultBuffer>{ kiwi->analyze(text, topN),{} };
	return (PKIWIRESULT)result;
}

int kiwi_analyzeMW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void * userData, int topN)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->analyze(topN, [=](size_t id)->u16string
	{
		u16string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, (kchar16_t*)&buf[0], userData);
		return buf;
	}, [=](size_t id, vector<KResult>&& res) 
	{
		auto result = new pair<vector<KResult>, ResultBuffer>{ res, {} };
		(*receiver)(id, (PKIWIRESULT)result, userData);
	});
	return 0;
}

int kiwi_analyzeM(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void * userData, int topN)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->analyze(topN, [=](size_t id)->u16string
	{
		string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, &buf[0], userData);
		return Kiwi::toU16(buf);
	}, [=](size_t id, vector<KResult>&& res)
	{
		auto result = new pair<vector<KResult>, ResultBuffer>{ res,{} };
		(*receiver)(id, (PKIWIRESULT)result, userData);
	});
	return 0;
}

int kiwi_performW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void * userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->perform(topN, [=](size_t id)->u16string
	{
		u16string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, (kchar16_t*)&buf[0], userData);
		return buf;
	}, [=](size_t id, vector<KResult>&& res)
	{
		auto result = new pair<vector<KResult>, ResultBuffer>{ res,{} };
		(*receiver)(id, (PKIWIRESULT)result, userData);
	}, minCnt, maxWordLen, minScore, posThreshold);
	return 0;
}

int kiwi_perform(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void * userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->perform(topN, [=](size_t id)->u16string
	{
		string buf;
		buf.resize((*reader)(id, nullptr, userData));
		if (buf.empty()) return {};
		(*reader)(id, &buf[0], userData);
		return Kiwi::toU16(buf);
	}, [=](size_t id, vector<KResult>&& res)
	{
		auto result = new pair<vector<KResult>, ResultBuffer>{ res,{} };
		(*receiver)(id, (PKIWIRESULT)result, userData);
	}, minCnt, maxWordLen, minScore, posThreshold);
	return 0;
}

int kiwi_clearCache(PKIWI handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->clearCache();
	return 0;
}

int kiwi_close(PKIWI handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	delete kiwi;
	return 0;
}

int kiwiResult_getSize(PKIWIRESULT result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	return k->first.size();
}

float kiwiResult_getProb(PKIWIRESULT result, int index)
{
	if (!result) return 0;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size()) return 0;
	return k->first[index].second;
}

int kiwiResult_getWordNum(PKIWIRESULT result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size()) return KIWIERR_INVALID_INDEX;
	return k->first[index].first.size();
}

const kchar16_t * kiwiResult_getWordFormW(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
	return (const kchar16_t*)k->first[index].first[num].str().c_str();
}

const kchar16_t * kiwiResult_getWordTagW(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
	return (const kchar16_t*)tagToStringW(k->first[index].first[num].tag());
}

const char * kiwiResult_getWordForm(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
	k->second.stringBuf.emplace_back(Kiwi::toU8(k->first[index].first[num].str()));
	return k->second.stringBuf.back().c_str();
}

const char * kiwiResult_getWordTag(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
	return tagToString(k->first[index].first[num].tag());
}

int kiwiResult_getWordPosition(PKIWIRESULT result, int index, int num)
{
	if (!result) return -1;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return -1;
	return k->first[index].first[num].pos();
}

int kiwiResult_getWordLength(PKIWIRESULT result, int index, int num)
{
	if (!result) return -1;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return -1;
	return k->first[index].first[num].len();
}

int kiwiResult_close(PKIWIRESULT result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	delete k;
	return 0;
}

int kiwiWords_getSize(PKIWIWORDS result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
	return k->first.size();
}

const kchar16_t * kiwiWords_getWordFormW(PKIWIWORDS result, int index)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size()) return nullptr;
	return (const kchar16_t*)k->first[index].form.c_str();
}

const char * kiwiWords_getWordForm(PKIWIWORDS result, int index)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size()) return nullptr;
	k->second.stringBuf.emplace_back(Kiwi::toU8(k->first[index].form));
	return k->second.stringBuf.back().c_str();
}

float kiwiWords_getScore(PKIWIWORDS result, int index)
{
	if (!result) return NAN;
	auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size()) return NAN;
	return k->first[index].score;
}

int kiwiWords_getFreq(PKIWIWORDS result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size()) return KIWIERR_INVALID_INDEX;
	return k->first[index].freq;
}

float kiwiWords_getPosScore(PKIWIWORDS result, int index)
{
	if (!result) return NAN;
	auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size()) return NAN;
	return k->first[index].posScore[KPOSTag::NNP];
}

int kiwiWords_close(PKIWIWORDS result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
	delete k;
	return 0;
}

