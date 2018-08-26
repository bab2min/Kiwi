#pragma once

#include <string>
#include "KTrie.h"
#include "ThreadPool.h"

struct KWordPair : public std::tuple<std::u16string, KPOSTag, uint8_t, uint16_t>
{
	using std::tuple<std::u16string, KPOSTag, uint8_t, uint16_t>::tuple;

	std::u16string& str() { return std::get<0>(*this); }
	const std::u16string& str() const { return std::get<0>(*this); }

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
	
	friend std::ostream& operator << (std::ostream& os, const KWordPair& kp);
};

typedef std::pair<std::vector<KWordPair>, float> KResult;

class KModelMgr;

class Kiwi
{
protected:
	float cutOffThreshold = 15.f;
	size_t maxCache;
	std::unique_ptr<KModelMgr> mdl;
	//mutable ThreadPool workerPool;
	mutable std::vector<ReusableThread> workers;
	mutable std::mutex lock;
	const KTrie* kt = nullptr;
	size_t numThread;
	typedef std::vector<std::pair<const KMorpheme*, k_string>> pathType;
	std::vector<std::pair<pathType, float>> findBestPath(const std::vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN) const;
public:
	Kiwi(const char* modelPath = "", size_t maxCache = -1, size_t numThread = 0);
	int addUserWord(const std::u16string& str, KPOSTag tag, float userScore = 10);
	int addUserRule(const std::u16string& str, const std::vector<std::pair<std::u16string, KPOSTag>>& morph);
	int loadUserDictionary(const char* userDictPath = "");
	int prepare();
	KResult analyze(const std::u16string& str) const;
	KResult analyze(const std::string& str) const;
	std::vector<KResult> analyze(const std::u16string& str, size_t topN) const;
	std::vector<KResult> analyze(const std::string& str, size_t topN) const;
	void clearCache();
	static int getVersion();
};

