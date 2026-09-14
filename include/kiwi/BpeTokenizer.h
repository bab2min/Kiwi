#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <iosfwd>
#include "Types.h"

namespace kiwi
{
	class Kiwi;

	struct MergeRule
	{
		uint32_t rank = 0;
		uint32_t newId = 0;
	};

	class BpeTokenizer 
	{
		std::vector<std::string> vocab;
		std::unordered_map<uint64_t, MergeRule> merges;

		// Open-addressed copy of `merges` that encode() probes; built once by the constructor.
		struct MergeSlot
		{
			uint64_t key;
			MergeRule rule;
		};
		std::vector<MergeSlot> mergeTable;
		size_t mergeTableMask = 0;

		bool addPrefixSpace = false;
		// Trained with useJamoAlphabet, so encode() must decompose Hangul too. Persisted as the "nfd_for_hangul" normalizer.
		bool nfdForHangul = false;

		template<class It>
		std::string decode(It first, It last, bool ignoreErrors = true) const;

		void buildMergeTable();
		const MergeRule* findMerge(uint32_t a, uint32_t b) const;

	public:
		BpeTokenizer() = default;
		BpeTokenizer(std::vector<std::string> vocab, std::unordered_map<uint64_t, MergeRule> merges, bool addPrefixSpace = false, bool nfdForHangul = false)
			: vocab(std::move(vocab)), merges(std::move(merges)), addPrefixSpace(addPrefixSpace), nfdForHangul(nfdForHangul)
		{
			buildMergeTable();
		}

		bool ready() const;

		void encode(std::vector<uint32_t>& out, const std::string& str, std::vector<std::pair<uint32_t, uint32_t>>* offset = nullptr, bool offsetInChrLevel = false) const;
		std::vector<uint32_t> encode(const std::string& str, std::vector<std::pair<uint32_t, uint32_t>>* offset = nullptr, bool offsetInChrLevel = false) const;

		std::string decode(const std::vector<uint32_t>& ids, bool ignoreErrors = true) const;
		std::string decode(const uint32_t* ids, size_t length, bool ignoreErrors = true) const;

		const std::vector<std::string>& getVocab() const { return vocab; }

		bool isNfdForHangul() const { return nfdForHangul; }

		std::ostream& save(std::ostream& ostr) const;
		static BpeTokenizer load(std::istream& istr);
	};

	enum class PretokenizeOption : uint8_t
	{
		none = 0,
		jClass = 1 << 0,
		eClass = 1 << 1,
		vcp = 1 << 2,
		xsv = 1 << 3,
		all = jClass | eClass | vcp | xsv,
	};

	enum class JamoAlphabet : uint8_t
	{
		none = 0,
		// BPE 학습 시 기본 알파벳에 현대 한글 자모를 추가함
		modern_only = 1,
		// 현대 한글 자모뿐만 아니라 옛 한글 자모까지 모두 추가함
		all = 2,
	};

	struct BpeTrainerConfig
	{
		size_t vocabSize = 0;
		size_t minPairFrequency = 5;
		size_t maxTokenLength = 64;
		bool addPrefixSpace = false;
		size_t maxDigitLength = 3;
		size_t maxRepeatLength = 8;
		size_t maxWhitespaceRepeatLength = 16;
		std::vector<std::string> additionalAlphabet;
		JamoAlphabet useJamoAlphabet = JamoAlphabet::none;
		PretokenizeOption pretokenizeOption = PretokenizeOption::none;
		// -1 selects std::thread::hardware_concurrency(), 0 disables threading, and any positive value is the number of threads to use.
		size_t numThreads = 0;
		size_t batchSize = 64;
		// 64-bit counters (16-byte slots) instead of 32-bit ones (8-byte slots).
		bool largeCounter = false;
	};

	enum class BpeTokenizerTrainerEvent
	{
		pretokenizeBegin,
		pretokenizeProgress,
		pretokenizeEnd,
		mergeBegin,
		mergeProgress,
		mergeEnd,
	};

	// Called as (event, current, total); `total` is 0 when it can't be known in advance.
	//   pretokenize*: from addSentences, `current` = sentences consumed so far.
	//   merge*: from build(), `current` = vocabulary size (starting at 256), `total` = vocabSize.
	// Always runs on the calling thread; an exception propagates out of that call.
	using BpeTokenizerTrainerEventCallback = std::function<void(BpeTokenizerTrainerEvent, size_t, size_t)>;

	class BpeTokenizerTrainer
	{
		BpeTrainerConfig config;
		const Kiwi* kiwi = nullptr;
		BpeTokenizerTrainerEventCallback callback;
		size_t sentenceCount = 0;
		struct Impl;
		std::unique_ptr<Impl> impl;

		template<bool> friend struct BpeTokenizerTrainerImpl;

	public:
		BpeTokenizerTrainer(const BpeTrainerConfig& config, const Kiwi* kiwi = nullptr, BpeTokenizerTrainerEventCallback callback = {});
		~BpeTokenizerTrainer();

		// Reads until `feeder` returns an empty string, so callers must filter out blank lines themselves.
		// Returns the number of sentences read.
		size_t addSentences(const std::function<std::string()>& feeder);
		size_t addSentences(const std::function<std::u16string()>& feeder);

		BpeTokenizer build() const;
	};
}

KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::PretokenizeOption);
