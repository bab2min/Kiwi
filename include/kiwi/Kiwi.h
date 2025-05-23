/**
 * @file Kiwi.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kiwi C++ API를 담고 있는 헤더 파일
 * @version 0.20.0
 * @date 2024-07-01
 * 
 * 
 */
#pragma once

#include <iostream>
#include <future>
#include <string>
#include <string_view>
#include "Macro.h"
#include "Types.h"
#include "Form.h"
#include "Utils.h"
#include "Trainer.h"
#include "Trie.hpp"
#include "PatternMatcher.h"
#include "FrozenTrie.h"
#include "Knlm.h"
#include "SkipBigramModel.h"
#include "CoNgramModel.h"
#include "ThreadPool.h"
#include "WordDetector.h"
#include "TagUtils.h"
#include "LangModel.h"
#include "Joiner.h"
#include "TypoTransformer.h"

namespace kiwi
{
	//// 헤더 파일 분리를 위한 전방 선언
	struct KTrie;
	struct KGraphNode;
	struct WordInfo;
	class HSDataset;

	namespace cmb
	{ 
		class CompiledRule; 
		struct Result;
	}

	template<class Ty> class RaggedVector;
	////

	inline uint32_t getDefaultMorphemeId(POSTag tag)
	{
		return (uint32_t)clearIrregular(tag) + 1;
	}

	struct AnalyzeOption
	{
		Match match = Match::allWithNormalizing;
		const std::unordered_set<const Morpheme*>* blocklist = nullptr;
		bool openEnding = false;

		AnalyzeOption() = default;
		AnalyzeOption(Match m, const std::unordered_set<const Morpheme*>* bl = nullptr, bool oe = false)
			: match{ m }, blocklist{ bl }, openEnding{ oe } 
		{}
	};

	/**
	 * @brief 실제 형태소 분석을 수행하는 클래스.
	 * 
	 */
	class Kiwi
	{
		friend class KiwiBuilder;
		template<class LangModel> friend struct BestPathFinder;
		template<class LmState, class> friend struct PathEvaluator;
		template<class LmState> friend struct MorphemeEvaluator;
		friend class cmb::AutoJoiner;
		template<template<ArchType> class LmState> friend struct NewAutoJoinerGetter;

		bool integrateAllomorph = true;
		float cutOffThreshold = 8;
		float unkFormScoreScale = 5;
		float unkFormScoreBias = 5;
		float spacePenalty = 7;
		float typoCostWeight = 6;
		float continualTypoCost = INFINITY;
		float lengtheningTypoCost = INFINITY;
		size_t maxUnkFormSize = 6;
		size_t spaceTolerance = 0;

		TagSequenceScorer tagScorer;

		Vector<Form> forms;
		Vector<Morpheme> morphemes;
		KString typoPool;
		Vector<size_t> typoPtrs;
		Vector<TypoForm> typoForms;
		utils::FrozenTrie<kchar_t, const Form*> formTrie;
		std::shared_ptr<lm::ILangModel> langMdl;
		std::shared_ptr<cmb::CompiledRule> combiningRule;
		std::unique_ptr<utils::ThreadPool> pool;
		
		const Morpheme* getDefaultMorpheme(POSTag tag) const;

		ArchType selectedArch = ArchType::none;
		void* dfSplitByTrie = nullptr;
		void* dfFindForm = nullptr;
		void* dfFindBestPath = nullptr;
		void* dfNewJoiner = nullptr;
	
	public:
		enum class SpecialMorph {
			singleQuoteOpen = 0,
			singleQuoteClose,
			singleQuoteNA,
			doubleQuoteOpen,
			doubleQuoteClose,
			doubleQuoteNA,
			max,
		};

	private:
		std::array<size_t, static_cast<size_t>(SpecialMorph::max)> specialMorphIds = { { 0, } };

		template<class Str, class Pretokenized, class ...Rest>
		auto _asyncAnalyze(Str&& str, Pretokenized&& pt, Rest&&... args) const;

		template<class Str, class Pretokenized, class ...Rest>
		auto _asyncAnalyzeEcho(Str&& str, Pretokenized&& pt, Rest&&... args) const;

		static std::vector<PretokenizedSpan> mapPretokenizedSpansToU16(const std::vector<PretokenizedSpan>& orig, const std::vector<size_t>& bytePositions);

	public:

		/**
		 * @brief 빈 Kiwi 객체를 생성한다.
		 * 
		 * @note 이 생성자는 기본 생성자로 이를 통해 생성된 객체는 바로 형태소 분석에 사용할 수 없다.
		 * kiwi::KiwiBuilder 를 통해 생성된 객체만이 형태소 분석에 사용할 수 있다.
		 */
		Kiwi(ArchType arch = ArchType::default_, 
			const std::shared_ptr<lm::ILangModel>& _langMdl = {}, 
			bool typoTolerant = false, 
			bool continualTypoTolerant = false, 
			bool lengtheningTypoTolerant = false);

		~Kiwi();

		Kiwi(const Kiwi&) = delete;

		Kiwi(Kiwi&&) noexcept;

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

		ModelType modelType() const { return langMdl ? langMdl->getType() : ModelType::none; }

		/**
		 * @brief 현재 Kiwi 객체가 오타 교정 기능이 켜진 상태로 생성되었는지 알려준다.
		 * 
		 * @return 오타 교정 기능이 켜진 경우 true를 반환한다.
		 */
		bool isTypoTolerant() const { return !typoForms.empty(); }

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param option 
		 * @return TokenResult 
		 */
		TokenResult analyze(const std::u16string& str, AnalyzeOption option,
			const std::vector<PretokenizedSpan>& pretokenized = {}
		) const
		{
			return analyze(str, 1, option, pretokenized)[0];
		}

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param option 
		 * @return TokenResult 
		 */
		TokenResult analyze(const std::string& str, AnalyzeOption option, 
			const std::vector<PretokenizedSpan>& pretokenized = {}
		) const
		{
			std::vector<size_t> bytePositions;
			auto u16str = utf8To16(str, bytePositions);
			return analyze(u16str, option, mapPretokenizedSpansToU16(pretokenized, bytePositions));
		}

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param topN 
		 * @param option 
		 * @return std::vector<TokenResult> 
		 */
		std::vector<TokenResult> analyze(const std::u16string& str, size_t topN, AnalyzeOption option,
			const std::vector<PretokenizedSpan>& pretokenized = {}
		) const;

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param topN 
		 * @param option 
		 * @return std::vector<TokenResult> 
		 */
		std::vector<TokenResult> analyze(const std::string& str, size_t topN, AnalyzeOption option,
			const std::vector<PretokenizedSpan>& pretokenized = {}) const
		{
			std::vector<size_t> bytePositions;
			auto u16str = utf8To16(str, bytePositions);
			return analyze(u16str, topN, option, mapPretokenizedSpansToU16(pretokenized, bytePositions));
		}

		/**
		 * @brief 
		 * 
		 * @param str 
		 * @param topN 
		 * @param option
		 * @return std::future<std::vector<TokenResult>> 
		 */
		std::future<std::vector<TokenResult>> asyncAnalyze(const std::string& str, size_t topN, AnalyzeOption option,
			const std::vector<PretokenizedSpan>& pretokenized = {}
		) const;
		std::future<std::vector<TokenResult>> asyncAnalyze(std::string&& str, size_t topN, AnalyzeOption option,
			std::vector<PretokenizedSpan>&& pretokenized = {}
		) const;

		std::future<TokenResult> asyncAnalyze(const std::string& str, AnalyzeOption option,
			const std::vector<PretokenizedSpan>& pretokenized = {}
		) const;
		std::future<TokenResult> asyncAnalyze(std::string&& str, AnalyzeOption option,
			std::vector<PretokenizedSpan>&& pretokenized = {}
		) const;
		std::future<std::pair<TokenResult, std::string>> asyncAnalyzeEcho(std::string&& str, AnalyzeOption option,
			std::vector<PretokenizedSpan>&& pretokenized = {}
		) const;

		std::future<std::vector<TokenResult>> asyncAnalyze(const std::u16string& str, size_t topN, AnalyzeOption option,
			const std::vector<PretokenizedSpan>& pretokenized = {}
		) const;
		std::future<std::vector<TokenResult>> asyncAnalyze(std::u16string&& str, size_t topN, AnalyzeOption option,
			std::vector<PretokenizedSpan>&& pretokenized = {}
		) const;

		std::future<TokenResult> asyncAnalyze(const std::u16string& str, AnalyzeOption option,
			const std::vector<PretokenizedSpan>& pretokenized = {}
		) const;
		std::future<TokenResult> asyncAnalyze(std::u16string&& str, AnalyzeOption option,
			std::vector<PretokenizedSpan>&& pretokenized = {}
		) const;
		std::future<std::pair<TokenResult, std::u16string>> asyncAnalyzeEcho(std::u16string&& str, AnalyzeOption option,
			std::vector<PretokenizedSpan>&& pretokenized = {}
		) const;

		/**
		 * @brief 
		 * 
		 * @tparam ReaderCallback 
		 * @tparam ResultCallback 
		 * @param topN 
		 * @param reader 
		 * @param resultCallback 
		 * @param option 
		 */
		template<class ReaderCallback, class ResultCallback>
		void analyze(size_t topN, ReaderCallback&& reader, ResultCallback&& resultCallback, 
			AnalyzeOption option
		) const
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
					futures.emplace_back(pool->enqueue([=, ustr = std::move(ustr)](size_t tid)
					{
						return analyze(ustr, topN, option, {});
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
						futures.emplace_back(pool->enqueue([=, ustr = std::move(ustr)](size_t tid)
						{
							return analyze(ustr, topN, option, {});
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
					resultCallback(analyze(ustr, topN, option));
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


		template<class LmState>
		cmb::AutoJoiner newJoinerImpl() const;

		/**
		 * @brief 형태소들을 결합하여 텍스트로 복원해주는 작업을 수행하는 AutoJoiner를 반환한다.
		 * 
		 * @param lmSearch 결합 전에 언어 모델을 이용하여 최적의 형태소를 탐색하여 사용한다.
		 * @return 새 AutoJoiner 인스턴스
		 * 
		 * @sa kiwi::cmb::AutoJoiner
		 */
		cmb::AutoJoiner newJoiner(bool lmSearch = true) const;

		/**
		 * @brief `TokenInfo::typoFormId`로부터 실제 오타 형태를 복원한다.
		 * 
		 * @param typoFormId analyze함수의 리턴으로 반환된 TokenInfo 내의 typoFormId 값
		 * @return 복원된 오타의 형태
		 */
		std::u16string getTypoForm(size_t typoFormId) const;

		size_t morphToId(const Morpheme* morph) const
		{
			if (!morph || morph < morphemes.data()) return -1;
			return morph - morphemes.data();
		}

		size_t getSpecialMorphId(SpecialMorph type) const
		{
			return specialMorphIds[static_cast<size_t>(type)];
		}

		SpecialMorph determineSpecialMorphType(size_t morphId) const
		{
			for (size_t i = 0; i < specialMorphIds.size(); ++i)
			{
				if (morphId == specialMorphIds[i]) return static_cast<SpecialMorph>(i);
			}
			return SpecialMorph::max;
		}

		size_t getMorphemeSize() const { return morphemes.size(); }

		const Morpheme* idToMorph(size_t morphId) const
		{
			if (morphId >= morphemes.size()) return nullptr;
			return &morphemes[morphId];
		}

		size_t getNumThreads() const
		{
			return pool ? pool->size() : 1;
		}

		utils::ThreadPool* getThreadPool() const
		{
			return pool.get();
		}

		float getCutOffThreshold() const
		{
			return cutOffThreshold;
		}

		void setCutOffThreshold(float v)
		{
			if (v < 0) throw std::invalid_argument{ "`v` must >= 0" };
			cutOffThreshold = v;
		}

		float getUnkScoreBias() const
		{
			return unkFormScoreBias;
		}

		void setUnkScoreBias(float v)
		{
			if (v < 0) throw std::invalid_argument{ "`v` must >= 0" };
			unkFormScoreBias = v;
		}

		float getUnkScoreScale() const
		{
			return unkFormScoreScale;
		}

		void setUnkScoreScale(float v)
		{
			if (v < 0) throw std::invalid_argument{ "`v` must >= 0" };
			unkFormScoreScale = v;
		}

		size_t getMaxUnkFormSize() const
		{
			return maxUnkFormSize;
		}

		void setMaxUnkFormSize(size_t v)
		{
			maxUnkFormSize = v;
		}

		size_t getSpaceTolerance() const
		{
			return spaceTolerance;
		}

		void setSpaceTolerance(size_t v)
		{
			spaceTolerance = v;
		}

		float getSpacePenalty() const
		{
			return spacePenalty;
		}

		void setSpacePenalty(float v)
		{
			if (v < 0) throw std::invalid_argument{ "`v` must >= 0" };
			spacePenalty = v;
		}

		float getTypoCostWeight() const
		{
			return typoCostWeight;
		}

		void setTypoCostWeight(float v)
		{
			if (v < 0) throw std::invalid_argument{ "`v` must >= 0" };
			typoCostWeight = v;
		}

		bool getIntegrateAllomorph() const
		{
			return integrateAllomorph;
		}

		void setIntegrateAllomorph(bool v)
		{
			integrateAllomorph = v;
		}

		const lm::ILangModel* getLangModel() const
		{
			return langMdl.get();
		}

		const Morpheme* findMorpheme(const std::u16string_view& form, POSTag tag = POSTag::unknown) const;

		size_t findMorphemeId(const std::u16string_view& form, POSTag tag = POSTag::unknown) const
		{
			const Morpheme* morph = findMorpheme(form, tag);
			if (!morph) return -1;
			return morphToId(morph);
		}

		void findMorphemes(std::vector<const Morpheme*>& out, const std::u16string_view& s, POSTag tag = POSTag::unknown) const;
		std::vector<const Morpheme*> findMorphemes(const std::u16string_view& s, POSTag tag = POSTag::unknown) const;
	};

	/**
	 * @brief 형태소 분석에 사용될 사전을 관리하고, 
	 * 사전을 바탕으로 실제 형태소 분석을 수행하는 Kiwi의 인스턴스를 생성하는 클래스.
	 * 
	 */
	class KiwiBuilder
	{
		Vector<FormRaw> forms;
		Vector<MorphemeRaw> morphemes;
		UnorderedMap<KString, size_t> formMap;
		std::shared_ptr<lm::ILangModel> langMdl;
		std::shared_ptr<cmb::CompiledRule> combiningRule;
		WordDetector detector;

		size_t numThreads = 0;
		BuildOption options = BuildOption::none;
		ModelType modelType = ModelType::none;
		ArchType archType = ArchType::none;

	public:
		struct ModelBuildArgs
		{
			std::string morphemeDef;
			std::vector<std::string> corpora;
			size_t minMorphCnt = 10;
			size_t lmOrder = 4;
			std::vector<size_t> lmMinCnts = { 1 };
			size_t numWorkers = 1;
			size_t sbgSize = 1000000;
			double sbgEpochs = 10;
			size_t sbgEvalSetRatio = 20;
			size_t sbgMinCount = 150;
			size_t sbgMinCoCount = 20;
			bool useLmTagHistory = true;
			bool quantizeLm = true;
			bool compressLm = true;
			float dropoutSampling = 0.05f;
			float dropoutProb = 0.15f;
		};

	private:
		using MorphemeMap = UnorderedMap<std::tuple<KString, uint8_t, POSTag>, std::pair<size_t, size_t>>;

		template<class VocabTy>
		std::unique_ptr<lm::KnLangModelBase> buildKnLM(const ModelBuildArgs& args, size_t lmVocabSize, MorphemeMap& morphMap) const;

		void loadMorphBin(std::istream& is);
		void saveMorphBin(std::ostream& os) const;
		FormRaw& addForm(const KString& form);
		size_t addForm(Vector<FormRaw>& newForms, UnorderedMap<KString, size_t>& newFormMap, KString form) const;

		
		void initMorphemes();

		template<class Fn>
		MorphemeMap loadMorphemesFromTxt(std::istream& is, Fn&& filter);

		MorphemeMap restoreMorphemeMap(bool separateDefaultMorpheme = false) const;

		template<class VocabTy>
		void _addCorpusTo(RaggedVector<VocabTy>& out, std::istream& is, MorphemeMap& morphMap, 
			double splitRatio, RaggedVector<VocabTy>* splitOut,
			UnorderedMap<std::pair<KString, POSTag>, size_t>* oovDict = nullptr,
			const UnorderedMap<std::pair<KString, POSTag>, std::pair<KString, POSTag>>* transform = nullptr) const;

		void addCorpusTo(RaggedVector<uint8_t>& out, std::istream& is, MorphemeMap& morphMap, 
			double splitRatio = 0, RaggedVector<uint8_t>* splitOut = nullptr,
			UnorderedMap<std::pair<KString, POSTag>, size_t>* oovDict = nullptr,
			const UnorderedMap<std::pair<KString, POSTag>, std::pair<KString, POSTag>>* transform = nullptr) const;
		void addCorpusTo(RaggedVector<uint16_t>& out, std::istream& is, MorphemeMap& morphMap, 
			double splitRatio = 0, RaggedVector<uint16_t>* splitOut = nullptr,
			UnorderedMap<std::pair<KString, POSTag>, size_t>* oovDict = nullptr,
			const UnorderedMap<std::pair<KString, POSTag>, std::pair<KString, POSTag>>* transform = nullptr) const;
		void addCorpusTo(RaggedVector<uint32_t>& out, std::istream& is, MorphemeMap& morphMap, 
			double splitRatio = 0, RaggedVector<uint32_t>* splitOut = nullptr,
			UnorderedMap<std::pair<KString, POSTag>, size_t>* oovDict = nullptr,
			const UnorderedMap<std::pair<KString, POSTag>, std::pair<KString, POSTag>>* transform = nullptr) const;
		void addCorpusTo(RaggedVector<int32_t>& out, std::istream& is, MorphemeMap& morphMap,
			double splitRatio = 0, RaggedVector<int32_t>* splitOut = nullptr,
			UnorderedMap<std::pair<KString, POSTag>, size_t>* oovDict = nullptr,
			const UnorderedMap<std::pair<KString, POSTag>, std::pair<KString, POSTag>>* transform = nullptr) const;
		void updateForms();
		void updateMorphemes(size_t vocabSize = 0);

		size_t findMorpheme(U16StringView form, POSTag tag) const;

		std::pair<uint32_t, bool> addWord(U16StringView newForm, POSTag tag, float score, size_t origMorphemeId, size_t lmMorphemeId);
		std::pair<uint32_t, bool> addWord(const std::u16string& newForm, POSTag tag, float score, size_t origMorphemeId, size_t lmMorphemeId);
		std::pair<uint32_t, bool> addWord(U16StringView form, POSTag tag = POSTag::nnp, float score = 0);
		std::pair<uint32_t, bool> addWord(U16StringView newForm, POSTag tag, float score, U16StringView origForm);

		template<class U16>
		bool addPreAnalyzedWord(U16StringView form,
			const std::vector<std::pair<U16, POSTag>>& analyzed,
			std::vector<std::pair<size_t, size_t>> positions = {},
			float score = 0
		);

		void addCombinedMorpheme(
			Vector<FormRaw>& newForms,
			UnorderedMap<KString, size_t>& newFormMap,
			Vector<MorphemeRaw>& newMorphemes,
			UnorderedMap<size_t, Vector<uint32_t>>& newFormCands,
			size_t leftId,
			size_t rightId,
			const cmb::Result& r
		) const;

		void addCombinedMorphemes(
			Vector<FormRaw>& newForms,
			UnorderedMap<KString, size_t>& newFormMap,
			Vector<MorphemeRaw>& newMorphemes,
			UnorderedMap<size_t, Vector<uint32_t>>& newFormCands,
			size_t leftId,
			size_t rightId,
			size_t ruleId
		) const;

		void buildCombinedMorphemes(
			Vector<FormRaw>& newForms,
			UnorderedMap<KString, size_t>& newFormMap,
			Vector<MorphemeRaw>& newMorphemes,
			UnorderedMap<size_t, Vector<uint32_t>>& newFormCands
		) const;

		void addAllomorphsToRule();

		std::array<size_t, static_cast<size_t>(Kiwi::SpecialMorph::max)> getSpecialMorphs() const;

	public:

		/**
		 * @brief 주어진 모델 경로로부터 모델의 타입을 추정한다.
		 */
		static ModelType getModelType(const std::string& modelPath, bool largest = false);

		/**
		 * @brief KiwiBuilder의 기본 생성자
		 *
		 * @note 이 생성자로 생성된 경우 `ready() == false`인 상태이므로 유효한 Kiwi 객체를 생성할 수 없다.
		 */
		KiwiBuilder();

		~KiwiBuilder();

		KiwiBuilder(const KiwiBuilder&);

		KiwiBuilder(KiwiBuilder&&) noexcept;

		KiwiBuilder& operator=(const KiwiBuilder&);

		KiwiBuilder& operator=(KiwiBuilder&&);

		/**
		 * @brief KiwiBuilder를 raw 데이터로부터 생성한다.
		 *
		 *
		 * @note 이 함수는 현재 내부적으로 기본 모델 구축에 쓰인다.
		 * 추후 공개 데이터로도 쉽게 직접 모델을 구축할 수 있도록 개선된 API를 제공할 예정.
		 */
		KiwiBuilder(const ModelBuildArgs& args);

		/**
		 * @brief 기본 모델로부터 확장 모델을 학습하여 생성한다.
		 */
		KiwiBuilder(const std::string& modelPath, const ModelBuildArgs& args);

		/**
		 * @brief KiwiBuilder를 모델 파일로부터 생성한다.
		 *
		 * @param modelPath 모델이 위치한 경로
		 * @param numThreads 모델 및 형태소 분석에 사용할 스레드 개수. 
		 *                   0일 경우 async를 지원하지 않는 단일 스레드에서 동작하며, 1 이상일 경우 지정한 개수만큼 worker 스레드를 생성하여 async가 지원된다.
		 *                   기본값은 -1으로 이 경우 시스템의 CPU 코어 개수를 탐지하여 이 값을 numThreads값으로 설정한다.
		 * @param options 생성 옵션. `kiwi::BuildOption`을 참조
		 */
		KiwiBuilder(const std::string& modelPath, size_t numThreads = -1, BuildOption options = BuildOption::default_, ModelType modelType = ModelType::none);

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
		 * @brief 사전에 새로운 형태소를 추가한다. 이미 동일한 형태소가 있는 경우는 무시된다.
		 *
		 * @param form 새로운 형태소의 형태
		 * @param tag 품사 태그
		 * @param score 페널티 점수. 이에 대한 자세한 설명은 하단의 note 참조.
		 * @return 추가된 형태소의 ID와 성공 여부를 pair로 반환한다. `form/tag` 형태소가 이미 존재하는 경우 추가에 실패한다.
		 * @note 이 방법으로 추가된 형태소는 언어모델 탐색에서 어휘 사전 외 토큰(OOV 토큰)으로 처리된다.
		 * 이 방법으로 추가된 형태소는 항상 분석 과정에서 최우선으로 탐색되지는 않으므로 최상의 결과를 위해서는 `score` 값을 조절할 필요가 있다.
		 * `score` 값을 높게 설정할수록 다른 후보들과의 경쟁에서 이 형태소가 더 높은 점수를 받아 최종 분석 결과에 노출될 가능성이 높아진다.
		 * 만약 이 방법으로 추가된 형태소가 원치 않는 상황에서 과도하게 출력되는 경우라면 `score`를 더 작은 값으로,
		 * 반대로 원하는 상황에서도 출력되지 않는 경우라면 `score`를 더 큰 값으로 조절하는 게 좋다.
		 */
		std::pair<uint32_t, bool> addWord(const std::u16string& form, POSTag tag = POSTag::nnp, float score = 0);
		std::pair<uint32_t, bool> addWord(const char16_t* form, POSTag tag = POSTag::nnp, float score = 0);

		/**
		 * @brief 사전에 기존 형태소의 변이형을 추가한다. 이미 동일한 형태소가 있는 경우는 무시된다.
		 *
		 * @param newForm 새로운 형태
		 * @param tag 품사 태그
		 * @param score 새로운 형태의 페널티 점수. 이에 대한 자세한 설명은 하단의 `addWord`함수의 note 참조.
		 * @param origForm 기존 형태
		 * @return 추가된 형태소의 ID와 성공 여부를 pair로 반환한다. `newForm/tag` 형태소가 이미 존재하는 경우 추가에 실패한다.
		 * @exception kiwi::UnknownMorphemeException `origForm/tag`에 해당하는 형태소가 없을 경우 예외를 발생시킨다.
		 * @note 이 방법으로 추가된 형태소는 언어모델 탐색 과정에서 `origForm/tag` 토큰으로 처리된다.
		 */
		std::pair<uint32_t, bool> addWord(const std::u16string& newForm, POSTag tag, float score, const std::u16string& origForm);
		std::pair<uint32_t, bool> addWord(const char16_t* newForm, POSTag tag, float score, const char16_t* origForm);

		/**
		 * @brief 사전에 기분석 형태소열을 추가한다. 이미 동일한 기분석 형태소열이 있는 경우는 무시된다.
		 *
		 * @param form 분석될 문자열 형태
		 * @param analyzed `form` 문자열이 입력되었을 때, 이의 분석결과로 내놓을 형태소의 배열
		 * @param positions `analyzed`의 각 형태소가 `form`내에서 차지하는 위치(시작/끝 지점, char16_t 단위). 생략 가능
		 * @param score 페널티 점수. 이에 대한 자세한 설명은 하단의 `addWord`함수의 note 참조.
		 * @exception kiwi::UnknownMorphemeException `analyzed`로 주어진 형태소 중 하나라도 존재하지 않는게 있는 경우 예외를 발생시킨다.
		 * @return 형태소열을 추가하는데 성공했으면 true, 동일한 형태소열이 존재하여 추가에 실패한 경우 false를 반환한다.
		 * @note 이 함수는 특정 문자열이 어떻게 분석되어야하는지 직접적으로 지정해줄 수 있다.
		 * 따라서 `addWord` 함수를 사용해도 오분석이 발생하는 경우, 이 함수를 통해 해당 사례들에 대해 정확한 분석 결과를 추가하면 원하는 분석 결과를 얻을 수 있다.
		 */
		bool addPreAnalyzedWord(const std::u16string& form,
			const std::vector<std::pair<std::u16string, POSTag>>& analyzed,
			std::vector<std::pair<size_t, size_t>> positions = {},
			float score = 0
		);
		bool addPreAnalyzedWord(const char16_t* form,
			const std::vector<std::pair<const char16_t*, POSTag>>& analyzed,
			std::vector<std::pair<size_t, size_t>> positions = {},
			float score = 0
		);

		/**
		 * @brief 규칙에 의해 변형된 형태소 목록을 생성하여 자동 추가한다.
		 *
		 * @param tag
		 * @param repl
		 * @param score
		 * @return 새로 추가된 변형된 형태소의 ID와 그 형태를 pair로 묶은 목록
		 */
		template<class Replacer>
		std::vector<std::pair<uint32_t, std::u16string>> addRule(POSTag tag, Replacer&& repl, float score = 0)
		{
			std::vector<std::pair<uint32_t, std::u16string>> ret;
			size_t formSize = forms.size();
			for (size_t i = 0; i < formSize; ++i)
			{
				auto& f = forms[i];
				const MorphemeRaw* m = nullptr;
				for (auto j : f.candidate)
				{
					if (morphemes[j].tag == tag)
					{
						m = &morphemes[j];
						break;
					}
				}
				if (!m || f.form.empty()) continue;
				std::u16string input = joinHangul(f.form);
				std::u16string output = repl(input);
				if (input == output) continue;
				size_t morphemeId = m->lmMorphemeId ? m->lmMorphemeId : (size_t)(m - morphemes.data());
				auto added = addWord(output, tag, score + (m->lmMorphemeId ? m->userScore : 0), morphemeId, 0);
				if (added.second)
				{
					ret.emplace_back(added.first, output);
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
		 * @param typos
		 * @param typoCostThreshold
		 * @return 형태소 분석 준비가 완료된 Kiwi의 객체.
		 */
		Kiwi build(const TypoTransformer& typos = {}, float typoCostThreshold = 2.5f) const;

		Kiwi build(DefaultTypoSet typos, float typoCostThreshold = 2.5f) const
		{
			return build(getDefaultTypoSet(typos), typoCostThreshold);
		}

		void convertHSData(
			const std::vector<std::string>& inputPathes,
			const std::string& outputPath,
			const std::string& morphemeDefPath = {},
			size_t morphemeDefMinCnt = 0,
			bool generateOovDict = false,
			const std::vector<std::pair<std::pair<std::string, POSTag>, std::pair<std::string, POSTag>>>* transform = nullptr
		) const;

		using TokenFilter = std::function<bool(const std::u16string&, POSTag)>;

		HSDataset makeHSDataset(const std::vector<std::string>& inputPathes,
			size_t batchSize, size_t causalContextSize, size_t windowSize, size_t numWorkers,
			double dropoutProb = 0,
			double dropoutProbOnHistory = 0,
			double nounAugmentingProb = 0,
			size_t generateUnlikelihoods = -1,
			const TokenFilter& tokenFilter = {},
			const TokenFilter& windowFilter = {},
			double splitRatio = 0,
			bool separateDefaultMorpheme = false,
			const std::string& morphemeDefPath = {},
			size_t morphemeDefMinCnt = 0,
			const std::vector<std::pair<size_t, std::vector<uint32_t>>>& contextualMapper = {},
			HSDataset* splitDataset = nullptr,
			const std::vector<std::pair<std::pair<std::string, POSTag>, std::pair<std::string, POSTag>>>* transform = nullptr
		) const;

		BuildOption getOptions() const { return options; }
		ModelType getModelType() const { return modelType; }

		static void buildMorphData(const std::string& morphemeDefPath, const std::string& outputPath, size_t minCnt = 10);
	};
}
