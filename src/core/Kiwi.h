#pragma once

#include "KiwiHeader.h"
#include "KTrie.h"
#include "ThreadPool.h"
#include "KWordDetector.h"
#include "KModelMgr.h"
#include "PatternMatcher.h"

namespace kiwi
{
	struct KWordPair;
}

std::ostream& operator<< (std::ostream& os, const kiwi::KWordPair& kp);

namespace kiwi
{
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

		friend std::ostream& (::operator<<) (std::ostream& os, const KWordPair& kp);
	};

	typedef std::pair<std::vector<KWordPair>, float> KResult;

	class KModelMgr;

	class Kiwi
	{
	protected:
		float cutOffThreshold = 5.f;
		std::unique_ptr<KModelMgr> mdl;
		std::unique_ptr<ThreadPool> workers;
		size_t numThreads;
		bool integrateAllomorph;
		std::unique_ptr<PatternMatcher> pm;
		KWordDetector detector;
		typedef std::vector<std::tuple<const KMorpheme*, k_string, uint32_t>> path;
		std::vector<std::pair<path, float>> findBestPath(const std::vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN) const;
		std::vector<KResult> analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, size_t topN, size_t matchOptions) const;
	public:
		enum
		{
			LOAD_DEFAULT_DICT = 1,
			INTEGRATE_ALLOMORPH = 2,
		};
		Kiwi(const char* modelPath = "", size_t maxCache = -1, size_t numThreads = 0, size_t options = LOAD_DEFAULT_DICT | INTEGRATE_ALLOMORPH);
		int addUserWord(const std::u16string& str, KPOSTag tag, float userScore = 20);
		int loadUserDictionary(const char* userDictPath = "");
		int prepare();
		int getNumThreads() const { return numThreads; }
		void setCutOffThreshold(float _cutOffThreshold);
		int getOption(size_t option) const;
		void setOption(size_t option, int value);
		std::vector<KWordDetector::WordInfo> extractWords(const std::function<std::u16string(size_t)>& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25);
		std::vector<KWordDetector::WordInfo> filterExtractedWords(std::vector<KWordDetector::WordInfo>&& words, float posThreshold = -3) const;
		std::vector<KWordDetector::WordInfo> extractAddWords(const std::function<std::u16string(size_t)>& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3);
		KResult analyze(const std::u16string& str, size_t matchOptions) const;
		KResult analyze(const std::string& str, size_t matchOptions) const;
		std::vector<KResult> analyze(const std::u16string& str, size_t topN, size_t matchOptions) const;
		std::vector<KResult> analyze(const std::string& str, size_t topN, size_t matchOptions) const;
		std::future<std::vector<KResult>> asyncAnalyze(const std::string& str, size_t topN, size_t matchOptions) const;
		void analyze(size_t topN, const std::function<std::u16string(size_t)>& reader, const std::function<void(size_t, std::vector<KResult>&&)>& receiver, size_t matchOptions) const;
		void perform(size_t topN, const std::function<std::u16string(size_t)>& reader, const std::function<void(size_t, std::vector<KResult>&&)>& receiver, size_t matchOptions, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3) const;
		void clearCache();
		static const char* getVersion();
		static std::u16string toU16(const std::string& str);
		static std::string toU8(const std::u16string& str);
	};

}