#pragma once

#include <kiwi/Types.h>
#include "KTrie.h"
#include "ThreadPool.h"
#include "KWordDetector.h"
#include "KModelMgr.h"
#include <kiwi/PatternMatcher.h>
#include <kiwi/Knlm.h>

std::ostream& operator<< (std::ostream& os, const kiwi::TokenInfo& kp);

namespace kiwi
{
	using KResult = std::pair<std::vector<TokenInfo>, float>;

	class KModelMgr;

	class Kiwi
	{
	protected:
		float cutOffThreshold = 5.f;
		std::unique_ptr<KModelMgr> mdl;
		std::unique_ptr<utils::ThreadPool> workers;
		size_t numThreads;
		bool integrateAllomorph;
		std::unique_ptr<PatternMatcher> pm;
		KWordDetector detector;
		using Path = Vector<std::tuple<const Morpheme*, KString, uint32_t>>;
		Vector<std::pair<Path, float>> findBestPath(const Vector<KGraphNode>& graph, const KNLangModel * knlm, const Morpheme* morphBase, size_t topN) const;
		std::vector<KResult> analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, size_t topN, Match matchOptions) const;
	public:
		enum
		{
			LOAD_DEFAULT_DICT = 1,
			INTEGRATE_ALLOMORPH = 2,
		};
		Kiwi(const char* modelPath = "", size_t maxCache = -1, size_t numThreads = 0, size_t options = LOAD_DEFAULT_DICT | INTEGRATE_ALLOMORPH);
		int addUserWord(const std::u16string& str, POSTag tag, float userScore = 20);
		int loadUserDictionary(const char* userDictPath = "");
		int prepare();
		int getNumThreads() const { return numThreads; }
		void setCutOffThreshold(float _cutOffThreshold);
		int getOption(size_t option) const;
		void setOption(size_t option, int value);
		std::vector<KWordDetector::WordInfo> extractWords(const U16MultipleReader& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25);
		std::vector<KWordDetector::WordInfo> filterExtractedWords(std::vector<KWordDetector::WordInfo>&& words, float posThreshold = -3) const;
		std::vector<KWordDetector::WordInfo> extractAddWords(const U16MultipleReader& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3);
		KResult analyze(const std::u16string& str, Match matchOptions) const;
		KResult analyze(const std::string& str, Match matchOptions) const;
		std::vector<KResult> analyze(const std::u16string& str, size_t topN, Match matchOptions) const;
		std::vector<KResult> analyze(const std::string& str, size_t topN, Match matchOptions) const;
		std::future<std::vector<KResult>> asyncAnalyze(const std::string& str, size_t topN, Match matchOptions) const;
		void analyze(size_t topN, const U16Reader& reader, const std::function<void(size_t, std::vector<KResult>&&)>& receiver, Match matchOptions) const;
		void perform(size_t topN, const U16MultipleReader& reader, const std::function<void(size_t, std::vector<KResult>&&)>& receiver, Match matchOptions, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3) const;
		void clearCache();

		const Morpheme* getMorphs() const;
		size_t getNumMorphs() const;
		std::u16string getFormOfMorph(size_t id) const;

		static const char* getVersion();
		static std::u16string toU16(const std::string& str);
		static std::string toU8(const std::u16string& str);
	};

}