#pragma once

#include "KForm.h"
#include "KTrie.h"
#include "KModelMgr.h"

typedef pair<wstring, KPOSTag> KWordPair;
typedef pair<vector<KWordPair>, float> KResult;

class Kiwi
{
protected:
	size_t maxCache;
	shared_ptr<KModelMgr> mdl;
	const KTrie* kt = nullptr;
	mutable unordered_map<string, vector<KResult>> analyzedCache;
	static KPOSTag identifySpecialChr(wchar_t chr);
	static vector<vector<KWordPair>> splitPart(const wstring& str);
	static vector<const KChunk*> divideChunk(const vector<KChunk>& ch);
	vector<vector<pair<vector<char>, float>>> calcProbabilities(const KChunk* pre, const KChunk* begin, const KChunk* end, const char* ostr, size_t len) const;
	vector<KResult> analyzeJM(const string& jm, size_t topN, KPOSTag prefix, KPOSTag suffix) const;
public:
	Kiwi(const char* modelPath = "", size_t maxCache = -1);
	int loadUserDictionary(const char* userDictPath = "");
	int prepare();
	KResult analyze(const wstring& str) const;
	vector<KResult> analyze(const wstring& str, size_t topN) const;
	void clearCache();
};

