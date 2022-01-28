/**
 * @file Kiwi.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kiwi C++ API를 담고 있는 헤더 파일
 * @version 0.10.0
 * @date 2021-08-31
 * 
 * 
 */
#pragma once

#include <iostream>
#include <future>
#include <string>
#include "Macro.h"
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

	inline uint32_t getDefaultMorphemeId(POSTag tag)
	{
		return (uint32_t)tag + 1;
	}

	/**
	 * @brief 실제 형태소 분석을 수행하는 클래스.
	 * 
	 */
	class Kiwi
	{
		friend class KiwiBuilder;
		friend class PathEvaluator;

		bool integrateAllomorph = true;
		float cutOffThreshold = 5;

		std::vector<Form> forms;
		std::vector<Morpheme> morphemes;
		utils::FrozenTrie<kchar_t, const Form*> formTrie;
		std::shared_ptr<lm::KnLangModelBase> langMdl;
		std::unique_ptr<utils::ThreadPool> pool;
			
		std::vector<TokenResult> analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, size_t topN, Match matchOptions) const;

		const Morpheme* getDefaultMorpheme(POSTag tag) const;

		ArchType selectedArch = ArchType::none;
		void* dfSplitByTrie = nullptr;
		void* dfFindBestPath = nullptr;

	public:
		/**
		 * @brief 빈 Kiwi 객체를 생성한다.
		 * 
		 * @note 이 생성자는 기본 생성자로 이를 통해 생성된 객체는 바로 형태소 분석에 사용할 수 없다.
		 * kiwi::KiwiBuilder 를 통해 생성된 객체만이 형태소 분석에 사용할 수 있다.
		 */
		Kiwi(ArchType arch = ArchType::default_, size_t lmKeySize = 2);

		~Kiwi();

		Kiwi(const Kiwi&) = delete;

		Kiwi(Kiwi&&);

		Kiwi& operator=(const Kiwi&) = delete;

		Kiwi& operator=(Kiwi&&);

		/**
		 * @brief 현재 Kiwi 객체가 형태소 분석을 수행할 준비가 되었는지를 알려준다.
		 * 
		 * @return 형태소 분석 준비가 완료된 경우 true를 반환한다.
		 * 
		 * @note 기본 생성자를 통해 생성된 경우 언제나 `ready() == false`이며,
		 * `kiwi::KiwiBuilder`를 통해 생성된 경우 `ready() == true`이다.
		 */
		bool ready() const { return !forms.empty(); }

		ArchType archType() const { return selectedArch; }

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param matchOptions 
		 * @return TokenResult 
		 */
		TokenResult analyze(const std::u16string& str, Match matchOptions) const
		{
			return analyze(str, 1, matchOptions)[0];
		}

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param matchOptions 
		 * @return TokenResult 
		 */
		TokenResult analyze(const std::string& str, Match matchOptions) const
		{
			return analyze(utf8To16(str), matchOptions);
		}

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param topN 
		 * @param matchOptions 
		 * @return std::vector<TokenResult> 
		 */
		std::vector<TokenResult> analyze(const std::u16string& str, size_t topN, Match matchOptions) const;

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param topN 
		 * @param matchOptions 
		 * @return std::vector<TokenResult> 
		 */
		std::vector<TokenResult> analyze(const std::string& str, size_t topN, Match matchOptions) const
		{
			return analyze(utf8To16(str), topN, matchOptions);
		}

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param topN 
		 * @param matchOptions 
		 * @return std::future<std::vector<TokenResult>> 
		 */
		std::future<std::vector<TokenResult>> asyncAnalyze(const std::string& str, size_t topN, Match matchOptions) const;

		/**
		 * @brief 
		 * 
		 * @tparam ReaderCallback 
		 * @tparam ResultCallback 
		 * @param topN 
		 * @param reader 
		 * @param resultCallback 
		 * @param matchOptions 
		 */
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

		/**
		 * @brief
		 *
		 * @param str
		 * @param matchOptions
		 * @param tokenizedResultOut
		 * @return std::vector<pair<size_t, size_t>>
		 */
		std::vector<std::pair<size_t, size_t>> splitIntoSents(
			const std::u16string& str, 
			Match matchOptions = Match::allWithNormalizing, 
			TokenResult* tokenizedResultOut = nullptr
		) const;

		/**
		 * @brief
		 *
		 * @param str
		 * @param matchOptions
		 * @param tokenizedResultOut
		 * @return std::vector<pair<size_t, size_t>>
		 */
		std::vector<std::pair<size_t, size_t>> splitIntoSents(
			const std::string& str,
			Match matchOptions = Match::allWithNormalizing,
			TokenResult* tokenizedResultOut = nullptr
		) const;

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
			return pool ? pool->size() : 1;
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

		const lm::KnLangModelBase* getLangModel() const
		{
			return langMdl.get();
		}
	};

	/**
	 * @brief 형태소 분석에 사용될 사전을 관리하고, 
	 * 사전을 바탕으로 실제 형태소 분석을 수행하는 Kiwi의 인스턴스를 생성하는 클래스.
	 * 
	 */
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
		void updateMorphemes();

		bool addWord(const std::u16string& newForm, POSTag tag, float score, size_t origMorphemeId);

	public:
		struct FromRawData {};
		static constexpr FromRawData fromRawDataTag = {};

		/**
		 * @brief KiwiBuilder의 기본 생성자
		 * 
		 * @note 이 생성자로 생성된 경우 `ready() == false`인 상태이므로 유효한 Kiwi 객체를 생성할 수 없다.
		 */
		KiwiBuilder();

		~KiwiBuilder();

		KiwiBuilder(const KiwiBuilder&);

		KiwiBuilder(KiwiBuilder&&);

		KiwiBuilder& operator=(const KiwiBuilder&);

		KiwiBuilder& operator=(KiwiBuilder&&);

		/**
		 * @brief KiwiBuilder를 raw 데이터로부터 생성한다.
		 * 
		 * @param rawDataPath 
		 * @param numThreads 
		 * @param options
		 * 
		 * @note 이 함수는 현재 내부적으로 모델 구축에 쓰인다. 
		 * 추후 공개 데이터로도 쉽게 직접 모델을 구축할 수 있도록 개선된 API를 제공할 예정.
		 */
		KiwiBuilder(FromRawData, const std::string& rawDataPath, size_t numThreads = 0, BuildOption options = BuildOption::integrateAllomorph | BuildOption::loadDefaultDict);

		/**
		 * @brief KiwiBuilder를 모델 파일로부터 생성한다.
		 * 
		 * @param modelPath 모델이 위치한 경로
		 * @param numThreads 모델 및 형태소 분석에 사용할 스레드 개수
		 * @param options 생성 옵션. `kiwi::BuildOption`을 참조
		 */
		KiwiBuilder(const std::string& modelPath, size_t numThreads = 0, BuildOption options = BuildOption::integrateAllomorph | BuildOption::loadDefaultDict);

		/**
		 * @brief 현재 KiwiBuilder 객체가 유효한 분석 모델을 로딩한 상태인지 알려준다.
		 * 
		 * @return 유효한 상태면 true를 반환한다. 기본 생성자로 생성한 경우 `ready() == false`이며,
		 * 다른 생성자로 생성한 경우는 `ready() == true`이다.
		 */
		bool ready() const
		{
			return !!langMdl;
		}

		void saveModel(const std::string& modelPath) const;

		/**
		 * @brief 
		 * 
		 * @param form 
		 * @param tag 
		 * @param score 
		 * @return
		 */
		bool addWord(const std::u16string& form, POSTag tag = POSTag::nnp, float score = 0);

		/**
		 * @brief 
		 *
		 * @param newForm
		 * @param tag
		 * @param score
		 * @param origForm
		 * @return
		 */
		bool addWord(const std::u16string& newForm, POSTag tag, float score, const std::u16string& origForm);

		/**
		 * @brief 규칙에 의해 변형된 형태소 목록을 생성하여 자동 추가한다.
		 * 
		 * @param tag 
		 * @param repl 
		 * @param score 
		 * @return 새로 추가된 변형된 형태소 목록
		 */
		template<class Replacer>
		std::vector<std::u16string> addRule(POSTag tag, Replacer&& repl, float score = 0)
		{
			std::vector<std::u16string> ret;
			for (auto& m : morphemes)
			{
				size_t morphemeId = &m - morphemes.data();
				if (morphemeId < defaultTagSize || m.tag != tag) continue;
				std::u16string input = joinHangul(forms[m.kform].form);
				std::u16string output = repl(input);
				if (input == output) continue;
				if (addWord(output, tag, score, morphemeId))
				{
					ret.emplace_back(output);
				}
			}
			return ret;
		}

		/**
		 * @brief 
		 * 
		 * @param dictPath 
		 * @return  
		 */
		size_t loadDictionary(const std::string& dictPath);

		std::vector<WordInfo> extractWords(const U16MultipleReader& reader, 
			size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3, bool lmFilter = true
		) const;

		std::vector<WordInfo> extractAddWords(const U16MultipleReader& reader, 
			size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.25, float posThreshold = -3, bool lmFilter = true
		);

		/**
		 * @brief 현재 단어 및 사전 설정을 기반으로 Kiwi 객체를 생성한다.
		 * 
		 * @return 형태소 분석 준비가 완료된 Kiwi의 객체.
		 */
		Kiwi build(ArchType arch = ArchType::default_) const;

		const lm::KnLangModelBase* getLangModel() const
		{
			return langMdl.get();
		}
	};
}
