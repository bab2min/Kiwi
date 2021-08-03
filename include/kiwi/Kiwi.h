#pragma once

#include <iostream>
#include <future>
#include "Types.h"
#include "Form.h"
#include "Utils.h"
#include "Trainer.h"
#include "Trie.hpp"
#include "PatternMatcher.h"
#include "FrozenTrie.h"
#include "Knlm.h"
#include "ThreadPool.h"
#include "WordDetector.h"

namespace kiwi
{
	struct KTrie;
	struct KGraphNode;
	struct WordInfo;

	class Kiwi
	{
		friend class KiwiBuilder;
		friend class PathEvaluator;

		bool integrateAllomorph = true;
		float cutOffThreshold = 5;

		std::vector<Form> forms;
		std::vector<Morpheme> morphemes;
		utils::FrozenTrie<kchar_t, const Form*> formTrie;
		const PatternMatcher* pm = nullptr;
		std::shared_ptr<lm::KnLangModelBase> langMdl;
		std::unique_ptr<utils::ThreadPool> pool;
			
		std::vector<TokenResult> analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, size_t topN, Match matchOptions) const;

		const Morpheme* getDefaultMorpheme(POSTag tag) const;

	public:
		Kiwi() = default;
		Kiwi(Kiwi&&) = default;
		Kiwi& operator=(Kiwi&&) = default;

		bool ready() const { return !forms.empty(); }

		TokenResult analyze(const std::u16string& str, Match matchOptions) const
		{
			return analyze(str, 1, matchOptions)[0];
		}
		TokenResult analyze(const std::string& str, Match matchOptions) const
		{
			return analyze(utf8To16(str), matchOptions);
		}

		std::vector<TokenResult> analyze(const std::u16string& str, size_t topN, Match matchOptions) const;
		std::vector<TokenResult> analyze(const std::string& str, size_t topN, Match matchOptions) const
		{
			return analyze(utf8To16(str), topN, matchOptions);
		}

		std::future<std::vector<TokenResult>> asyncAnalyze(const std::string& str, size_t topN, Match matchOptions) const;

		template<class ReaderCallback, class ResultCallback>
		void analyze(size_t topN, ReaderCallback&& reader, ResultCallback&& resultCallback, Match matchOptions) const
		{
			if (pool)
			{
				bool stop = false;
				std::deque<std::future<std::vector<TokenResult>>> futures;
				for (size_t i = 0; i < pool->size() * 2; ++i)
				{
					auto ustr = reader();
					if (ustr.empty())
					{
						stop = true;
						break;
					}
					futures.emplace_back(pool->enqueue([&, ustr](size_t tid)
					{
						return analyze(ustr, topN, matchOptions);
					}));
				}

				while (!futures.empty())
				{
					resultCallback(futures.front().get());
					futures.pop_front();
					if (!stop)
					{
						auto ustr = reader();
						if (ustr.empty())
						{
							stop = true;
							continue;
						}
						futures.emplace_back(pool->enqueue([&, ustr](size_t tid)
						{
							return analyze(ustr, topN, matchOptions);
						}));
					}
				}
			}
			else
			{
				while(1)
				{
					auto ustr = reader();
					if (ustr.empty()) break;
					resultCallback(analyze(ustr, topN, matchOptions));
				}
			}
		}

		size_t morphToId(const Morpheme* morph) const
		{
			if (!morph || morph < morphemes.data()) return -1;
			return morph - morphemes.data();
		}

		const Morpheme* idToMorph(size_t morphId) const
		{
			if (morphId >= morphemes.size()) return nullptr;
			return &morphemes[morphId];
		}

		size_t getNumThreads() const
		{
			return pool ? 1 : pool->size();
		}

		float getCutOffThreshold() const
		{
			return cutOffThreshold;
		}

		void setCutOffThreshold(float v)
		{
			cutOffThreshold = v;
		}

		bool getIntegrateAllomorph() const
		{
			return integrateAllomorph;
		}

		void setIntegrateAllomorph(bool v)
		{
			integrateAllomorph = v;
		}
	};

	class KiwiBuilder
	{
		std::vector<FormRaw> forms;
		std::vector<MorphemeRaw> morphemes;
		std::unordered_map<FormCond, size_t> formMap;
		std::shared_ptr<lm::KnLangModelBase> langMdl;
		size_t numThreads = 0;
		WordDetector detector;
		BuildOption options = BuildOption::none;

		void loadMorphBin(std::istream& is);
		void saveMorphBin(std::ostream& os) const;
		FormRaw& addForm(KString form, CondVowel vowel, CondPolarity polar);

		using MorphemeMap = std::unordered_map<std::pair<KString, POSTag>, size_t>;
		void loadMMFromTxt(std::istream&& is, MorphemeMap& morphMap, std::unordered_map<POSTag, float>* posWeightSum, const std::function<bool(float, POSTag)>& selector);
		void loadCMFromTxt(std::istream&& is, MorphemeMap& morphMap);
		void loadPCMFromTxt(std::istream&& is, MorphemeMap& morphMap);
		void addCorpusTo(Vector<Vector<uint16_t>>& out, std::istream&& is, MorphemeMap& morphMap);
		void updateForms();
	public:
		struct FromRawData {};
		static constexpr FromRawData fromRawDataTag = {};

		KiwiBuilder() = default;
		KiwiBuilder(FromRawData, const std::string& rawDataPath, size_t numThreads = 0);
		KiwiBuilder(const std::string& modelPath, size_t numThreads = 0, BuildOption options = BuildOption::integrateAllomorph | BuildOption::loadDefaultDict);

		bool ready() const
		{
			return !!langMdl;
		}

		void saveModel(const std::string& modelPath) const;

		bool addWord(const std::u16string& str, POSTag tag = POSTag::nnp, float score = 0);

		size_t loadDictionary(const std::string& dictPath);

		std::vector<WordInfo> extractWords(const U16MultipleReader& reader, 
			size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3, bool lmFilter = true
		) const;

		std::vector<WordInfo> extractAddWords(const U16MultipleReader& reader, 
			size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3, bool lmFilter = true
		);

		Kiwi build() const;
	};
}
