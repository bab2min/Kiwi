#pragma once

#include <vector>
#include <string>

#include <kiwi/FrozenTrie.h>
#include <kiwi/Knlm.h>

namespace kiwi
{
	std::vector<std::pair<std::u16string, size_t>> extractSubstrings(
		const char16_t* first, 
		const char16_t* last, 
		size_t minCnt, 
		size_t minLength = 2, 
		size_t maxLength = 32,
		bool longestOnly = true,
		char16_t stopChr = 0);


	class PrefixCounter
	{
		size_t prefixSize = 0, minCf = 0, numArrays = 0;
		UnorderedMap<uint32_t, uint32_t> token2id;
		Vector<uint32_t> id2Token;
		Vector<uint16_t> buf;
		Vector<size_t> tokenClusters;
		Vector<size_t> tokenCnts;
		std::shared_ptr<void> threadPool;

		template<class It>
		void _addArray(It first, It last);

		Vector<std::pair<uint32_t, float>> computeClusterScore() const;

	public:
		PrefixCounter(size_t _prefixSize, size_t _minCf, size_t _numWorkers,
			const std::vector<std::vector<size_t>>& clusters = {}
			);
		void addArray(const uint16_t* first, const uint16_t* last);
		void addArray(const uint32_t* first, const uint32_t* last);
		void addArray(const uint64_t* first, const uint64_t* last);
		utils::FrozenTrie<uint32_t, uint32_t> count() const;
		std::unique_ptr<lm::KnLangModelBase> buildLM(
			const std::vector<size_t>& minCfByOrder, 
			size_t bosTokenId,
			size_t eosTokenId,
			size_t unkTokenId,
			ArchType archType = ArchType::none
		) const;
	};

	class ClusterData
	{
		const std::pair<uint32_t, float>* clusterScores = nullptr;
		size_t clusterSize = 0;
	public:
		ClusterData();
		ClusterData(const void* _ptr, size_t _size);

		size_t size() const;
		size_t cluster(size_t i) const;
		float score(size_t i) const;
	};

	class Kiwi;

	class NgramExtractor
	{
		const Kiwi* kiwi = nullptr;
		bool gatherLmScore = true;
		UnorderedMap<std::u16string, size_t> morph2id;
		Vector<std::u16string> id2morph;
		Vector<uint16_t> buf;
		Vector<int16_t> scores;
		Vector<size_t> docBoundaries;
		Vector<uint32_t> positions;
		Vector<std::u16string> rawDocs;

		size_t addTokens(const std::vector<TokenInfo>& tokens);

	public:
		struct Candidate
		{
			std::u16string text;
			std::vector<std::u16string> tokens;
			std::vector<float> tokenScores;
			size_t cnt = 0;
			size_t df = 0;
			float score = 0;
			float npmi = 0;
			float leftBranch = 0;
			float rightBranch = 0;
			float lmScore = 0;
		};

		NgramExtractor();
		NgramExtractor(const Kiwi& kiwi, bool gatherLmScore = true);
		NgramExtractor(const NgramExtractor&);
		NgramExtractor(NgramExtractor&&) noexcept;
		NgramExtractor& operator=(const NgramExtractor&);
		NgramExtractor& operator=(NgramExtractor&&) noexcept;
		~NgramExtractor();

		size_t addText(const std::u16string& text);
		size_t addTexts(const U16Reader& reader);

		std::vector<Candidate> extract(size_t maxCandidates = 1000, size_t minCnt = 10, size_t maxLength = 5, float minScore = 1e-3, size_t numWorkers = 1) const;
	};
}
