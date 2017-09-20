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
	mutable mutex lock;
	const KTrie* kt = nullptr;
	const k_vpcf* getOptimaPath(KMorphemeNode* node, size_t topN, KPOSTag prefix, KPOSTag suffix) const;
	mutable unordered_map<string, vector<KInterResult>> tempCache, freqCache;
	mutable unordered_map<string, size_t> cachePriority;
	static KPOSTag identifySpecialChr(k_wchar chr);
	static vector<vector<KWordPair>> splitPart(const k_wstring& str);
	static vector<const KChunk*> divideChunk(const k_vchunk& ch);
	vector<k_vpcf> calcProbabilities(const KChunk* pre, const KChunk* begin, const KChunk* end, const char* ostr, size_t len) const;
	vector<KInterResult> analyzeJM(const k_string& jm, size_t topN, KPOSTag prefix, KPOSTag suffix) const;
	vector<KInterResult> analyzeJM2(const k_string& jm, size_t topN, KPOSTag prefix, KPOSTag suffix) const;
	vector<KResult> analyzeOld(const k_wstring& str, size_t topN) const;
	vector<KResult> analyzeGM(const k_wstring& str, size_t topN) const;
	bool addCache(const string& jm, const vector<KInterResult>& value) const;
	vector<KInterResult>* findCache(const string& jm) const;
public:
	Kiwi(const char* modelPath = "", size_t maxCache = -1);
	int addUserWord(const k_wstring& str, KPOSTag tag);
	int addUserRule(const k_wstring& str, const vector<pair<k_wstring, KPOSTag>>& morph);
	int loadUserDictionary(const char* userDictPath = "");
	int prepare();
	KResult analyze(const k_wstring& str) const;
	KResult analyze(const string& str) const;
	vector<KResult> analyze(const k_wstring& str, size_t topN) const;
	vector<KResult> analyze(const string& str, size_t topN) const;
	vector<KResult> analyzeMT(const k_wstring& str, size_t topN, size_t pool) const;
	void clearCache();
	static int getVersion();
};

