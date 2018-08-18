#pragma once

#include "KForm.h"
#include "KTrie.h"
#include "ThreadPool.h"
/*
struct KWordPair : public std::tuple<k_wstring, KPOSTag, uint8_t, uint16_t>
{
	using std::tuple<k_wstring, KPOSTag, uint8_t, uint16_t>::tuple;

	k_wstring& str() { return std::get<0>(*this); }
	const k_wstring& str() const { return std::get<0>(*this); }

	KPOSTag& tag() { return std::get<1>(*this); }
	const KPOSTag& tag() const { return std::get<1>(*this); }

	uint8_t& len() { return std::get<2>(*this); }
	const uint8_t& len() const { return std::get<2>(*this); }

	uint16_t& pos() { return std::get<3>(*this); }
	const uint16_t& pos() const { return std::get<3>(*this); }

	bool operator ==(const KWordPair& o) const
	{
		return str() == o.str() && tag() == o.tag();
	}

	bool operator !=(const KWordPair& o) const
	{
		return !operator ==(o);
	}
};

typedef std::pair<std::vector<KWordPair>, float> KResult;

struct KInterWordPair : std::tuple<const KMorpheme*, k_wstring, KPOSTag, uint8_t, uint16_t>
{
	using std::tuple<const KMorpheme*, k_wstring, KPOSTag, uint8_t, uint16_t>::tuple;

	const KMorpheme*& morph() { return std::get<0>(*this); }
	const KMorpheme* const& morph() const { return std::get<0>(*this); }

	k_wstring& str() { return std::get<1>(*this); }
	const k_wstring& str() const { return std::get<1>(*this); }

	KPOSTag& tag() { return std::get<2>(*this); }
	const KPOSTag& tag() const { return std::get<2>(*this); }

	uint8_t& len() { return std::get<3>(*this); }
	const uint8_t& len() const { return std::get<3>(*this); }

	uint16_t& pos() { return std::get<4>(*this); }
	const uint16_t& pos() const { return std::get<4>(*this); }
};

typedef std::pair<std::vector<KInterWordPair>, float> KInterResult;

class KModelMgr;

class Kiwi
{
protected:
	size_t maxCache;
	std::shared_ptr<KModelMgr> mdl;
	mutable ThreadPool threadPool;
	mutable std::mutex lock;
	const KTrie* kt = nullptr;
	size_t numThread;
	const k_vpcf* getOptimalPath(KMorphemeNode* node, size_t topN, KPOSTag prefix, KPOSTag suffix) const;
	mutable std::unordered_map<std::string, std::vector<KInterResult>> tempCache, freqCache;
	mutable std::unordered_map<std::string, size_t> cachePriority;
	static KPOSTag identifySpecialChr(k_wchar chr);
	static std::vector<std::vector<KWordPair>> splitPart(const k_wstring& str);
	static std::vector<const KChunk*> divideChunk(const k_vchunk& ch);
	std::vector<k_vpcf> calcProbabilities(const KChunk* pre, const KChunk* begin, const KChunk* end, const char* ostr, size_t len) const;
	std::vector<KInterResult> analyzeJM(const k_string& jm, size_t topN, KPOSTag prefix, KPOSTag suffix, uint8_t len = 0, uint16_t pos = 0) const;
	std::vector<KResult> analyzeGM(const k_wstring& str, size_t topN) const;
	bool addCache(const std::string& jm, const std::vector<KInterResult>& value) const;
	std::vector<KInterResult>* findCache(const std::string& jm) const;
public:
	Kiwi(const char* modelPath = "", size_t maxCache = -1, size_t numThread = 0);
	int addUserWord(const k_wstring& str, KPOSTag tag);
	int addUserRule(const k_wstring& str, const std::vector<std::pair<k_wstring, KPOSTag>>& morph);
	int loadUserDictionary(const char* userDictPath = "");
	int prepare();
	KResult analyze(const k_wstring& str) const;
	KResult analyze(const std::string& str) const;
	std::vector<KResult> analyze(const k_wstring& str, size_t topN) const;
	std::vector<KResult> analyze(const std::string& str, size_t topN) const;
	std::vector<KResult> analyzeMT(const k_wstring& str, size_t topN) const;
	void clearCache();
	static int getVersion();
};

*/