#include <iostream>
#include <fstream>
#include "PathEvaluator.hpp"
#include "Joiner.hpp"
#include "Kiwi.hpp"
#include "PCLanguageModel.hpp"
#include "StrUtils.h"
#include "FrozenTrie.hpp"

using namespace std;

namespace kiwi
{
	namespace lm
	{
		inline float half2float(uint16_t h)
		{
			union
			{
				uint32_t i;
				float f;
			} u;
			u.i = (uint32_t)(h & 0x8000) << 16;
			u.i |= ((uint32_t)(h & 0x7FFF) + 0x1C000) << 13;
			return u.f;
		}

		inline void dequantize(float* out, const int8_t* ints, size_t n, float scale)
		{
			for (size_t i = 0; i < n; ++i)
			{
				out[i] = ints[i] * scale;
			}
		}

		template<class Arr>
		void logsoftmaxInplace(Arr& arr)
		{
			arr -= arr.maxCoeff();
			arr -= std::log(arr.exp().sum());
		}

		template<ArchType arch, class KeyType, size_t windowSize, bool exclusive, bool useDistantTokens>
		PcLangModel<arch, KeyType, windowSize, exclusive, useDistantTokens>::PcLangModel(utils::MemoryObject&& mem) : PcLangModelBase{ std::move(mem) }
		{
			auto* ptr = reinterpret_cast<const char*>(base.get());
			auto& header = getHeader();

			Vector<uint32_t> nodeSizes(header.numNodes);
			streamvbyte_decode_0124(reinterpret_cast<const uint8_t*>(ptr + header.nodeOffset), nodeSizes.data(), header.numNodes);
			keyData = make_unique<KeyType[]>(header.numNodes - 1);
			if (std::is_same<KeyType, uint32_t>::value)
			{
				streamvbyte_decode(reinterpret_cast<const uint8_t*>(ptr + header.keyOffset), (uint32_t*)keyData.get(), header.numNodes - 1);
			}
			else
			{
				Vector<uint32_t> tempKeyData(header.numNodes - 1);
				streamvbyte_decode(reinterpret_cast<const uint8_t*>(ptr + header.keyOffset), tempKeyData.data(), header.numNodes - 1);
				std::copy(tempKeyData.begin(), tempKeyData.end(), keyData.get());
			}
			Vector<uint32_t> values(header.numNodes);
			streamvbyte_decode_0124(reinterpret_cast<const uint8_t*>(ptr + header.valueOffset), values.data(), header.numNodes);

			size_t numNonLeafNodes = 0, numLeafNodes = 0;
			for (size_t i = 0; i < header.numNodes; ++i)
			{
				if (nodeSizes[i]) numNonLeafNodes++;
				else numLeafNodes++;
			}

			nodeData = make_unique<MyNode[]>(numNonLeafNodes);
			valueData = make_unique<int32_t[]>(header.numNodes - 1);

			size_t nonLeafIdx = 0, leafIdx = 0, nextOffset = 0;
			Vector<std::array<size_t, 3>> keyRanges;
			for (size_t i = 0; i < header.numNodes; ++i)
			{
				if (nodeSizes[i])
				{
					auto& node = nodeData[nonLeafIdx];
					if (!keyRanges.empty())
					{
						auto& back = keyRanges.back();
						valueData[back[1]] = nonLeafIdx - back[0];
					}
					node.value = values[i];
					node.numNexts = nodeSizes[i];
					node.nextOffset = nextOffset;
					nextOffset += nodeSizes[i];
					keyRanges.emplace_back(std::array<size_t, 3>{ nonLeafIdx, (size_t)node.nextOffset, (size_t)(node.nextOffset + node.numNexts) });
					nonLeafIdx++;
				}
				else
				{
					auto& back = keyRanges.back();
					valueData[back[1]] = -(int32_t)values[i];
					back[1]++;
					while (keyRanges.back()[1] == keyRanges.back()[2])
					{
						keyRanges.pop_back();
						if (keyRanges.empty()) break;
						keyRanges.back()[1]++;
					}
					leafIdx++;
				}
			}

			Vector<uint8_t> tempBuf;
			for (size_t i = 0; i < nonLeafIdx; ++i)
			{
				auto& node = nodeData[i];
				nst::prepare<arch>(&keyData[node.nextOffset], &valueData[node.nextOffset], node.numNexts, tempBuf);
			}

			Deque<MyNode*> dq;
			for (dq.emplace_back(&nodeData[0]); !dq.empty(); dq.pop_front())
			{
				auto p = dq.front();
				for (size_t i = 0; i < p->numNexts; ++i)
				{
					auto k = keyData[p->nextOffset + i];
					auto v = valueData[p->nextOffset + i];
					if (v <= 0) continue;
					auto* child = &p[v];
					child->lower = findLowerNode(p, k) - child;
					if (child->value == 0)
					{
						child->value = findLowerValue(p, k);
					}
					dq.emplace_back(child);
				}
			}

			auto* eptr = ptr + header.embOffset;
			contextEmb = make_unique<float[]>(header.contextSize * header.dim);
			contextBias = make_unique<float[]>(header.contextSize);
			contextValidTokenSum = make_unique<float[]>(header.contextSize);
			contextConf = make_unique<float[]>(header.contextSize);
			if (useDistantTokens)
			{
				distantEmb = make_unique<float[]>(header.vocabSize * header.dim);
				distantBias = make_unique<float[]>(header.vocabSize);
				distantConf = make_unique<float[]>(header.vocabSize);
				positionConf = make_unique<float[]>(header.windowSize);
			}
			outputEmb = make_unique<float[]>(header.vocabSize * header.dim);

			const uint16_t* contextEmbScale = reinterpret_cast<const uint16_t*>(eptr + header.contextSize * header.dim);
			for (size_t i = 0; i < header.contextSize; ++i)
			{
				dequantize(&contextEmb[i * header.dim], reinterpret_cast<const int8_t*>(eptr), header.dim, half2float(contextEmbScale[i]));
				eptr += header.dim;
			}
			eptr += header.contextSize * sizeof(uint16_t);
			for (size_t i = 0; i < header.contextSize; ++i)
			{
				contextBias[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
				eptr += sizeof(uint16_t);
			}
			for (size_t i = 0; i < header.contextSize; ++i)
			{
				contextValidTokenSum[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
				eptr += sizeof(uint16_t);
			}
			for (size_t i = 0; i < header.contextSize; ++i)
			{
				contextConf[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
				eptr += sizeof(uint16_t);
			}

			const uint16_t* distantEmbScale = reinterpret_cast<const uint16_t*>(eptr + header.vocabSize * header.dim);
			for (size_t i = 0; i < header.vocabSize; ++i)
			{
				if (useDistantTokens) dequantize(&distantEmb[i * header.dim], reinterpret_cast<const int8_t*>(eptr), header.dim, half2float(distantEmbScale[i]));
				eptr += header.dim;
			}
			eptr += header.vocabSize * sizeof(uint16_t);
			for (size_t i = 0; i < header.vocabSize; ++i)
			{
				if (useDistantTokens) distantBias[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
				eptr += sizeof(uint16_t);
			}
			for (size_t i = 0; i < header.vocabSize; ++i)
			{
				if (useDistantTokens) distantConf[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
				eptr += sizeof(uint16_t);
			}
			for (size_t i = 0; i < header.windowSize; ++i)
			{
				if (useDistantTokens) positionConf[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
				eptr += sizeof(uint16_t);
			}

			const uint16_t* outputEmbScale = reinterpret_cast<const uint16_t*>(eptr + header.vocabSize * header.dim);
			for (size_t i = 0; i < header.vocabSize; ++i)
			{
				dequantize(&outputEmb[i * header.dim], reinterpret_cast<const int8_t*>(eptr), header.dim, half2float(outputEmbScale[i]));
				eptr += header.dim;
			}
			eptr += header.vocabSize * sizeof(uint16_t);

			if (useDistantTokens)
			{
				const size_t compressedDistantMaskSize = (header.vocabSize + 7) / 8;
				distantMask = make_unique<uint8_t[]>(compressedDistantMaskSize);
				std::copy(eptr, eptr + compressedDistantMaskSize, distantMask.get());
			}
		}

		template<ArchType arch, class KeyType, size_t windowSize, bool exclusive, bool useDistantTokens>
		float PcLangModel<arch, KeyType, windowSize, exclusive, useDistantTokens>::progress(int32_t& nodeIdx,
			uint32_t& contextIdx,
			size_t& historyPos,
			std::array<KeyType, windowSize + (exclusive ? 1 : 0)>& history,
			KeyType next) const
		{
			const auto& header = getHeader();
			const bool validDistantToken = distantTokenMask(next);
			float ll = 0;

			thread_local Eigen::MatrixXf mat;
			mat.resize(header.dim, 1 + windowSize);
			thread_local Eigen::VectorXf lls;
			lls.resize(1 + windowSize);
			if (useDistantTokens && validDistantToken)
			{
				lls[0] = contextConf[contextIdx];
				lls.tail(windowSize) = Eigen::Map<Eigen::VectorXf>{ &positionConf[0], windowSize };
				for (size_t i = 0; i < windowSize; ++i)
				{
					const auto historyToken = history[(historyPos + i) % windowSize];
					lls[i + 1] += historyToken ? distantConf[historyToken] : -99999;
				}
				logsoftmaxInplace(lls.array());

				mat.col(0) = Eigen::Map<Eigen::VectorXf>{ &contextEmb[contextIdx * header.dim], header.dim };
				lls[0] -= contextBias[contextIdx];
				for (size_t i = 0; i < windowSize; ++i)
				{
					const auto historyToken = history[(historyPos + i) % windowSize];
					if (historyToken) mat.col(i + 1) = Eigen::Map<Eigen::VectorXf>{ &distantEmb[historyToken * header.dim], header.dim };
					else mat.col(i + 1).setZero();
					lls[i + 1] -= distantBias[historyToken];
				}
				lls.tail(windowSize).array() += contextValidTokenSum[contextIdx];
				Eigen::Map<Eigen::VectorXf> outputVec{ &outputEmb[next * header.dim], header.dim };
				lls += mat.transpose() * outputVec;
				ll = LogExpSum<arch>{}(lls.data(), std::integral_constant<size_t, windowSize>());
			}
			else
			{
				lls[0] = -contextBias[contextIdx];
				mat.col(0) = Eigen::Map<Eigen::VectorXf>{ &contextEmb[contextIdx * header.dim], header.dim };
				Eigen::Map<Eigen::VectorXf> outputVec{ &outputEmb[next * header.dim], header.dim };
				lls.head(1) += mat.transpose() * outputVec;
				ll = lls[0];
			}

			contextIdx = progressContextNode(nodeIdx, next);
			if (history[windowSize])
			{
				history[historyPos] = history[windowSize];
				historyPos = (historyPos + 1) % windowSize;
			}
			history[windowSize] = validDistantToken ? next : 0;
			return ll;
		}

		utils::MemoryObject PcLangModelBase::build(const string& contextDefinition, const string& embedding, bool reorderContextId)
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

			PcLangModelHeader header;
			memset(&header, 0, sizeof(PcLangModelHeader));
			header.dim = dim;
			header.contextSize = contextSize;
			header.vocabSize = outputSize;
			header.keySize = 4;
			header.windowSize = windowSize;
			header.numNodes = nodeSizes.size();

			size_t finalSize = 0;
			header.nodeOffset = alignedOffsetInc(finalSize, sizeof(PcLangModelHeader));
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
			ostr.write((const char*)&header, sizeof(PcLangModelHeader));
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

		template<ArchType arch, class KeyType, size_t windowSize, bool exclusive, bool useDistantTokens>
		void* PcLangModel<arch, KeyType, windowSize, exclusive, useDistantTokens>::getFindBestPathFn() const
		{
			return (void*)&BestPathFinder::findBestPath<PcLangModel<arch, KeyType, windowSize, exclusive, useDistantTokens>>;
		}

		template<ArchType arch, class KeyType, size_t windowSize, bool exclusive, bool useDistantTokens>
		void* PcLangModel<arch, KeyType, windowSize, exclusive, useDistantTokens>::getNewJoinerFn() const
		{
			return (void*)&newJoinerWithKiwi<LmStateType>;
		}

		template<ArchType archType, class KeyTy, bool useDistantTokens>
		inline std::unique_ptr<PcLangModelBase> createOptimizedModelWithWindowSize(utils::MemoryObject&& mem)
		{
			auto& header = *reinterpret_cast<const PcLangModelHeader*>(mem.get());
			switch (header.windowSize)
			{
			case 4:
				return make_unique<PcLangModel<archType, KeyTy, 4, true, useDistantTokens>>(std::move(mem));
			case 7:
				return make_unique<PcLangModel<archType, KeyTy, 7, true, useDistantTokens>>(std::move(mem));
			case 8:
				return make_unique<PcLangModel<archType, KeyTy, 8, false, useDistantTokens>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `window_size` : " + std::to_string((size_t)header.windowSize) };
			};
		}

		template<ArchType archType, bool useDistantTokens>
		std::unique_ptr<PcLangModelBase> createOptimizedModel(utils::MemoryObject&& mem)
		{
			auto& header = *reinterpret_cast<const PcLangModelHeader*>(mem.get());
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

		std::unique_ptr<PcLangModelBase> PcLangModelBase::create(utils::MemoryObject&& mem, ArchType archType, bool useDistantTokens)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> tableWithoutDistantTokens{ CreateOptimizedModelGetter<false>{} },
				tableWithDistantTokens{ CreateOptimizedModelGetter<true>{} };
			auto fn = (useDistantTokens ? tableWithDistantTokens : tableWithoutDistantTokens)[static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(mem));
		}
	}
}
