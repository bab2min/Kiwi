#pragma once

#include <string>
#include "KTrie.h"
#include "ThreadPool.h"
#include "KWordDetector.h"

struct KWordPair : public std::tuple<std::u16string, KPOSTag, uint16_t, uint32_t>
{
	using std::tuple<std::u16string, KPOSTag, uint16_t, uint32_t>::tuple;

	std::u16string& str() { return std::get<0>(*this); }
	const std::u16string& str() const { return std::get<0>(*this); }

	KPOSTag& tag() { return std::get<1>(*this); }
	const KPOSTag& tag() const { return std::get<1>(*this); }

	uint16_t& len() { return std::get<2>(*this); }
	const uint16_t& len() const { return std::get<2>(*this); }

	uint32_t& pos() { return std::get<3>(*this); }
	const uint32_t& pos() const { return std::get<3>(*this); }

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
	float cutOffThreshold = 10.f;
	std::unique_ptr<KModelMgr> mdl;
	mutable ThreadPool workers;
	const KTrie* kt = nullptr;
	KWordDetector detector;
	typedef std::vector<std::tuple<const KMorpheme*, k_string, uint32_t>> path;
	std::vector<std::pair<path, float>> findBestPath(const std::vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN) const;
public:
	Kiwi(const char* modelPath = "", size_t maxCache = -1, size_t numThread = 0);
	int addUserWord(const std::u16string& str, KPOSTag tag, float userScore = 20);
	int addUserRule(const std::u16string& str, const std::vector<std::pair<std::u16string, KPOSTag>>& morph);
	int loadUserDictionary(const char* userDictPath = "");
	int prepare();
	std::vector<KWordDetector::WordInfo> extractWords(const std::function<std::u16string(size_t)>& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25);
	std::vector<KWordDetector::WordInfo> extractAndAddWords(const std::function<std::u16string(size_t)>& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -2);
	KResult analyze(const std::u16string& str) const;
	KResult analyze(const std::string& str) const;
	std::vector<KResult> analyze(const std::u16string& str, size_t topN) const;
	std::vector<KResult> analyze(const std::string& str, size_t topN) const;
	void analyze(size_t topN, const std::function<std::u16string(size_t)>& reader, const std::function<void(size_t, std::vector<KResult>&&)>& receiver) const;
	void clearCache();
	static int getVersion();
};

