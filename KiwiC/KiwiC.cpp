// KiwiDLL.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <wchar.h>

#include "../KiwiLibrary/KiwiHeader.h"
#include "../KiwiLibrary/Kiwi.h"

#include "KiwiC.h"

int kiwi_version()
{
	return Kiwi::getVersion();
}

exception currentError;

const char* kiwi_error()
{
	return currentError.what();
}

PKIWI kiwi_init(const char * modelPath, int maxCache, int numThread)
{
	try
	{
		return new Kiwi{ modelPath, (size_t)maxCache, (size_t)numThread };
	}
	catch (const exception& e)
	{
		currentError = e;
		return nullptr;
	}
}

int kiwi_loadUserDict(PKIWI handle, const char * dictPath)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->loadUserDictionary(dictPath);
	return 0;
}

int kiwi_prepare(PKIWI handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	kiwi->prepare();
	return 0;
}

struct ResultBuffer
{
	vector<string> stringBuf;
};

PKIWIRESULT kiwi_analyzeW(PKIWI handle, const wchar_t * text, int topN)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto result = new pair<vector<KResult>, ResultBuffer>{ kiwi->analyze(text, topN), {} };
	return result;
}

PKIWIRESULT kiwi_analyze(PKIWI handle, const char * text, int topN)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	auto result = new pair<vector<KResult>, ResultBuffer>{ kiwi->analyze(text, topN),{} };
	return result;
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

const wchar_t * kiwiResult_getWordFormW(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
	return k->first[index].first[num].str().c_str();
}

const wchar_t * kiwiResult_getWordTagW(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
	return tagToStringW(k->first[index].first[num].tag());
}

const char * kiwiResult_getWordForm(PKIWIRESULT result, int index, int num)
{
	if (!result) return nullptr;
	auto k = (pair<vector<KResult>, ResultBuffer>*)result;
	if (index < 0 || index >= k->first.size() || num < 0 || num >= k->first[index].first.size()) return nullptr;
	wstring_convert<codecvt_utf8_utf16<k_wchar>> converter;
	k->second.stringBuf.emplace_back(converter.to_bytes(k->first[index].first[num].str()));
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

