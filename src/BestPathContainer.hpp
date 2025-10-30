#pragma once

#include <kiwi/Types.h>
#include <kiwi/BitUtils.h>

namespace kiwi
{
	template<class LmState>
	struct WordLL;

	using Wid = uint32_t;

	enum class PathEvaluatingMode
	{
		topN,
		top1Small,
		top1Medium,
		top1,
	};

	template<class LmState>
	struct WordLL
	{
		LmState lmState;
		uint8_t prevRootId = 0;
		SpecialState spState;
		uint8_t rootId = 0;

		const Morpheme* morpheme = nullptr;
		float accScore = 0, firstChunkScore = 0, accTypoCost = 0, accDialectCost = 0;
		const WordLL* parent = nullptr;
		Wid wid = 0;
		uint16_t ownFormId = 0;
		uint8_t combineSocket = 0;

		WordLL() = default;

		WordLL(const Morpheme* _morph, float _accScore, float _firstChunkScore, float _accTypoCost, float _accDialectCost,
			const WordLL* _parent, LmState _lmState, SpecialState _spState)
			: morpheme{ _morph },
			accScore{ _accScore },
			firstChunkScore{ _firstChunkScore },
			accTypoCost{ _accTypoCost },
			accDialectCost{ _accDialectCost },
			parent{ _parent },
			lmState{ _lmState },
			spState{ _spState },
			rootId{ _parent ? _parent->rootId : (uint8_t)0 }
		{
		}

		const WordLL* root() const
		{
			if (parent) return parent->root();
			else return this;
		}

		bool equalTo(const LmState& lmState, uint8_t prevRootId, SpecialState spState) const
		{
			return ((this->prevRootId == prevRootId) & (this->spState == spState)) && this->lmState == lmState;
		}

		bool operator==(const WordLL& o) const
		{
			return equalTo(o.lmState, o.prevRootId, o.spState);
		}
	};

	template<class LmState>
	struct Hash<WordLL<LmState>>
	{
		size_t operator()(const WordLL<LmState>& p) const
		{
			size_t ret = Hash<LmState>{}(p.lmState);
			ret = *reinterpret_cast<const uint16_t*>(&p.prevRootId) ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
			return ret;
		}

		size_t operator()(const LmState& lmState, uint8_t prevRootId, uint8_t spState) const
		{
			size_t ret = Hash<LmState>{}(lmState);
			ret = ((uint16_t)(prevRootId) | ((uint16_t)spState << 8)) ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
			return ret;
		}
	};

	static constexpr uint8_t commonRootId = -1;

	template<class LmState>
	struct PathHash
	{
		LmState lmState;
		uint8_t rootId, spState;

		PathHash(LmState _lmState = {}, uint8_t _rootId = 0, SpecialState _spState = {})
			: lmState{ _lmState }, rootId{ _rootId }, spState{ _spState }
		{
		}

		PathHash(const WordLL<LmState>& wordLl, const Morpheme* morphBase)
			: PathHash{ wordLl.lmState, wordLl.rootId, wordLl.spState }
		{
		}

		bool operator==(const PathHash& o) const
		{
			return lmState == o.lmState && rootId == o.rootId && spState == o.spState;
		}
	};

	template<class LmState>
	struct Hash<PathHash<LmState>>
	{
		size_t operator()(const PathHash<LmState>& p) const
		{
			size_t ret = Hash<LmState>{}(p.lmState);
			ret = *reinterpret_cast<const uint16_t*>(&p.rootId) ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
			return ret;
		}
	};

	struct WordLLGreater
	{
		template<class LmState>
		bool operator()(const WordLL<LmState>& a, const WordLL<LmState>& b) const
		{
			return a.accScore > b.accScore;
		}
	};

	template<class LmState>
	inline std::ostream& printDebugPath(std::ostream& os, const WordLL<LmState>& src)
	{
		if (src.parent)
		{
			printDebugPath(os, *src.parent);
		}

		if (src.morpheme) src.morpheme->print(os);
		else os << "NULL";
		os << " , ";
		return os;
	}

	template<PathEvaluatingMode mode, class LmState>
	class BestPathConatiner;

	template<PathEvaluatingMode mode>
	struct BestPathContainerTraits
	{
		static constexpr size_t maxSize = -1;
	};

	template<class LmState>
	class BestPathConatiner<PathEvaluatingMode::topN, LmState>
	{
		// pair: [index, size]
		UnorderedMap<PathHash<LmState>, std::pair<uint32_t, uint32_t>> bestPathIndex;
		Vector<WordLL<LmState>> bestPathValues;
	public:

		inline void clear()
		{
			bestPathIndex.clear();
			bestPathValues.clear();
		}

		inline void insert(size_t topN, uint8_t prevRootId, uint8_t rootId,
			const Morpheme* morph, float accScore, float firstChunkScore, float accTypoCost, float accDialectCost,
			const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			PathHash<LmState> ph{ lmState, prevRootId, spState };
			auto inserted = bestPathIndex.emplace(ph, std::make_pair((uint32_t)bestPathValues.size(), 1));
			if (inserted.second)
			{
				bestPathValues.emplace_back(morph, accScore, firstChunkScore, accTypoCost, accDialectCost,
					parent, std::move(lmState), spState);
				if (rootId != commonRootId) bestPathValues.back().rootId = rootId;
				bestPathValues.resize(bestPathValues.size() + topN - 1);
			}
			else
			{
				auto bestPathFirst = bestPathValues.begin() + inserted.first->second.first;
				auto bestPathLast = bestPathValues.begin() + inserted.first->second.first + inserted.first->second.second;
				if (std::distance(bestPathFirst, bestPathLast) < topN)
				{
					*bestPathLast = WordLL<LmState>{ morph, accScore, firstChunkScore, accTypoCost, accDialectCost,
						parent, std::move(lmState), spState };
					if (rootId != commonRootId) bestPathLast->rootId = rootId;
					std::push_heap(bestPathFirst, bestPathLast + 1, WordLLGreater{});
					++inserted.first->second.second;
				}
				else
				{
					if (accScore > bestPathFirst->accScore)
					{
						std::pop_heap(bestPathFirst, bestPathLast, WordLLGreater{});
						*(bestPathLast - 1) = WordLL<LmState>{ morph, accScore, firstChunkScore, accTypoCost, accDialectCost,
							parent, std::move(lmState), spState };
						if (rootId != commonRootId) (*(bestPathLast - 1)).rootId = rootId;
						std::push_heap(bestPathFirst, bestPathLast, WordLLGreater{});
					}
				}
			}
		}

		inline void writeTo(Vector<WordLL<LmState>>& resultOut, const Morpheme* curMorph, Wid lastSeqId, size_t ownFormId)
		{
			for (auto& p : bestPathIndex)
			{
				const auto index = p.second.first;
				const auto size = p.second.second;
				for (size_t i = 0; i < size; ++i)
				{
					resultOut.emplace_back(std::move(bestPathValues[index + i]));
					auto& newPath = resultOut.back();

					// fill the rest information of resultOut
					newPath.wid = lastSeqId;
					if (curMorph->isSingle())
					{
						newPath.combineSocket = curMorph->combineSocket;
						newPath.ownFormId = ownFormId;
					}
				}
			}
		}
	};

	template<class LmState>
	class BestPathConatiner<PathEvaluatingMode::top1, LmState>
	{
		UnorderedSet<WordLL<LmState>> bestPathes;
	public:
		inline void clear()
		{
			bestPathes.clear();
		}

		inline void insert(size_t topN, uint8_t prevRootId, uint8_t rootId,
			const Morpheme* morph, float accScore, float firstChunkScore, float accTypoCost, float accDialectCost,
			const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			WordLL<LmState> newPath{ morph, accScore, firstChunkScore, accTypoCost, accDialectCost,
				parent, std::move(lmState), spState };
			newPath.prevRootId = prevRootId;
			if (rootId != commonRootId) newPath.rootId = rootId;
			auto inserted = bestPathes.emplace(newPath);
			if (!inserted.second)
			{
				// this is dangerous, but we can update the key safely
				// because an equality between the two objects is guaranteed
				auto& target = const_cast<WordLL<LmState>&>(*inserted.first);
				if (accScore > target.accScore)
				{
					target = newPath;
				}
			}
		}

		inline void writeTo(Vector<WordLL<LmState>>& resultOut, const Morpheme* curMorph, Wid lastSeqId, size_t ownFormId)
		{
			for (auto& p : bestPathes)
			{
				resultOut.emplace_back(std::move(p));
				auto& newPath = resultOut.back();

				// fill the rest information of resultOut
				newPath.wid = lastSeqId;
				if (curMorph->isSingle())
				{
					newPath.combineSocket = curMorph->combineSocket;
					newPath.ownFormId = ownFormId;
				}
			}
		}
	};

	template<>
	struct BestPathContainerTraits<PathEvaluatingMode::top1Small>
	{
		static constexpr size_t maxSize = (sizeof(size_t) == 8 ? 64 : 32) * 2;
	};

	template<>
	struct BestPathContainerTraits<PathEvaluatingMode::top1Medium>
	{
		static constexpr size_t maxSize = BestPathContainerTraits<PathEvaluatingMode::top1Small>::maxSize * 4;
	};

	template<class LmState, size_t bucketBits>
	class BucketedHashContainer
	{
		static constexpr size_t bucketSize = 1 << bucketBits;

		std::array<std::array<uint8_t, BestPathContainerTraits<PathEvaluatingMode::top1Small>::maxSize>, bucketSize> hashes;
		std::array<Vector<WordLL<LmState>>, bucketSize> values;

	public:
		BucketedHashContainer()
		{
			for (auto& v : values)
			{
				v.reserve(BestPathContainerTraits<PathEvaluatingMode::top1Small>::maxSize);
			}
		}

		inline void clear()
		{
			for (auto& v : values)
			{
				v.clear();
			}
		}

		template<ArchType archType>
		inline void insertOptimized(size_t topN, uint8_t prevRootId, uint8_t rootId,
			const Morpheme* morph, float accScore, float firstChunkScore, float accTypoCost, float accDialectCost,
			const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			static constexpr size_t numBits = sizeof(size_t) * 8;
			const size_t h = Hash<WordLL<LmState>>{}(lmState, prevRootId, spState);
			const size_t bucket = (h >> 8) & (bucketSize - 1);
			auto& hash = hashes[bucket];
			auto& value = values[bucket];

			size_t it = value.size();
			size_t bits[2];
			bits[0] = nst::findAll<archType>(hash.data(), std::min(value.size(), numBits), (uint8_t)h);
			bits[1] = value.size() > numBits ? nst::findAll<archType>(hash.data() + numBits, value.size() - numBits, (uint8_t)h) : 0;
			while (bits[0])
			{
				const size_t i = utils::countTrailingZeroes(bits[0]);
				if (value[i].equalTo(lmState, prevRootId, spState))
				{
					it = i;
					goto breakloop;
				}
				bits[0] &= ~((size_t)1 << i);
			}
			while (bits[1])
			{
				const size_t i = utils::countTrailingZeroes(bits[1]);
				if (value[i].equalTo(lmState, prevRootId, spState))
				{
					it = i + numBits;
					goto breakloop;
				}
				bits[1] &= ~((size_t)1 << i);
			}

		breakloop:;
			if (it >= value.size())
			{
				if (value.size() < hash.size())
				{
					hash[value.size()] = h;
					value.emplace_back(morph, accScore, firstChunkScore, accTypoCost, accDialectCost,
						parent, std::move(lmState), spState);
					value.back().prevRootId = prevRootId;
					if (rootId != commonRootId) value.back().rootId = rootId;
				}
				else
				{
					// skip insertion if container is full.
					// this isn't correct, but it rarely happens
				}
			}
			else
			{
				auto& target = value[it];
				if (accScore > target.accScore)
				{
					target.morpheme = morph;
					target.accScore = accScore;
					target.firstChunkScore = firstChunkScore;
					target.accTypoCost = accTypoCost;
					target.accDialectCost = accDialectCost;
					target.parent = parent;
					target.lmState = std::move(lmState);
					target.spState = spState;
					target.rootId = parent ? parent->rootId : 0;
					if (rootId != commonRootId) target.rootId = rootId;
				}
			}
		}

		inline void insert(size_t topN, uint8_t prevRootId, uint8_t rootId,
			const Morpheme* morph, float accScore, float firstChunkScore, float accTypoCost, float accDialectCost,
			const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			static constexpr ArchType archType = LmState::arch;
			if constexpr (archType != ArchType::none && archType != ArchType::balanced)
			{
				return insertOptimized<archType>(topN, prevRootId, rootId, morph, accScore, firstChunkScore, accTypoCost, accDialectCost,
					parent, std::move(lmState), spState);
			}

			const size_t h = Hash<WordLL<LmState>>{}(lmState, prevRootId, spState);
			const size_t bucket = (h >> 8) & (bucketSize - 1);
			auto& hash = hashes[bucket];
			auto& value = values[bucket];

			const auto hashEnd = hash.begin() + value.size();
			auto it = std::find(hash.begin(), hashEnd, (uint8_t)h);
			while (it != hashEnd)
			{
				if (value[it - hash.begin()].equalTo(lmState, prevRootId, spState))
				{
					break;
				}
				++it;
				it = std::find(it, hashEnd, (uint8_t)h);
			}

			if (it == hashEnd)
			{
				if (value.size() < hash.size())
				{
					hash[value.size()] = h;
					value.emplace_back(morph, accScore, firstChunkScore, accTypoCost, accDialectCost,
						parent, std::move(lmState), spState);
					value.back().prevRootId = prevRootId;
					if (rootId != commonRootId) value.back().rootId = rootId;
				}
				else
				{
					// skip insertion if container is full.
					// this isn't correct, but it rarely happens
				}
			}
			else
			{
				auto& target = value[it - hash.begin()];
				if (accScore > target.accScore)
				{
					target.morpheme = morph;
					target.accScore = accScore;
					target.firstChunkScore = firstChunkScore;
					target.accTypoCost = accTypoCost;
					target.accDialectCost = accDialectCost;
					target.parent = parent;
					target.lmState = std::move(lmState);
					target.spState = spState;
					target.rootId = parent ? parent->rootId : 0;
					if (rootId != commonRootId) target.rootId = rootId;
				}
			}
		}

		inline void writeTo(Vector<WordLL<LmState>>& resultOut, const Morpheme* curMorph, Wid lastSeqId, size_t ownFormId)
		{
			for (auto& v : values)
			{
				for (auto& p : v)
				{
					resultOut.emplace_back(std::move(p));
					auto& newPath = resultOut.back();

					// fill the rest information of resultOut
					newPath.wid = lastSeqId;
					if (curMorph->isSingle())
					{
						newPath.combineSocket = curMorph->combineSocket;
						newPath.ownFormId = ownFormId;
					}
				}
			}
		}
	};


	template<class LmState>
	class alignas(BestPathContainerTraits<PathEvaluatingMode::top1Small>::maxSize) BestPathConatiner<PathEvaluatingMode::top1Small, LmState>
		: public BucketedHashContainer<LmState, 0>
	{
	};

	template<class LmState>
	class alignas(BestPathContainerTraits<PathEvaluatingMode::top1Small>::maxSize) BestPathConatiner<PathEvaluatingMode::top1Medium, LmState>
		: public BucketedHashContainer<LmState, 2>
	{
	};
}
