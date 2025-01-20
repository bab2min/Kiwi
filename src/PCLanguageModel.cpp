#include <iostream>
#include <fstream>
#include "PCLanguageModel.hpp"
#include "StrUtils.h"
#include "FrozenTrie.hpp"

using namespace std;

namespace kiwi
{
	namespace pclm
	{
		utils::MemoryObject PCLanguageModelBase::build(const string& contextDefinition, const string& embedding, bool reorderContextId)
		{
			ifstream contextStr, embeddingStr;
			if (!openFile(contextStr, contextDefinition))
			{
				throw IOException{ "Cannot open file : " + contextDefinition };
			}

			uint32_t maxClusterId = 0;
			using Node = utils::TrieNodeEx<uint32_t, uint32_t, utils::ConstAccess<Map<uint32_t, int32_t>>>;
			utils::ContinuousTrie<Node> trie(1);
			{
				Vector<UnorderedMap<Vector<uint32_t>, uint32_t>> contextMap;
				UnorderedMap<Vector<uint32_t>, uint32_t> erasedContexts;
				Vector<uint32_t> context;
				string line;
				while (getline(contextStr, line))
				{
					auto tokens = split(line, '\t');
					if (tokens.size() <= 1)
					{
						throw IOException{ "Invalid format : " + contextDefinition };
					}

					auto clusterId = stol(tokens[0].begin(), tokens[0].end());
					if (clusterId < 0) throw IOException{ "Invalid format : " + contextDefinition };
					context.clear();
					for (size_t i = 1; i < tokens.size(); ++i)
					{
						auto id = stol(tokens[i].begin(), tokens[i].end());
						if (id < 0) throw IOException{ "Invalid format : " + contextDefinition };
						context.push_back(id);
					}
					if (contextMap.size() < context.size()) contextMap.resize(context.size());
					contextMap[context.size() - 1][context] = (uint32_t)clusterId;
					maxClusterId = max(maxClusterId, (uint32_t)(clusterId + 1));
				}

				for (size_t i = contextMap.size(); i-- > 0;) // remove redundant context
				{
					auto& c = contextMap[i];
					for (auto it = c.begin(); it != c.end();)
					{
						bool erase = false;
						for (size_t j = i; j-- > 0; )
						{
							auto& c2 = contextMap[j];
							context.clear();
							context.insert(context.end(), it->first.end() - j - 1, it->first.end());
							auto found = c2.find(context);
							if (found != c2.end())
							{
								erase = found->second == it->second;
								break;
							}
						}

						if (erase)
						{
							if (it->first.size() < contextMap.size())
							{
								erasedContexts.emplace(it->first, it->second);
							}
							it = c.erase(it);
						}
						else ++it;
					}
				}

				for (auto& c : contextMap)
				{
					for (auto& p : c)
					{
						trie.build(p.first.begin(), p.first.end(), p.second + 1);
					}
				}
			}

			Vector<uint32_t> nodeSizes;
			nodeSizes.reserve(trie.size());
			Vector<uint32_t> keys;
			keys.reserve(trie.size());
			Vector<uint32_t> values;
			values.reserve(trie.size());
			Vector<uint32_t> valueNewIdx(maxClusterId + 1);
			{
				Vector<size_t> valueCnts(valueNewIdx.size());
				Vector<uint32_t> valueArgsorted(valueNewIdx.size());
				Vector<uint32_t> rkeys;
				trie.traverseWithKeys([&](const Node* node, const Vector<uint32_t>& rkeys)
				{
					nodeSizes.emplace_back(node->next.size());
					for (auto& p : node->next)
					{
						keys.emplace_back(p.first);
					}
					values.emplace_back(node->val);
					valueCnts[node->val]++;
				}, rkeys);

				valueCnts[0] = -1;

				// remap value idx by frequency
				if (reorderContextId)
				{
					iota(valueArgsorted.begin(), valueArgsorted.end(), 0);
					sort(valueArgsorted.begin(), valueArgsorted.end(), [&](uint32_t a, uint32_t b) { return valueCnts[a] > valueCnts[b]; });
					for (size_t i = 0; i < valueArgsorted.size(); ++i)
					{
						valueNewIdx[valueArgsorted[i]] = (uint32_t)i;
					}
					for (auto& v : values) v = valueNewIdx[v];
				}
			}

			assert(nodeSizes.size() - 1 == keys.size());

			Vector<uint8_t> compressedNodeSizes(streamvbyte_max_compressedbytes(nodeSizes.size()));
			compressedNodeSizes.resize(streamvbyte_encode_0124(nodeSizes.data(), nodeSizes.size(), compressedNodeSizes.data()));
			Vector<uint8_t> compressedValues(streamvbyte_max_compressedbytes(values.size()));
			compressedValues.resize(streamvbyte_encode_0124(values.data(), values.size(), compressedValues.data()));
			Vector<uint8_t> compressedKeys(streamvbyte_max_compressedbytes(keys.size()));
			compressedKeys.resize(streamvbyte_encode(keys.data(), keys.size(), compressedKeys.data()));

			if (!openFile(embeddingStr, embedding, ios_base::binary))
			{
				throw IOException{ "Cannot open file : " + embedding };
			}
			const uint32_t dim = utils::read<uint32_t>(embeddingStr);
			const uint32_t contextSize = utils::read<uint32_t>(embeddingStr);
			const uint32_t outputSize = utils::read<uint32_t>(embeddingStr);
			const uint32_t windowSize = utils::read<uint32_t>(embeddingStr);

			Vector<int8_t> contextEmb(dim * contextSize);
			Vector<uint16_t> contextEmbScale(contextSize);
			Vector<uint16_t> contextEmbBias(contextSize);
			Vector<uint16_t> contextValidTokenSum(contextSize);
			Vector<uint16_t> contextConfidence(contextSize);
			Vector<int8_t> distantEmb(dim * outputSize);
			Vector<uint16_t> distantEmbScale(outputSize);
			Vector<uint16_t> distantEmbBias(outputSize);
			Vector<uint16_t> distantConfidence(outputSize);
			vector<uint16_t> positionConfidence(windowSize);
			Vector<int8_t> outputEmb(dim * outputSize);
			Vector<uint16_t> outputEmbScale(outputSize);
			Vector<uint8_t> distantMask(outputSize);

			embeddingStr.read((char*)contextEmb.data(), contextEmb.size());
			embeddingStr.read((char*)contextEmbScale.data(), contextEmbScale.size() * sizeof(uint16_t));
			embeddingStr.read((char*)contextEmbBias.data(), contextEmbBias.size() * sizeof(uint16_t));
			embeddingStr.read((char*)contextValidTokenSum.data(), contextValidTokenSum.size() * sizeof(uint16_t));
			embeddingStr.read((char*)contextConfidence.data(), contextConfidence.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantEmb.data(), distantEmb.size());
			embeddingStr.read((char*)distantEmbScale.data(), distantEmbScale.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantEmbBias.data(), distantEmbBias.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantConfidence.data(), distantConfidence.size() * sizeof(uint16_t));
			embeddingStr.read((char*)positionConfidence.data(), positionConfidence.size() * sizeof(uint16_t));
			embeddingStr.read((char*)outputEmb.data(), outputEmb.size());
			embeddingStr.read((char*)outputEmbScale.data(), outputEmbScale.size() * sizeof(uint16_t));
			embeddingStr.read((char*)distantMask.data(), distantMask.size());

			// remap context embedding
			if (reorderContextId)
			{
				Vector<int8_t> newContextEmb(contextEmb.size());
				Vector<uint16_t> newContextEmbScale(contextSize);
				Vector<uint16_t> newContextEmbBias(contextSize);
				Vector<uint16_t> newContextValidTokenSum(contextSize);
				for (size_t i = 0; i < contextSize; ++i)
				{
					auto idx = valueNewIdx[i];
					auto src = contextEmb.data() + i * dim;
					auto dst = newContextEmb.data() + idx * dim;
					copy(src, src + dim, dst);
					newContextEmbScale[idx] = contextEmbScale[i];
					newContextEmbBias[idx] = contextEmbBias[i];
					newContextValidTokenSum[idx] = contextValidTokenSum[i];
				}
				contextEmb = move(newContextEmb);
				contextEmbScale = move(newContextEmbScale);
				contextEmbBias = move(newContextEmbBias);
				contextValidTokenSum = move(newContextValidTokenSum);
			}

			// compress distantMask into bits
			const size_t compressedDistantMaskSize = (outputSize + 7) / 8;
			{
				for (size_t i = 0; i < outputSize; ++i)
				{
					if (i % 8 == 0)
					{
						distantMask[i / 8] = distantMask[i];
					}
					else
					{
						distantMask[i / 8] |= distantMask[i] << (i % 8);
					}
				}
				distantMask.resize(compressedDistantMaskSize);
			}

			Header header;
			memset(&header, 0, sizeof(Header));
			header.dim = dim;
			header.contextSize = contextSize;
			header.vocabSize = outputSize;
			header.keySize = 4;
			header.windowSize = windowSize;
			header.numNodes = nodeSizes.size();

			size_t finalSize = 0;
			header.nodeOffset = alignedOffsetInc(finalSize, sizeof(Header));
			header.keyOffset = alignedOffsetInc(finalSize, compressedNodeSizes.size());
			header.valueOffset = alignedOffsetInc(finalSize, compressedKeys.size());
			header.embOffset = alignedOffsetInc(finalSize, compressedValues.size());
			finalSize += dim * (contextSize + outputSize * 2);
			finalSize += contextSize * sizeof(uint16_t) * 4;
			finalSize += outputSize * sizeof(uint16_t) * 4;
			finalSize += windowSize * sizeof(uint16_t);
			finalSize += compressedDistantMaskSize;

			utils::MemoryOwner mem{ finalSize };
			utils::omstream ostr{ (char*)mem.get(), (std::ptrdiff_t)mem.size() };
			ostr.write((const char*)&header, sizeof(Header));
			writePadding(ostr);
			ostr.write((const char*)compressedNodeSizes.data(), compressedNodeSizes.size());
			writePadding(ostr);
			ostr.write((const char*)compressedKeys.data(), compressedKeys.size());
			writePadding(ostr);
			ostr.write((const char*)compressedValues.data(), compressedValues.size());
			writePadding(ostr);
			ostr.write((const char*)contextEmb.data(), contextEmb.size());
			ostr.write((const char*)contextEmbScale.data(), contextEmbScale.size() * sizeof(uint16_t));
			ostr.write((const char*)contextEmbBias.data(), contextEmbBias.size() * sizeof(uint16_t));
			ostr.write((const char*)contextValidTokenSum.data(), contextValidTokenSum.size() * sizeof(uint16_t));
			ostr.write((const char*)contextConfidence.data(), contextConfidence.size() * sizeof(uint16_t));
			ostr.write((const char*)distantEmb.data(), distantEmb.size());
			ostr.write((const char*)distantEmbScale.data(), distantEmbScale.size() * sizeof(uint16_t));
			ostr.write((const char*)distantEmbBias.data(), distantEmbBias.size() * sizeof(uint16_t));
			ostr.write((const char*)distantConfidence.data(), distantConfidence.size() * sizeof(uint16_t));
			ostr.write((const char*)positionConfidence.data(), positionConfidence.size() * sizeof(uint16_t));
			ostr.write((const char*)outputEmb.data(), outputEmb.size());
			ostr.write((const char*)outputEmbScale.data(), outputEmbScale.size() * sizeof(uint16_t));
			ostr.write((const char*)distantMask.data(), distantMask.size());
			return mem;
		}

		template<ArchType archType, class KeyTy, bool useDistantTokens>
		inline std::unique_ptr<PCLanguageModelBase> createOptimizedModelWithWindowSize(utils::MemoryObject&& mem)
		{
			auto& header = *reinterpret_cast<const Header*>(mem.get());
			switch (header.windowSize)
			{
			case 4:
				return make_unique<PCLanguageModel<archType, KeyTy, 4, true, useDistantTokens>>(std::move(mem));
			case 7:
				return make_unique<PCLanguageModel<archType, KeyTy, 7, true, useDistantTokens>>(std::move(mem));
			case 8:
				return make_unique<PCLanguageModel<archType, KeyTy, 8, false, useDistantTokens>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `window_size` : " + std::to_string((size_t)header.windowSize) };
			};
		}

		template<ArchType archType, bool useDistantTokens>
		std::unique_ptr<PCLanguageModelBase> createOptimizedModel(utils::MemoryObject&& mem)
		{
			auto& header = *reinterpret_cast<const Header*>(mem.get());
			switch (header.keySize)
			{
			case 1:
				return createOptimizedModelWithWindowSize<archType, uint8_t, useDistantTokens>(std::move(mem));
			case 2:
				return createOptimizedModelWithWindowSize<archType, uint16_t, useDistantTokens>(std::move(mem));
			case 4:
				return createOptimizedModelWithWindowSize<archType, uint32_t, useDistantTokens>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.keySize) };
			}
		}

		using FnCreateOptimizedModel = decltype(&createOptimizedModel<ArchType::none, false>);

		template<bool useDistantTokens>
		struct CreateOptimizedModelGetter
		{
			template<std::ptrdiff_t i>
			struct Wrapper
			{
				static constexpr FnCreateOptimizedModel value = &createOptimizedModel<static_cast<ArchType>(i), useDistantTokens>;
			};
		};

		std::unique_ptr<PCLanguageModelBase> PCLanguageModelBase::create(utils::MemoryObject&& mem, ArchType archType, bool useDistantTokens)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> tableWithoutDistantTokens{ CreateOptimizedModelGetter<false>{} },
				tableWithDistantTokens{ CreateOptimizedModelGetter<true>{} };
			auto fn = (useDistantTokens ? tableWithDistantTokens : tableWithoutDistantTokens)[static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(mem));
		}
	}
}
