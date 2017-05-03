#pragma once

#include "KForm.h"
#include "KTrie.h"


typedef pair<k_wstring, KPOSTag> KWordPair;
typedef pair<vector<KWordPair>, float> KResult;

typedef pair<vector<tuple<const KMorpheme*, k_wstring, KPOSTag>>, float> KInterResult;

class KModelMgr;

class Kiwi
{
protected:
	size_t maxCache;
	shared_ptr<KModelMgr> mdl;
	const KTrie* kt = nullptr;
	mutable unordered_map<k_string, vector<KInterResult>> analyzedCache;
	static KPOSTag identifySpecialChr(k_wchar chr);
	static vector<vector<KWordPair>> splitPart(const k_wstring& str);
	static vector<const KChunk*> divideChunk(const vector<KChunk>& ch);
	vector<k_vpcf> calcProbabilities(const KChunk* pre, const KChunk* begin, const KChunk* end, const char* ostr, size_t len) const;
	vector<KInterResult> analyzeJM(const k_string& jm, size_t topN, KPOSTag prefix, KPOSTag suffix) const;
public:
	Kiwi(const char* modelPath = "", size_t maxCache = -1);
	int addUserWord(const k_wstring& str, KPOSTag tag);
	int addUserRule(const k_wstring& str, const vector<pair<k_wstring, KPOSTag>>& morph);
	int loadUserDictionary(const char* userDictPath = "");
	int prepare();
	KResult analyze(const k_wstring& str) const;
	vector<KResult> analyze(const k_wstring& str, size_t topN) const;
	void clearCache();
};

