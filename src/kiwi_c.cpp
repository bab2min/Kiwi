// KiwiDLL.cpp : Defines the exported functions for the DLL application.
//

#include <memory>
#include "core/Kiwi.h"

#include "kiwi_c.h"

using namespace std;
using namespace kiwi;

int kiwi_version()
{
	return Kiwi::getVersion();
}

struct ResultBuffer
{
	vector<string> stringBuf;
};

thread_local exception_ptr currentError;

const char* kiwi_error()
{
	if (currentError)
	{
		try
		{
			rethrow_exception(currentError);
		}
		catch (const exception& e)
		{
			return e.what();
		}
	}
	return nullptr;
}

PKIWI kiwi_init(const char * modelPath, int numThread, int options)
{
	try
	{
		return (PKIWI)new Kiwi{ modelPath, 0, (size_t)numThread, (size_t)options };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_addUserWord(PKIWI handle, const char * word, const char * pos)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		return kiwi->addUserWord(Kiwi::toU16(word), makePOSTag(Kiwi::toU16(pos)));
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_loadUserDict(PKIWI handle, const char * dictPath)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		kiwi->loadUserDictionary(dictPath);
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

PKIWIWORDS kiwi_extractWords(PKIWI handle, kiwi_reader reader, void * userData, int minCnt, int maxWordLen, float minScore)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

PKIWIWORDS kiwi_extractFilterWords(PKIWI handle, kiwi_reader reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

PKIWIWORDS kiwi_extractAddWords(PKIWI handle, kiwi_reader reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

PKIWIWORDS kiwi_extractWordsW(PKIWI handle, kiwi_readerW reader, void * userData, int minCnt, int maxWordLen, float minScore)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

PKIWIWORDS kiwi_extractFilterWordsW(PKIWI handle, kiwi_readerW reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

PKIWIWORDS kiwi_extractAddWordsW(PKIWI handle, kiwi_readerW reader, void * userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_prepare(PKIWI handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		kiwi->prepare();
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

PKIWIRESULT kiwi_analyzeW(PKIWI handle, const kchar16_t * text, int topN)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		auto result = new pair<vector<KResult>, ResultBuffer>{ kiwi->analyze((const char16_t*)text, topN), {} };
		return (PKIWIRESULT)result;
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

PKIWIRESULT kiwi_analyze(PKIWI handle, const char * text, int topN)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		auto result = new pair<vector<KResult>, ResultBuffer>{ kiwi->analyze(text, topN),{} };
		return (PKIWIRESULT)result;
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_analyzeMW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void * userData, int topN)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_analyzeM(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void * userData, int topN)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_performW(PKIWI handle, kiwi_readerW reader, kiwi_receiver receiver, void * userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_perform(PKIWI handle, kiwi_reader reader, kiwi_receiver receiver, void * userData, int topN, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
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
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_clearCache(PKIWI handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		kiwi->clearCache();
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_close(PKIWI handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		delete kiwi;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwiResult_getSize(PKIWIRESULT result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		return k->first.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwiResult_getProb(PKIWIRESULT result, int index)
{
	if (!result) return 0;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size()) return 0;
		return k->first[index].second;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwiResult_getWordNum(PKIWIRESULT result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size()) return KIWIERR_INVALID_INDEX;
		return k->first[index].first.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

const kchar16_t * kiwiResult_getWordFormW(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
		return (const kchar16_t*)k->first[index].first[num].str().c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const kchar16_t * kiwiResult_getWordTagW(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
		return (const kchar16_t*)tagToStringW(k->first[index].first[num].tag());
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const char * kiwiResult_getWordForm(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
		k->second.stringBuf.emplace_back(Kiwi::toU8(k->first[index].first[num].str()));
		return k->second.stringBuf.back().c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const char * kiwiResult_getWordTag(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
		return tagToString(k->first[index].first[num].tag());
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwiResult_getWordPosition(PKIWIRESULT result, int index, int num)
{
	if (!result) return -1;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return -1;
		return k->first[index].first[num].pos();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwiResult_getWordLength(PKIWIRESULT result, int index, int num)
{
	if (!result) return -1;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return -1;
		return k->first[index].first[num].len();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwiResult_close(PKIWIRESULT result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		auto k = (pair<vector<KResult>, ResultBuffer>*)result;
		delete k;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwiWords_getSize(PKIWIWORDS result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
		return k->first.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

const kchar16_t * kiwiWords_getWordFormW(PKIWIWORDS result, int index)
{
	if (!result) return nullptr;
	try
	{
		auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size()) return nullptr;
		return (const kchar16_t*)k->first[index].form.c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const char * kiwiWords_getWordForm(PKIWIWORDS result, int index)
{
	if (!result) return nullptr;
	try
	{
		auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size()) return nullptr;
		k->second.stringBuf.emplace_back(Kiwi::toU8(k->first[index].form));
		return k->second.stringBuf.back().c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

float kiwiWords_getScore(PKIWIWORDS result, int index)
{
	if (!result) return NAN;
	try
	{
		auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size()) return NAN;
		return k->first[index].score;
	}
	catch (...)
	{
		currentError = current_exception();
		return NAN;
	}
}

int kiwiWords_getFreq(PKIWIWORDS result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size()) return KIWIERR_INVALID_INDEX;
		return k->first[index].freq;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwiWords_getPosScore(PKIWIWORDS result, int index)
{
	if (!result) return NAN;
	try
	{
		auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
		if (index < 0 || index >= k->first.size()) return NAN;
		return k->first[index].posScore[KPOSTag::NNP];
	}
	catch (...)
	{
		currentError = current_exception();
		return NAN;
	}
}

int kiwiWords_close(PKIWIWORDS result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		auto k = (pair<vector<KWordDetector::WordInfo>, ResultBuffer>*)result;
		delete k;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

