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
		bool addPrefixSpace = false;

		template<class It>
		std::string decode(It first, It last, bool ignoreErrors = true) const;

	public:
		BpeTokenizer() = default;
		BpeTokenizer(std::vector<std::string> vocab, std::unordered_map<uint64_t, MergeRule> merges, bool addPrefixSpace = false)
			: vocab(std::move(vocab)), merges(std::move(merges)), addPrefixSpace(addPrefixSpace) {}

		bool ready() const;

		void encode(std::vector<uint32_t>& out, const std::string& str, std::vector<std::pair<uint32_t, uint32_t>>* offset = nullptr, bool offsetInChrLevel = false) const;
		std::vector<uint32_t> encode(const std::string& str, std::vector<std::pair<uint32_t, uint32_t>>* offset = nullptr, bool offsetInChrLevel = false) const;

		std::string decode(const std::vector<uint32_t>& ids, bool ignoreErrors = true) const;
		std::string decode(const uint32_t* ids, size_t length, bool ignoreErrors = true) const;

		const std::vector<std::string>& getVocab() const { return vocab; }

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

	struct BpeTrainerConfig
	{
		size_t vocabSize = 0;
		size_t minPairFrequency = 5;
		// A value of numeric_limits<size_t>::max() means no length limit.
		size_t maxTokenLength = std::numeric_limits<size_t>::max();
		bool addPrefixSpace = false;
		// Caps how many consecutive digits may share one chunk, so that long digit runs
		// do not spend vocabulary slots on specific numbers ("2019", "100000").  Only
		// digits count toward the limit, not the space a chunk may start with, so
		// " 12345" with a cap of 3 becomes " 123" + "45".  Zero means no limit.
		//
		// This needs no counterpart at encode time: no training chunk ever holds more
		// than this many digits, so no merge rule can produce a longer digit token.
		// Note that the cap bounds token length but not where an unseen number is cut,
		// which is decided by merge ranks — "1234" may come out as "12"+"34" there.
		size_t maxDigitLength = 0;
		// Byte strings pinned into the initial alphabet.  Each one is made reachable by
		// merge rules holding the lowest ranks, so it is rebuilt from its bytes before
		// any learned merge applies and can never come out split — which is what keeps
		// a multi-byte character such as a Hangul jamo atomic.
		//
		// An entry of n bytes also pins one token per proper prefix, but prefixes are
		// shared between entries: the 51 compatibility jamo cost 2 extra tokens between
		// them.  Every pinned token counts toward vocabSize, and the order of this list
		// decides the ranks the pinned merges get.
		std::vector<std::string> additionalAlphabet;
		// Trains on Hangul decomposed into 첫가끝 conjoining jamo: once the chunks are
		// pre-tokenized, every modern syllable (U+AC00..U+D7A3) is rewritten as its
		// 초성/중성/종성 before being counted, and all 67 conjoining jamo are pinned into
		// the initial alphabet the way additionalAlphabet entries are.  An unseen
		// syllable then falls back to jamo instead of to raw bytes.  Costs 71 tokens
		// (67 jamo plus the four two-byte prefixes they share).
		//
		// This changes what the model is trained on, so it is NOT self-contained the way
		// maxDigitLength is: the caller must apply the same decomposition before encode()
		// and recompose after decode(), since neither does it.
		bool useJamoAlphabet = false;
		PretokenizeOption pretokenizeOption = PretokenizeOption::none;
		// -1 selects std::thread::hardware_concurrency(), 0 disables threading, and any positive value is the number of threads to use.
		size_t numThreads = 0;
		// Limits how many input sentences are retained while collecting chunks.
		size_t batchSize = 1024;
		// Use 64-bit per-chunk counters (16-byte slots) instead of the default
		// 32-bit counters (8-byte slots).  Enable when a single chunk may appear
		// more than ~4 billion times across the entire corpus, or when the total
		// size of the distinct chunks exceeds 4 GB.  Exceeding either limit with
		// largeCounter=false makes addSentences throw rather than wrap silently.
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

	// Called as (event, current, total).  `total` is zero when it cannot be known
	// in advance, so a consumer must treat zero as "indeterminate" rather than
	// dividing by it.
	//
	//   pretokenize*  emitted by addSentences, one `pretokenizeProgress` per batch.
	//                 `current` counts the sentences consumed so far by this call;
	//                 `total` is always zero, since the feeder announces no length.
	//   merge*        emitted by build().  `current` is the vocabulary size, which
	//                 starts at 256 (the byte alphabet), and `total` is the
	//                 configured vocabSize, or zero when it is unbounded.
	//                 `mergeProgress` is throttled to roughly 1000 events.
	//
	// Both `*End` events report `current == total` with the value actually reached,
	// which may fall short of the configured target if the corpus runs out of
	// mergeable pairs first.
	//
	// The callback always runs on the thread that called addSentences/build, never
	// on a worker.  An exception thrown from it propagates out of that call.
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

		// Consumes sentences from `feeder` until it returns an empty string, which is
		// the end-of-input sentinel; `feeder` is not called again afterwards.  A blank
		// line in the middle of a corpus therefore terminates collection, so callers
		// must filter empty lines out themselves.  Returns the number of sentences
		// consumed (excluding the terminating sentinel).
		size_t addSentences(const std::function<std::string()>& feeder);
		size_t addSentences(const std::function<std::u16string()>& feeder);

		BpeTokenizer build() const;
	};
}

KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::PretokenizeOption);
