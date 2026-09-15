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
		float dropoutProbOnHistory = 0;
		std::discrete_distribution<> dropout;
		std::discrete_distribution<> ssAugmentor;
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
		std::array<uint32_t, static_cast<size_t>(Kiwi::SpecialMorph::max)> specialMorphIds = { { 0, } };

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

	class ChrTokenizer
	{
	public:

		enum class Token : int32_t
		{
			bos = 0,
			eos = 0,
			sf, sp, ss, sso, ssc, se, so, sw, sh,
			hangulSyllableStart,
			hangulCodaStart = hangulSyllableStart + 399,
			asciiStart = hangulCodaStart + 27,
			max = asciiStart + 94,
		};
		size_t encodeOne(char32_t ch) const;
		size_t encode(std::string_view text, int32_t* outBuf, size_t bufSize) const;
		std::u16string decode(const int32_t* tokenBuf, size_t tokenCnt) const;
		size_t vocabSize() const { return static_cast<size_t>(Token::max); }
	};

	class ChrDataset
	{
		static constexpr int32_t nonVocab = -1;

		HiddenMember<RaggedVector<int32_t>, sizeof(Vector<size_t>) * 2> sents;
		Vector<float> sentWeights, sentSampled;
		Vector<uint32_t> shuffledIdcs;
		Vector<uint32_t> nonLabelPrefixSizes;
		double totalWeight = 0.;
		size_t totalSampled = 0;
		std::unique_ptr<utils::ThreadPool> workers;
		float prefixDropoutProb = 0.f;
		std::mt19937_64 rng;
		utils::FrozenTrie<uint32_t, uint32_t> contextualMapper;
		size_t batchSize = 0;
		size_t causalContextSize = 0;
		size_t windowSize = 0;
		size_t currentSeed = 0;
		size_t consumedSents = 0;
		bool sampleWithoutWeights = false;

		template<class InTy, class OutTy>
		size_t _next(InTy in, OutTy out);

	public:
		ChrDataset(size_t _batchSize = 0,
			size_t _causalContextSize = 0,
			size_t _windowSize = 0,
			float _prefixDropoutProb = 0.f,
			bool _sampleWithoutWeights = false,
			const std::vector<std::pair<size_t, std::vector<uint32_t>>>& contextualMapper = {}
		);
		~ChrDataset();
		ChrDataset(const ChrDataset&) = delete;
		ChrDataset(ChrDataset&&) /*noexcept*/;
		ChrDataset& operator=(const ChrDataset&) = delete;
		ChrDataset& operator=(ChrDataset&&) /*noexcept*/;

		void addSentence(std::string_view sentence, float weight = 1.f, std::string_view nonLabelPrefix = {}, bool reverse = false);

		size_t numSents() const;
		
		double getTotalWeight() const { return totalWeight; }
		size_t getBatchSize() const { return batchSize; }
		size_t getCausalContextSize() const { return causalContextSize; }
		size_t getWindowSize() const { return windowSize; }
		size_t vocabSize() const { return ChrTokenizer{}.vocabSize(); }
		std::vector<float> getVocabProbs(double epsilon = 0.1) const;

		void seed(size_t newSeed);
		void reset();
		size_t next(int32_t* in, int32_t* out);
		size_t next(int64_t* in, int64_t* out);

		std::vector<std::pair<std::vector<uint32_t>, double>> extractPrefixes(float resolution, float minWeight, size_t maxLength, 
			size_t numWorkers = 1, 
			bool exclusiveCnt = false,
			const std::vector<std::pair<uint32_t, uint32_t>>* mergeTargets = nullptr) const;
	};

	class BpeTokenizer;

	/**
	* posTagTokenIds는 POSTag 값으로 인덱싱한다. 불규칙 활용 태그(POSTag::vvi 등)의 자리가 0이면 규칙 활용 태그의 값을 쓴다.
	*/
	struct GenerativeMAOption
	{
		uint32_t toMorphemeTokenId = 0;
		uint32_t toSurfaceTokenId = 0;
		uint32_t bosTokenId = 0;
		uint32_t eosTokenId = 0;
		/** ToMorpheme 방향의 원문에서 오타가 발생할 수 있는 지점마다 오타를 넣을 확률 */
		float typoProb = 0;
		/** 넣을 오타 하나의 비용 상한 */
		float typoCostThreshold = 2.5f;
		/** 한 지점의 오타 후보를 exp(-typoCostScale * cost)에 비례하는 확률로 고른다. 0이면 균등하게 고른다. */
		float typoCostScale = 1;
		/** ToMorpheme 방향의 원문에서 어절 사이의 공백을 지울 확률 */
		float spaceRemoveProb = 0;
		/** ToMorpheme 방향의 원문에서 공백이 아닌 두 글자 사이에 공백을 넣을 확률 */
		float spaceInsertProb = 0;
		std::array<uint32_t, 256> posTagTokenIds = { { 0, } };
	};

	/**
	* 임의의 한국어 문장이 입력되면 생성형 형태소분석 스타일로 변환하여 데이터셋을 구성하는 클래스
	* 예를 들어 '이것은 분석기입니다.' 라는 문장이 입력되면 '이것/NP 은/JX 분석/NNG 기/NNG 이/VCP ㅂ니다/EF ./SF'로 형태소 분석한 뒤
	* 다음과 같이 한 쌍의 번역 데이터셋으로 변환한다.
	* 
	* 이것은 분석기입니다. <|ToMorpheme|> 이것/NP 은/JX 분석/NNG 기/NNG 이/VCP ㅂ니다/EF ./SF
	* 이것/NP 은/JX 분석/NNG 기/NNG 이/VCP ㅂ니다/EF ./SF <|ToSurface|> 이것은 분석기입니다.
	* 
	* 이렇게 1개의 입력 문장은 총 2개의 데이터를 만들어내며, 각 데이터는 BpeTokenizer에 의해 encoding되어 int 배열로 리턴된다.
	*
	* 실제 토큰열은 다음과 같으며, 형태소마다 형태 뒤에 품사 태그 토큰 하나를 붙인다.
	*
	*     [bos] encode(원문) [toMorpheme] (encode(형태) [태그])... [eos]
	*     [bos] (encode(형태) [태그])... [toSurface] encode(원문) [eos]
	*
	* 오타(`typoProb`)와 띄어쓰기 오류(`spaceRemoveProb`, `spaceInsertProb`)은 ToMorpheme 방향의 원문에만 넣으며,
	* 오타를 먼저 넣고 띄어쓰기를 흐트러뜨린다. 형태소열과 ToSurface 방향의 원문은 항상 깨끗하다.
	*
	* maxSeqLength를 넘는 부분은 뒤에서 잘리고(따라서 [eos]도 사라진다), 남는 자리는 `padToken`으로 채운다.
	*
	* `KiwiBuilder::makeGenerativeMADataset`으로 생성해야 한다.
	*/
	class GenerativeMADataset
	{
		friend class KiwiBuilder;

		struct ThreadLocal
		{
			std::u16string u16Buf, noisyBuf, spacedBuf, formsBuf;
			std::string textBuf, formBuf;
			std::vector<uint32_t> surfaceBuf, noisySurfaceBuf, morphemeBuf;
		};

		struct WorkItem
		{
			Vector<int32_t> data; /**< maxSeqLength 길이의 행들이 이어붙여진 버퍼 */
			size_t numRows = 0;
			size_t numTruncatedSents = 0;
			size_t numTypos = 0;
			size_t numRemovedSpaces = 0;
			size_t numInsertedSpaces = 0;
		};

		std::shared_ptr<Kiwi> kiwiInst;
		std::shared_ptr<const BpeTokenizer> tokenizer;
		/** 오타를 넣지 않는 경우 nullptr */
		std::shared_ptr<const PreparedTypoTransformer> typoGenerator;
		std::unique_ptr<utils::ThreadPool> workers;
		HiddenMember<RaggedVector<char16_t>, sizeof(Vector<size_t>) * 2> sents;
		/** 데이터마다 형태소들의 형태를 이어붙인 것. 원문만 받은 데이터는 비어있다. */
		HiddenMember<RaggedVector<char16_t>, sizeof(Vector<size_t>) * 2> morphemeForms;
		/** 데이터마다 형태소별 (morphemeForms 안에서 형태가 끝나는 위치 << 8 | 품사). 비어있으면 원문을 형태소 분석해서 쓴다. */
		HiddenMember<RaggedVector<uint32_t>, sizeof(Vector<size_t>) * 2> morphemeInfos;
		Vector<ThreadLocal> locals;
		Vector<uint32_t> shuffledIdx;
		Deque<std::future<WorkItem>> futures;
		/** `consumedRows >= current.numRows`이면 다 쓴 작업 단위다. */
		WorkItem current;
		size_t consumedRows = 0;
		GenerativeMAOption option;
		std::mt19937_64 rng;
		size_t currentSeed = 0;
		size_t batchSize = 0;
		size_t maxSeqLength = 0;
		size_t passedSents = 0;
		size_t truncatedSents = 0;
		size_t insertedTypos = 0;
		size_t removedSpaces = 0;
		size_t insertedSpaces = 0;

		uint32_t tagTokenId(POSTag tag) const;
		size_t sentsPerWorkItem() const;
		void pushItem(std::u16string_view surface, std::u16string_view forms, const Vector<uint32_t>& infos);
		void appendMorpheme(std::vector<uint32_t>& out, std::string& buf, std::u16string_view form, POSTag tag, POSTag prevTag) const;
		/** `seed`는 주 스레드에서 정하므로 어느 워커가 처리하든 결과가 같다. */
		WorkItem buildWorkItem(size_t localId, size_t sentFirst, size_t sentLast, uint64_t seed);
		bool prepareMore();

		template<class Ty>
		size_t _next(Ty* inputIds);

	public:
		static constexpr int32_t padToken = -1;

		GenerativeMADataset(
			const BpeTokenizer& tokenizer,
			const GenerativeMAOption& option,
			size_t _batchSize = 0,
			size_t _maxSeqLength = 0,
			size_t _workers = 0,
			const TypoTransformer& _typos = {}
		);
		~GenerativeMADataset();
		GenerativeMADataset(const GenerativeMADataset&) = delete;
		GenerativeMADataset(GenerativeMADataset&&) /*noexcept*/;
		GenerativeMADataset& operator=(const GenerativeMADataset&) = delete;
		GenerativeMADataset& operator=(GenerativeMADataset&&) /*noexcept*/;

		void addSentence(std::string_view sentence);
		void addSentence(std::u16string_view sentence);

		/**
		* 형태소 분석된 말뭉치를 추가한다. 한 줄은 `어절\t형태1\t품사1\t형태2\t품사2...`이고,
		* 빈 줄 하나는 문장 경계, 빈 줄 두 개 이상은 문서 경계다.
		* 같은 문서의 연속된 문장은 행이 maxSeqLength를 넘지 않는 만큼 이어붙여 하나의 데이터로 만든다.
		* 어미 첫 글자 '아'는 Kiwi의 분석 결과에 맞추어 '어'로 바꾸고, NA처럼 알 수 없는 품사가 있는 문장은
		* 경고를 stderr로 출력하고 버린다.
		* @return 추가된 문장의 개수
		*/
		size_t addAnalyzedCorpus(std::istream& is);

		size_t numSents() const;
		size_t numEstimBatches() const;

		// 아래 개수들은 직전 `reset()` 이후의 누적값이다.
		size_t numTruncatedSents() const { return truncatedSents; }
		size_t numInsertedTypos() const { return insertedTypos; }
		size_t numRemovedSpaces() const { return removedSpaces; }
		size_t numInsertedSpaces() const { return insertedSpaces; }

		size_t getBatchSize() const { return batchSize; }
		size_t getMaxSeqLength() const { return maxSeqLength; }
		size_t vocabSize() const;

		void seed(size_t newSeed);
		void reset();

		/**
		* @param input_ids `batchSize * maxSeqLength` 크기의 버퍼
		* @return 채운 행의 개수. 0이면 epoch이 끝난 것이므로 `reset()`을 호출해야 한다.
		*/
		size_t next(int32_t* input_ids);
		size_t next(int64_t* input_ids);
	};
}
