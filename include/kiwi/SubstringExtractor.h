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
}
