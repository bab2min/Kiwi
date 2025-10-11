#pragma once
#include <random>
#include "Kiwi.h"
#include "FrozenTrie.h"

namespace kiwi
{
	template<class Ty>
	class OptionalFuture
	{
		std::future<Ty> future;
		Ty val;
	public:
		OptionalFuture(Ty _val = {}) : val{ _val } {}
		OptionalFuture(std::future<Ty>&& _future) : future{ std::move(_future) } {}

		OptionalFuture(OptionalFuture&&) = default;
		OptionalFuture& operator=(OptionalFuture&&) = default;

		Ty get()
		{
			if (future.valid()) return future.get();
			return val;
		}
	};

	class HSDataset
	{
		friend class KiwiBuilder;

		struct ThreadLocal
		{
			std::mt19937_64 rng;
			Vector<int32_t> tokenBuf;
			Vector<float> lmLProbsBuf;
			Vector<uint32_t> outNgramNodeBuf;
			Deque<int32_t> inData;
			Deque<int32_t> outData;
			Deque<float> lmLProbsData;
			Deque<uint32_t> outNgramNodeData;
			Deque<float> restLmLProbsData;
			Deque<uint32_t> restLmLProbsCntData;
			Vector<std::pair<int32_t, int32_t>> unlikelihoodBuf;
			Deque<int32_t> unlikelihoodInData;
			Deque<int32_t> unlikelihoodOutData;
		};

		static constexpr int32_t nonVocab = -1;

		HiddenMember<RaggedVector<int32_t>, sizeof(Vector<size_t>) * 2> sents;
		std::shared_ptr<lm::ILangModel> langModel;
		std::shared_ptr<Kiwi> kiwiInst;
		std::shared_ptr<Vector<std::pair<std::u16string, POSTag>>> oovDict;
		std::unique_ptr<utils::ThreadPool> workers;
		std::shared_ptr<KiwiBuilder> dummyBuilder;
		std::discrete_distribution<> dropout;
		float dropoutProbOnHistory = 0;
		std::discrete_distribution<> nounAugmentor;
		std::discrete_distribution<> emojiAugmentor;
		std::discrete_distribution<> sbAugmentor;
		std::mt19937_64 rng;
		Vector<ThreadLocal> locals;
		Vector<size_t> shuffledIdx;
		Vector<int32_t> tokenToVocab, vocabToToken;
		
		Vector<uint8_t> windowTokenValidness;
		Deque<OptionalFuture<size_t>> futures;
		Vector<uint32_t> sbTokenIds;
		const Vector<MorphemeRaw>* morphemes = nullptr;
		const Vector<FormRaw>* forms = nullptr;
		utils::FrozenTrie<uint32_t, uint32_t> contextualMapper;
		size_t knlmVocabSize = 0;
		size_t batchSize = 0;
		size_t causalContextSize = 0;
		size_t windowSize = 0;
		bool exclusiveWindow = true;
		size_t generateUnlikelihoods = -1;
		size_t totalTokens = 0;
		size_t passedSents = 0;
		size_t passedWorkItems = 0;
		std::array<size_t, static_cast<size_t>(Kiwi::SpecialMorph::max)> specialMorphIds = { { 0, } };

		size_t numValidTokensInSent(size_t sentId) const;

		template<class Token>
		void prepareInOutData(Deque<int32_t>& inData, Deque<int32_t>& outData, const Vector<Token>& tokens, std::mt19937_64& rng) const;

		bool tokenizeUnlikely(Vector<std::pair<int32_t, int32_t>>& out, int32_t prefix, int32_t target, int32_t suffix, std::mt19937_64& rng) const;

		void fillSbTokenIds();

		template<class InTy, class OutTy, class LmTy, class NgramTy, class UlInTy, class UlOutTy>
		size_t _next(InTy in, OutTy out, LmTy lmLProbs, NgramTy outNgramNode, float& restLmOut, uint32_t& restLmCntOut, 
			UlInTy unlikelihoodIn, UlOutTy unlikelihoodOut, size_t* unlikelihoodSize);

	public:
		HSDataset(size_t _batchSize = 0, 
			size_t _causalContextSize = 0, 
			size_t _windowSize = 0, 
			bool _exclusiveWindow = true, 
			size_t _workers = 0, 
			HSDatasetOption option = {}
		);
		~HSDataset();
		HSDataset(const HSDataset&) = delete;
		HSDataset(HSDataset&&) /*noexcept*/;
		HSDataset& operator=(const HSDataset&) = delete;
		HSDataset& operator=(HSDataset&&) /*noexcept*/;

		size_t numEstimBatches() const;
		size_t numSents() const;
		size_t numTokens() const;
		bool doesGenerateUnlikelihoods() const { return generateUnlikelihoods < (size_t)-1; }

		size_t getBatchSize() const { return batchSize; }
		size_t getCausalContextSize() const { return causalContextSize; }
		size_t getWindowSize() const { return windowSize; }
		const Vector<uint8_t>& getWindowTokenValidness() const { return windowTokenValidness; }

		void seed(size_t newSeed);
		void reset();
		size_t next(int32_t* in, int32_t* out, float* lmLProbs, uint32_t* outNgramNode, float& restLmOut, uint32_t& restLmCntOut, 
			int32_t* unlikelihoodIn = nullptr, int32_t* unlikelihoodOut = nullptr, size_t* unlikelihoodSize = nullptr);
		size_t next(int64_t* in, int64_t* out, float* lmLProbs, int64_t* outNgramNode, float& restLmOut, uint32_t& restLmCntOut,
			int64_t* unlikelihoodIn = nullptr, int64_t* unlikelihoodOut = nullptr, size_t* unlikelihoodSize = nullptr);

		size_t vocabSize() const { return vocabToToken.size(); }
		size_t getKnlmVocabSize() const;
		size_t ngramNodeSize() const;
		const MorphemeRaw& vocabInfo(uint32_t vocab) const;
		std::u16string vocabForm(uint32_t vocab) const;
		std::vector<size_t> estimVocabFrequency() const;

		Range<Vector<int32_t>::const_iterator> getSent(size_t idx) const;
		std::vector<uint32_t> getAugmentedSent(size_t idx);

		std::vector<std::pair<std::vector<uint32_t>, size_t>> extractPrefixes(size_t minCnt, size_t maxLength, size_t numWorkers = 1, bool exclusiveCnt = false) const;
	};
}
