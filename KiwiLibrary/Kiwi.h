#pragma once

#include "KForm.h"
#include "KTrie.h"
#include "KModelMgr.h"

typedef pair<wstring, KPOSTag> KWordPair;
typedef pair<vector<KWordPair>, float> KResult;

class Kiwi
{
protected:
	shared_ptr<KModelMgr> mdl;
	shared_ptr<KTrie> kt;
	mutable unordered_map<string, vector<KResult>> analyzeCache;
	static KPOSTag identifySpecialChr(wchar_t chr);
	static vector<vector<KWordPair>> splitPart(const wstring& str);
	static vector<const KChunk*> divideChunk(const vector<KChunk>& ch);
	vector<vector<pair<vector<char>, float>>> calcProbabilities(const KChunk* pre, const KChunk* begin, const KChunk* end, const char* ostr, size_t len) const;
	vector<KResult> analyzeJM(const string& jm, size_t topN, KPOSTag prefix, KPOSTag suffix) const;
public:
	Kiwi(const char* modelPath = "");
	int loadUserDictionary(const char* userDictPath = "");
	int prepare();
	KResult analyze(const wstring& str) const;
	vector<KResult> analyze(const wstring& str, size_t topN) const;
};

