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

	struct BpeTrainerConfig
	{
		size_t vocabSize = 0;
		size_t minPairFrequency = 5;
		// A value of numeric_limits<size_t>::max() means no length limit.
		size_t maxTokenLength = std::numeric_limits<size_t>::max();
		bool addPrefixSpace = false;
		// Zero selects std::thread::hardware_concurrency().
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

	class BpeTokenizerTrainer
	{
		BpeTrainerConfig config;
		size_t sentenceCount = 0;
		struct Impl;
		std::unique_ptr<Impl> impl;

		template<bool> friend struct BpeTokenizerTrainerImpl;

	public:
		BpeTokenizerTrainer(const BpeTrainerConfig& config);
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
