#pragma once

#include <kiwi/Types.h>

namespace kiwi
{
	template<class LmState>
	struct WordLL;

	using Wid = uint32_t;

	enum class PathEvaluatingMode
	{
		topN,
		top1,
		top1Small,
	};

	template<class LmState>
	struct WordLL
	{
		const Morpheme* morpheme = nullptr;
		float accScore = 0, accTypoCost = 0;
		const WordLL* parent = nullptr;
		LmState lmState;
		Wid wid = 0;
		uint16_t ownFormId = 0;
		uint8_t combineSocket = 0;
		uint8_t rootId = 0;
		SpecialState spState;

		WordLL() = default;

		WordLL(const Morpheme* _morph, float _accScore, float _accTypoCost, const WordLL* _parent, LmState _lmState, SpecialState _spState)
			: morpheme{ _morph },
			accScore{ _accScore },
			accTypoCost{ _accTypoCost },
			parent{ _parent },
			lmState{ _lmState },
			spState{ _spState },
			rootId{ parent ? parent->rootId : (uint8_t)0 }
		{
		}

		const WordLL* root() const
		{
			if (parent) return parent->root();
			else return this;
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
			size_t ret = 0;
			if (sizeof(PathHash<LmState>) % sizeof(size_t))
			{
				auto ptr = reinterpret_cast<const uint32_t*>(&p);
				for (size_t i = 0; i < sizeof(PathHash<LmState>) / sizeof(uint32_t); ++i)
				{
					ret ^= ptr[i];
				}
			}
			else
			{
				auto ptr = reinterpret_cast<const size_t*>(&p);
				for (size_t i = 0; i < sizeof(PathHash<LmState>) / sizeof(size_t); ++i)
				{
					ret ^= ptr[i];
				}
			}
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

		inline void insert(const PathHash<LmState>& ph, size_t topN, uint8_t rootId,
			const Morpheme* morph, float accScore, float accTypoCost, const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			auto inserted = bestPathIndex.emplace(ph, make_pair((uint32_t)bestPathValues.size(), 1));
			if (inserted.second)
			{
				bestPathValues.emplace_back(morph, accScore, accTypoCost, parent, move(lmState), spState);
				if (rootId != commonRootId) bestPathValues.back().rootId = rootId;
				bestPathValues.resize(bestPathValues.size() + topN - 1);
			}
			else
			{
				auto bestPathFirst = bestPathValues.begin() + inserted.first->second.first;
				auto bestPathLast = bestPathValues.begin() + inserted.first->second.first + inserted.first->second.second;
				if (distance(bestPathFirst, bestPathLast) < topN)
				{
					*bestPathLast = WordLL<LmState>{ morph, accScore, accTypoCost, parent, move(lmState), spState };
					if (rootId != commonRootId) bestPathLast->rootId = rootId;
					push_heap(bestPathFirst, bestPathLast + 1, WordLLGreater{});
					++inserted.first->second.second;
				}
				else
				{
					if (accScore > bestPathFirst->accScore)
					{
						pop_heap(bestPathFirst, bestPathLast, WordLLGreater{});
						*(bestPathLast - 1) = WordLL<LmState>{ morph, accScore, accTypoCost, parent, move(lmState), spState };
						if (rootId != commonRootId) (*(bestPathLast - 1)).rootId = rootId;
						push_heap(bestPathFirst, bestPathLast, WordLLGreater{});
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
					resultOut.emplace_back(move(bestPathValues[index + i]));
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
		UnorderedMap<PathHash<LmState>, WordLL<LmState>> bestPathes;
	public:
		inline void clear()
		{
			bestPathes.clear();
		}

		inline void insert(const PathHash<LmState>& ph, size_t topN, uint8_t rootId,
			const Morpheme* morph, float accScore, float accTypoCost, const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			WordLL<LmState> newPath{ morph, accScore, accTypoCost, parent, move(lmState), spState };
			if (rootId != commonRootId) newPath.rootId = rootId;
			auto inserted = bestPathes.emplace(ph, newPath);
			if (!inserted.second)
			{
				auto& target = inserted.first->second;
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
				resultOut.emplace_back(move(p.second));
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

	template<class LmState>
	class BestPathConatiner<PathEvaluatingMode::top1Small, LmState>
	{
		Vector<PathHash<LmState>> bestPathIndicesSmall;
		Vector<WordLL<LmState>> bestPathValuesSmall;
	public:

		inline void clear()
		{
			bestPathIndicesSmall.clear();
			bestPathValuesSmall.clear();
		}

		inline void insert(const PathHash<LmState>& ph, size_t topN, uint8_t rootId,
			const Morpheme* morph, float accScore, float accTypoCost, const WordLL<LmState>* parent, LmState&& lmState, SpecialState spState)
		{
			const auto it = find(bestPathIndicesSmall.begin(), bestPathIndicesSmall.end(), ph);
			if (it == bestPathIndicesSmall.end())
			{
				bestPathIndicesSmall.push_back(ph);
				bestPathValuesSmall.emplace_back(morph, accScore, accTypoCost, parent, move(lmState), spState);
				if (rootId != commonRootId) bestPathValuesSmall.back().rootId = rootId;
			}
			else
			{
				auto& target = bestPathValuesSmall[it - bestPathIndicesSmall.begin()];
				if (accScore > target.accScore)
				{
					target = WordLL<LmState>{ morph, accScore, accTypoCost, parent, move(lmState), spState };
					if (rootId != commonRootId) target.rootId = rootId;
				}
			}
		}

		inline void writeTo(Vector<WordLL<LmState>>& resultOut, const Morpheme* curMorph, Wid lastSeqId, size_t ownFormId)
		{
			for (auto& p : bestPathValuesSmall)
			{
				resultOut.emplace_back(move(p));
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
}