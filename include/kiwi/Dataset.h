#pragma once
#include <random>
#include "Kiwi.h"

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
			Vector<uint32_t> tokenBuf;
			Vector<float> lmLProbsBuf;
			Vector<uint32_t> outNgramNodeBuf;
			Deque<int32_t> historyBuf;
			Deque<int32_t> inData;
			Deque<int32_t> outData;
			Deque<float> lmLProbsData;
			Deque<uint32_t> outNgramNodeData;
			Deque<float> restLmLProbsData;
			Deque<uint32_t> restLmLProbsCntData;
		};

		static constexpr int32_t nonVocab = -1;

		HiddenMember<RaggedVector<uint32_t>, sizeof(Vector<size_t>) * 2> sents;
		std::shared_ptr<lm::KnLangModelBase> knlm;
		std::unique_ptr<utils::ThreadPool> workers;
		std::shared_ptr<KiwiBuilder> dummyBuilder;
		std::discrete_distribution<> dropout;
		std::mt19937_64 rng;
		Vector<ThreadLocal> locals;
		Vector<size_t> shuffledIdx;
		Vector<int32_t> tokenToVocab, vocabToToken;
		Vector<uint8_t> windowTokenValidness;
		Deque<OptionalFuture<size_t>> futures;
		const Vector<MorphemeRaw>* morphemes = nullptr;
		const Vector<FormRaw>* forms = nullptr;
		size_t knlmVocabSize = 0;
		size_t batchSize = 0;
		size_t causalContextSize = 0;
		size_t windowSize = 0;
		size_t totalTokens = 0;
		size_t passedSents = 0;
		size_t passedWorkItems = 0;

		size_t numValidTokensInSent(size_t sentId) const;

		template<class InTy, class OutTy, class LmTy, class NgramTy>
		size_t _next(InTy in, OutTy out, LmTy lmLProbs, NgramTy outNgramNode, float& restLmOut, uint32_t& restLmCntOut);

	public:
		HSDataset(size_t _batchSize = 0, size_t _causalContextSize = 0, size_t _windowSize = 0, size_t _workers = 0, double _dropoutProb = 0);
		~HSDataset();
		HSDataset(const HSDataset&) = delete;
		HSDataset(HSDataset&&) /*noexcept*/;
		HSDataset& operator=(const HSDataset&) = delete;
		HSDataset& operator=(HSDataset&&) /*noexcept*/;

		size_t numEstimBatches() const;
		size_t numSents() const;
		size_t numTokens() const;

		size_t getBatchSize() const { return batchSize; }
		size_t getCausalContextSize() const { return causalContextSize; }
		size_t getWindowSize() const { return windowSize; }
		const Vector<uint8_t>& getWindowTokenValidness() const { return windowTokenValidness; }

		void seed(size_t newSeed);
		void reset();
		size_t next(int32_t* in, int32_t* out, float* lmLProbs, uint32_t* outNgramNode, float& restLmOut, uint32_t& restLmCntOut);
		size_t next(int64_t* in, int64_t* out, float* lmLProbs, int64_t* outNgramNode, float& restLmOut, uint32_t& restLmCntOut);

		size_t vocabSize() const { return vocabToToken.size(); }
		size_t getKnlmVocabSize() const;
		size_t ngramNodeSize() const;
		const MorphemeRaw& vocabInfo(uint32_t vocab) const;
		std::u16string vocabForm(uint32_t vocab) const;
		std::vector<size_t> estimVocabFrequency() const;

		Range<Vector<uint32_t>::const_iterator> getSent(size_t idx) const;
		std::vector<uint32_t> getAugmentedSent(size_t idx);
	};
}
