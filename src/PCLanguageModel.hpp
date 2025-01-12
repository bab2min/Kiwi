#pragma once

#include <Eigen/Dense>

#include <kiwi/Types.h>
#include <kiwi/Utils.h>
#include <kiwi/PCLanguageModel.h>
#include <kiwi/ArchUtils.h>
#include "ArchAvailable.h"
#include "search.h"
#include "streamvbyte.h"


namespace kiwi
{
	namespace pclm
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

		template<ArchType arch, class KeyType, size_t windowSize>
		class PCLanguageModel : public PCLanguageModelBase
		{
			using MyNode = Node<KeyType, uint32_t>;

			std::unique_ptr<MyNode[]> nodeData;
			std::unique_ptr<KeyType[]> keyData;
			std::unique_ptr<int32_t[]> valueData;
			std::unique_ptr<float[]> contextEmb;
			std::unique_ptr<float[]> contextBias;
			std::unique_ptr<float[]> contextConf;
			std::unique_ptr<float[]> distantEmb;
			std::unique_ptr<float[]> distantBias;
			std::unique_ptr<float[]> distantConf;
			std::unique_ptr<float[]> outputEmb;

			MyNode* findLowerNode(MyNode* node, KeyType k) const
			{
				while (node->lower)
				{
					auto* lowerNode = node + node->lower;
					auto* keys = &keyData[lowerNode->nextOffset];
					auto* values = &valueData[lowerNode->nextOffset];
					int32_t found;
					if (nst::search<arch>(
						keys,
						values,
						lowerNode->numNexts,
						k,
						found
					) && found >= 0)
					{
						return lowerNode + found;
					}
					node = lowerNode;
				}
				return node;
			}

			uint32_t findLowerValue(MyNode* node, KeyType k) const
			{
				while (node->lower)
				{
					auto* lowerNode = node + node->lower;
					auto* keys = &keyData[lowerNode->nextOffset];
					auto* values = &valueData[lowerNode->nextOffset];
					int32_t found;
					if (nst::search<arch>(
						keys,
						values,
						lowerNode->numNexts,
						k,
						found
					))
					{
						if (found >= 0)
						{
							return lowerNode[found].value;
						}
						else
						{
							return -found;
						}
					}
					node = lowerNode;
				}
				return node->value;
			}

		public:
			PCLanguageModel(utils::MemoryObject&& mem) : PCLanguageModelBase{ std::move(mem) }
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
				contextConf = make_unique<float[]>(header.contextSize);
				distantEmb = make_unique<float[]>(header.vocabSize * header.dim);
				distantBias = make_unique<float[]>(header.vocabSize);
				distantConf = make_unique<float[]>(header.vocabSize);
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
					contextConf[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
					eptr += sizeof(uint16_t);
				}

				const uint16_t* distantEmbScale = reinterpret_cast<const uint16_t*>(eptr + header.vocabSize * header.dim);
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					dequantize(&distantEmb[i * header.dim], reinterpret_cast<const int8_t*>(eptr), header.dim, half2float(distantEmbScale[i]));
					eptr += header.dim;
				}
				eptr += header.vocabSize * sizeof(uint16_t);
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					distantBias[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
					eptr += sizeof(uint16_t);
				}
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					distantConf[i] = half2float(*reinterpret_cast<const uint16_t*>(eptr));
					eptr += sizeof(uint16_t);
				}

				const uint16_t* outputEmbScale = reinterpret_cast<const uint16_t*>(eptr + header.vocabSize * header.dim);
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					dequantize(&outputEmb[i * header.dim], reinterpret_cast<const int8_t*>(eptr), header.dim, half2float(outputEmbScale[i]));
					eptr += header.dim;
				}
			}

			uint32_t progressContextNode(int32_t& nodeIdx, KeyType next) const
			{
				while (1)
				{
					int32_t v;
					auto* node = &nodeData[nodeIdx];
					auto* keys = &keyData[node->nextOffset];
					auto* values = &valueData[node->nextOffset];
					PREFETCH_T0(node + node->lower);
					if (!nst::search<arch>(
						keys,
						values,
						node->numNexts, next, v
					))
					{
						if (!node->lower) return 0;
						nodeIdx += node->lower;
						PREFETCH_T0(&keyData[nodeData[nodeIdx].nextOffset]);
						continue;
					}

					// non-leaf node
					if (v > 0)
					{
						nodeIdx += v;
						return nodeData[nodeIdx].value;
					}
					// leaf node
					else
					{
						while (node->lower)
						{
							node += node->lower;
							int32_t lv;
							if (nst::search<arch>(
								&keyData[node->nextOffset],
								&valueData[node->nextOffset],
								node->numNexts, next, lv
							))
							{
								if (lv > 0)
								{
									node += lv;
									nodeIdx = node - &nodeData[0];
									return (uint32_t)-v;
								}
							}
						}
						nodeIdx = 0;
						return (uint32_t)-v;
					}
				}
			}

			float progress(int32_t& nodeIdx, uint32_t& contextIdx, KeyType next) const
			{
				const auto& header = getHeader();
				Eigen::Map<Eigen::VectorXf> contextVec{ &contextEmb[contextIdx * header.dim], header.dim };
				Eigen::Map<Eigen::VectorXf> outputVec{ &outputEmb[next * header.dim], header.dim };
				float ll = contextVec.dot(outputVec) - contextBias[contextIdx];
				contextIdx = progressContextNode(nodeIdx, next);
				return ll;
			}
		};

		static constexpr size_t serialAlignment = 16;

		inline size_t alignedOffsetInc(size_t& offset, size_t inc, size_t alignment = serialAlignment)
		{
			return offset = (offset + inc + alignment - 1) & ~(alignment - 1);
		}

		inline std::ostream& writePadding(std::ostream& os, size_t alignment = serialAlignment)
		{
			const size_t pos = os.tellp();
			size_t pad = ((pos + alignment - 1) & ~(alignment - 1)) - pos;
			for (size_t i = 0; i < pad; ++i)
			{
				os.put(0);
			}
			return os;
		}
	}
}
