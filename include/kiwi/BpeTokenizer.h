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
	};

	class BpeTokenizerTrainer
	{
		BpeTrainerConfig config;
		std::unordered_map<std::string, size_t> wordCounts;
		size_t sentenceCount = 0;

	public:
		BpeTokenizerTrainer(const BpeTrainerConfig& config);

		size_t addSentences(const std::function<std::string()>& feeder);
		size_t addSentences(const std::function<std::u16string()>& feeder);

		BpeTokenizer build() const;
	};
}
