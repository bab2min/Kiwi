#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>

#include "ArchUtils.h"
#include "Mmap.h"
#include "LangModel.h"

namespace kiwi
{
	namespace lm
	{
		struct CoNgramModelHeader
		{
			enum
			{
				hasOutputEmbBias = 1 << 0,
				hasReorderedVocab = 1 << 1,
				hasTrieFrequency = 1 << 2,
			};

			uint64_t vocabSize, contextSize;
			uint16_t dim;
			uint16_t flags;
			uint8_t keySize, windowSize, qbit, qgroup;
			uint64_t numNodes;
			uint64_t nodeOffset, keyOffset, valueOffset, embOffset;
		};

		template<class KeyType, class ValueType, class DiffType = int32_t>
		struct Node
		{
			KeyType numNexts = 0;
			ValueType value = 0;
			DiffType lower = 0;
			uint32_t nextOffset = 0;
		};

		template<>
		struct Node<uint16_t, uint32_t, int32_t>
		{
			uint16_t numNexts = 0;
			uint16_t depth = 0;
			uint32_t value = 0;
			int32_t lower = 0;
			uint32_t nextOffset = 0;
		};

		template<class T>
		struct HasDepthField : public std::false_type
		{
		};

		template<>
		struct HasDepthField<Node<uint16_t, uint32_t, int32_t>> : public std::true_type
		{
		};
		

		class CoNgramModelBase : public ILangModel
		{
		protected:
			const size_t memorySize = 0;
			CoNgramModelHeader header;
			mutable std::vector<std::vector<uint32_t>> contextWordMapCache;

			CoNgramModelBase(const utils::MemoryObject& mem) : memorySize{ mem.size() }, header{ *reinterpret_cast<const CoNgramModelHeader*>(mem.get()) }
			{
			}
		public:
			virtual ~CoNgramModelBase() {}
			size_t vocabSize() const override { return header.vocabSize; }
			size_t getMemorySize() const override { return memorySize; }

			const CoNgramModelHeader& getHeader() const { return header; }

			virtual size_t mostSimilarWords(uint32_t vocabId, size_t topN, std::pair<uint32_t, float>* output) const = 0;
			virtual float wordSimilarity(uint32_t vocabId1, uint32_t vocabId2) const = 0;

			virtual size_t mostSimilarContexts(uint32_t contextId, size_t topN, std::pair<uint32_t, float>* output) const = 0;
			virtual float contextSimilarity(uint32_t contextId1, uint32_t contextId2) const = 0;

			virtual size_t predictWordsFromContext(uint32_t contextId, size_t topN, std::pair<uint32_t, float>* output) const = 0;
			virtual size_t predictWordsFromContextDiff(uint32_t contextId, uint32_t bgContextId, float weight, size_t topN, std::pair<uint32_t, float>* output) const = 0;

			virtual uint32_t toContextId(const uint32_t* vocabIds, size_t size) const = 0;
			virtual float getContextFrequency(uint32_t contextId) const = 0;
			virtual size_t getNodeDepth(uint32_t nodeId) const = 0;

			virtual std::vector<std::vector<uint32_t>> getContextWordMap() const = 0;
			virtual float progressOneStep(int32_t& nodeIdx, uint32_t& contextIdx, uint32_t next) const = 0;

			const std::vector<std::vector<uint32_t>>& getContextWordMapCached() const
			{
				if (contextWordMapCache.empty())
				{
					contextWordMapCache = getContextWordMap();
				}
				return contextWordMapCache;
			}

			static utils::MemoryObject build(const std::string& contextDefinition, const std::string& embedding, 
				size_t maxContextLength = -1, 
				bool useVLE = true, 
				bool reorderContextIdx = true,
				const std::vector<size_t>* selectedEmbIdx = nullptr);

			static utils::MemoryObject buildChrModel(const std::string& contextDefinition, const std::string& embedding,
				size_t maxContextLength = -1, bool reorderContextIdx = true, bool eraseRedundantContexts = false);

			static std::unique_ptr<CoNgramModelBase> create(utils::MemoryObject&& mem, 
				ArchType archType = ArchType::none, 
				bool useDistantTokens = false, 
				bool quantized = true);
		};
	}
}
