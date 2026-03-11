#include <numeric>
#include <kiwi/Types.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Utils.h>
#include "ArchAvailable.h"
#include "KTrie.h"
#include "FeatureTestor.h"
#include "FrozenTrie.hpp"

using namespace std;
using namespace kiwi;

namespace kiwi
{
	template<class... Args>
	inline bool appendNewNode(Vector<KGraphNode>& nodes, Vector<pair<uint32_t, uint32_t>>& endPosMap, size_t startPos, size_t endPos, Args&&... args)
	{
		static constexpr uint32_t npos = -1;

		if (endPosMap[startPos].first == endPosMap[startPos].second)
		{
			return false;
		}

		size_t newId = nodes.size();
		nodes.emplace_back(startPos, endPos, forward<Args>(args)...);
		auto& nnode = nodes.back();

		nnode.prev = newId - endPosMap[startPos].first;
		if (nnode.endPos >= endPosMap.size()) return true;

		if (endPosMap[nnode.endPos].first == endPosMap[nnode.endPos].second)
		{
			endPosMap[nnode.endPos].first = newId;
			endPosMap[nnode.endPos].second = newId + 1;
		}
		else
		{
			nodes[endPosMap[nnode.endPos].second - 1].sibling = newId - (endPosMap[nnode.endPos].second - 1);
			endPosMap[nnode.endPos].second = newId + 1;
		}
		return true;
	}

	template<bool typoTolerant, bool continualTypoTolerant, bool lengtheningTypoTolerant>
	struct FormCandidate
	{
		const Form* form = nullptr;
		float cost = 0;
		uint32_t start = 0;
		uint32_t typoId = 0;
		uint32_t end = 0; // only used in continual typo tolerant mode
		uint32_t numSpaces = 0;

		FormCandidate(const Form* _form = nullptr,
			float _cost = 0,
			uint32_t _start = 0,
			uint32_t _typoId = 0,
			uint32_t _end = 0,
			uint32_t _numSpaces = 0,
			uint32_t = 0)
			: form{ _form },
			cost{ _cost },
			start{ _start },
			typoId{ _typoId },
			end{ _end },
			numSpaces{ _numSpaces }
		{
		}

		size_t getStartPos(size_t) const
		{
			return start;
		}

		size_t getEndPos(size_t currentPos) const
		{
			return end ? end : currentPos;
		}

		float getTypoCost() const
		{
			return cost;
		}

		uint32_t getTypoId() const
		{
			return typoId;
		}

		size_t getFormSizeWithTypos(const size_t* typoPtrs) const
		{
			return typoPtrs[typoId + 1] - typoPtrs[typoId] + numSpaces;
		}

		bool operator==(const Form* f) const
		{
			return form == f;
		}
	};

	template<>
	struct FormCandidate<false, false, false>
	{
		const Form* form = nullptr;

		FormCandidate(const Form* _form = nullptr, float = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0)
			: form{ _form }
		{
		}

		size_t getStartPos(size_t currentPos) const
		{
			return currentPos - form->sizeWithoutSpace();
		}

		size_t getEndPos(size_t currentPos) const
		{
			return currentPos;
		}

		float getTypoCost() const
		{
			return 0;
		}

		uint32_t getTypoId() const
		{
			return 0;
		}

		size_t getFormSizeWithTypos(const size_t*) const
		{
			return form->form.size();
		}

		bool operator==(const Form* f) const
		{
			return form == f;
		}
	};

	template<bool typoTolerant, bool continualTypoTolerant>
	struct FormCandidate<typoTolerant, continualTypoTolerant, true> : public FormCandidate<typoTolerant, continualTypoTolerant, false>
	{
		using BaseType = FormCandidate<typoTolerant, continualTypoTolerant, false>;
		uint32_t lengthenedSize = 0;

		FormCandidate(const Form* _form = nullptr,
			float _cost = 0,
			uint32_t _start = 0,
			uint32_t _typoId = 0,
			uint32_t _end = 0,
			uint32_t _numSpaces = 0,
			uint32_t _lengthenedSize = 0)
			: FormCandidate<typoTolerant, continualTypoTolerant, false>{ _form, _cost, _start, _typoId, _end, _numSpaces, _lengthenedSize },
			lengthenedSize{ _lengthenedSize }
		{}

		size_t getFormSizeWithTypos(const size_t* typoPtrs) const
		{
			return BaseType::getFormSizeWithTypos(typoPtrs) + lengthenedSize;
		}
	};

	template<bool typoTolerant>
	const Form& getForm(const Form* foundCand, const Form* formBase)
	{
		if (typoTolerant)
		{
			auto tCand = reinterpret_cast<const TypoForm*>(foundCand);
			return tCand->form(formBase);
		}
		else
		{
			return *foundCand;
		}
	}

	template<bool typoTolerant, bool continualTypoTolerant, bool lengtheningTypoTolerant>
	bool insertCandidates(
		Vector<FormCandidate<typoTolerant, continualTypoTolerant, lengtheningTypoTolerant>>& candidates,
		const Form* foundCand,
		const Form* formBase,
		const size_t* typoPtrs,
		U16StringView str,
		const Vector<uint32_t>& nonSpaces,
		Dialect allowedDialect,
		uint32_t startPosition = 0,
		uint32_t endPosition = 0,
		float cost = 0,
		uint32_t lengthenedSize = 0
	)
	{
		static constexpr size_t posMultiplier = continualTypoTolerant ? 4 : 1;
		if (typoTolerant)
		{
			auto tCand = reinterpret_cast<const TypoForm*>(foundCand);
			if (find(candidates.begin(), candidates.end(), &tCand->form(formBase)) != candidates.end()) return false;

			while (1)
			{
				const auto typoFormSize = typoPtrs[tCand->typoId + 1] - typoPtrs[tCand->typoId] + lengthenedSize;
				auto cand = &tCand->form(formBase);
				if (FeatureTestor::isMatched(&str[0], &str[nonSpaces[nonSpaces.size() - typoFormSize]], tCand->leftCond)
					&& FeatureTestor::isMatchedApprox(&str[0], &str[nonSpaces[nonSpaces.size() - typoFormSize]], cand->vowel, cand->polar)
					&& (cand->dialect == Dialect::standard || !!(cand->dialect & allowedDialect))
					&& (tCand->dialect == Dialect::standard || !!(tCand->dialect & allowedDialect)))
				{
					candidates.emplace_back(cand,
						tCand->score() + cost,
						startPosition ? startPosition : ((nonSpaces.size() - typoFormSize) * posMultiplier),
						tCand->typoId,
						endPosition,
						tCand->numSpaces,
						lengthenedSize);
				}
				if (tCand[0].hash() != tCand[1].hash()) break;
				++tCand;
			}
		}
		else
		{
			if (find(candidates.begin(), candidates.end(), foundCand) != candidates.end()) return false;

			while (1)
			{
				if (FeatureTestor::isMatchedApprox(&str[0], &str[nonSpaces[nonSpaces.size() + foundCand->numSpaces - foundCand->form.size()]], foundCand->vowel, foundCand->polar)
					&& (foundCand->dialect == Dialect::standard || !!(foundCand->dialect & allowedDialect)))
				{
					candidates.emplace_back(foundCand);
				}
				if (foundCand[0].formHash != foundCand[1].formHash) break;
				++foundCand;
			}
		}
		return true;
	}

	inline void removeUnconnected(Vector<KGraphNode>& ret, const Vector<KGraphNode>& graph, const Vector<std::pair<uint32_t, uint32_t>>& endPosMap)
	{
		thread_local Vector<uint8_t> connectedList;
		thread_local Vector<uint16_t> newIndexDiff;
		thread_local Deque<uint32_t> updateList;
		connectedList.clear();
		connectedList.resize(graph.size());
		newIndexDiff.clear();
		newIndexDiff.resize(graph.size());
		updateList.clear();
		updateList.emplace_back(graph.size() - 1);
		connectedList[graph.size() - 1] = 1;

		while (!updateList.empty())
		{
			const auto id = updateList.front();
			updateList.pop_front();
			const auto& node = graph[id];
			const auto scanStart = endPosMap[node.startPos].first, scanEnd = endPosMap[node.startPos].second;
			for (auto i = scanStart; i < scanEnd; ++i)
			{
				if (graph[i].endPos != node.startPos) continue;
				if (connectedList[i]) continue;
				updateList.emplace_back(i);
				connectedList[i] = 1;
			}
		}

		size_t connectedCnt = accumulate(connectedList.begin(), connectedList.end(), 0);
		newIndexDiff[0] = connectedList[0];
		for (size_t i = 1; i < graph.size(); ++i)
		{
			newIndexDiff[i] = newIndexDiff[i - 1] + connectedList[i];
		}
		for (size_t i = 0; i < graph.size(); ++i)
		{
			newIndexDiff[i] = i + 1 - newIndexDiff[i];
		}

		ret.reserve(connectedCnt);
		for (size_t i = 0; i < graph.size(); ++i)
		{
			if (!connectedList[i]) continue;
			ret.emplace_back(graph[i]);
			auto& newNode = ret.back();
			if (newNode.prev) newNode.prev -= newIndexDiff[i] - newIndexDiff[i - newNode.prev];
			if (newNode.sibling)
			{
				if (connectedList[i + newNode.sibling]) newNode.sibling -= newIndexDiff[i + newNode.sibling] - newIndexDiff[i];
				else newNode.sibling = 0;
			}
		}
	}

	// nonSpaces idx 데이터로부터 글자 수 + 공백 블록 수를 계산한다.
	template<class It>
	inline size_t countChrWithNormalizedSpace(It first, It last)
	{
		size_t n = std::distance(first, last);
		auto prevIdx = *first++;
		for (; first != last; ++first)
		{
			if (*first != prevIdx + 1) ++n;
			prevIdx = *first;
		}
		return n;
	}

	// 공백 문자의 위치가 형태소의 공백 위치와 불일치하는 개수를 센다.
	inline size_t countSpaceErrors(const KString& form, const uint32_t* spaceIdxFirst, const uint32_t* spaceIdxLast)
	{
		size_t n = 0;
		size_t spaceOffset = 0;
		const size_t size = std::distance(spaceIdxFirst, spaceIdxLast);
		for (size_t i = 1; i < size; ++i)
		{
			const bool hasSpace = spaceIdxFirst[i] - spaceIdxFirst[i - 1] > 1;
			if (hasSpace && form[i + spaceOffset] != u' ') ++n;
			spaceOffset += form[i + spaceOffset] == u' ' ? 1 : 0;
		}
		return n;
	}

	// onset: ㅇ=11, ㅎ=18
	inline char16_t overrideOnset(char16_t c, const int onset = 11)
	{
		if (!isHangulSyllable(c)) return 0;
		const int vowel = (c - 0xAC00) / 28 % 21;
		const int coda = (c - 0xAC00) % 28;
		return 0xAC00 + onset * 28 * 21 + vowel * 28 + coda;
	}

	// 받침 + 초성 ㅇ이 연철된 경우
	struct ContinualIeungDecomposer
	{
		static constexpr size_t boundaryId = 1;
		char16_t onsetToCoda(char16_t c)
		{
			static constexpr char16_t onsetToCoda[] = {
				0x11A8, // ㄱ
				0x11A9, // ㄲ
				0x11AB, // ㄴ
				0x11AE, // ㄷ
				0, // ㄸ
				0x11AF, // ㄹ
				0x11B7, // ㅁ
				0x11B8, // ㅂ
				0, // ㅃ
				0x11BA, // ㅅ
				0x11BB, // ㅆ
				0, // ㅇ
				0x11BD, // ㅈ
				0, // ㅉ
				0x11BE, // ㅊ
				0x11BF, // ㅋ
				0x11C0, // ㅌ
				0x11C1, // ㅍ
				0x11C2, // ㅎ
			};

			if (isHangulSyllable(c))
			{
				int onset = (c - 0xAC00) / 28 / 21;
				return onsetToCoda[onset];
			}

			switch (c)
			{
			case u'ㄱ': return 0x11A8;
			case u'ㄲ': return 0x11A9;
			case u'ㄴ': return 0x11AB;
			case u'ㄷ': return 0x11AE;
			case u'ㄹ': return 0x11AF;
			case u'ㅁ': return 0x11B7;
			case u'ㅂ': return 0x11B8;
			case u'ㅅ': return 0x11BA;
			case u'ㅆ': return 0x11BB;
			case u'ㅈ': return 0x11BD;
			case u'ㅊ': return 0x11BE;
			case u'ㅋ': return 0x11BF;
			case u'ㅌ': return 0x11C0;
			case u'ㅍ': return 0x11C1;
			case u'ㅎ': return 0x11C2;
			default: return 0;
			}

			return 0;
		}

		char16_t dropRightSyllable(char16_t c)
		{
			return overrideOnset(c, 11);
		}
	};

	// 받침 + 초성 ㅎ이 연철된 경우
	struct ContinualHieutDecomposer
	{
		static constexpr size_t boundaryId = 2;
		char16_t onsetToCoda(char16_t c)
		{
			static constexpr char16_t onsetToCoda[] = {
				0, // ㄱ
				0, // ㄲ
				0x11AB, // ㄴ
				0, // ㄷ
				0, // ㄸ
				0x11AF, // ㄹ
				0x11B7, // ㅁ
				0, // ㅂ
				0, // ㅃ
				0x11BA, // ㅅ
				0, // ㅆ
				0, // ㅇ
				0, // ㅈ
				0, // ㅉ
				0x11BD, // ㅊ->ㅈ
				0x11A8, // ㅋ->ㄱ
				0x11AE, // ㅌ->ㄷ
				0x11B8, // ㅍ->ㅂ
				0, // ㅎ
			};

			if (isHangulSyllable(c))
			{
				int onset = (c - 0xAC00) / 28 / 21;
				return onsetToCoda[onset];
			}
			return 0;
		}

		char16_t dropRightSyllable(char16_t c)
		{
			return overrideOnset(c, 18);
		}
	};

	// 받침 ㅎ + ㅎ이 아닌 초성이 연철된 경우
	struct ContinualCodaDecomposer
	{
		static constexpr size_t boundaryId = 3;
		char16_t onsetToCoda(char16_t c)
		{
			static constexpr char16_t onsetToCoda[] = {
				0, // ㄱ
				0, // ㄲ
				0, // ㄴ
				0, // ㄷ
				0, // ㄸ
				0, // ㄹ
				0, // ㅁ
				0, // ㅂ
				0, // ㅃ
				0, // ㅅ
				0, // ㅆ
				0, // ㅇ
				0, // ㅈ
				0, // ㅉ
				0x11C2, // ㅊ->ㅎ
				0x11C2, // ㅋ->ㅎ
				0x11C2, // ㅌ->ㅎ
				0x11C2, // ㅍ->ㅎ
				0, // ㅎ
			};

			if (isHangulSyllable(c))
			{
				int onset = (c - 0xAC00) / 28 / 21;
				return onsetToCoda[onset];
			}
			return 0;
		}

		char16_t dropRightSyllable(char16_t c)
		{
			const int onset = (c - 0xAC00) / 28 / 21;
			const int vowel = (c - 0xAC00) / 28 % 21;
			const int coda = (c - 0xAC00) % 28;
			static constexpr char16_t onsetMap[] = {
				0, // ㄱ
				0, // ㄲ
				0, // ㄴ
				0, // ㄷ
				0, // ㄸ
				0, // ㄹ
				0, // ㅁ
				0, // ㅂ
				0, // ㅃ
				0, // ㅅ
				0, // ㅆ
				0, // ㅇ
				0, // ㅈ
				0, // ㅉ
				12, // ㅊ->ㅈ
				0, // ㅋ->ㄱ
				3, // ㅌ->ㄷ
				7, // ㅍ->ㅂ
				0, // ㅎ
			};
			return 0xAC00 + (onsetMap[onset] * 21 + vowel) * 28 + coda;
		}
	};

	template<ArchType arch, class Decomposer, bool typoTolerant, bool continualTypoTolerant, bool lengtheningTypoTolerant>
	inline void insertContinualTypoNode(
		Vector<FormCandidate<typoTolerant, continualTypoTolerant, lengtheningTypoTolerant>>& candidates,
		Vector<pair<size_t, const utils::FrozenTrie<kchar_t, const Form*>::Node*>>& continualTypoRightNodes,
		Decomposer decomposer,
		float continualTypoCost,
		char16_t c,
		const Form* formBase,
		const size_t* typoPtrs,
		const utils::FrozenTrie<kchar_t, const Form*>& trie,
		U16StringView str,
		const Vector<uint32_t>& nonSpaces,
		const utils::FrozenTrie<kchar_t, const Form*>::Node* curNode,
		Dialect allowedDialect
	)
	{
		if (!continualTypoTolerant) return;
		static constexpr size_t posMultiplier = continualTypoTolerant ? 4 : 1;

		const char16_t codaFromContinual = decomposer.onsetToCoda(c),
			droppedSyllable = decomposer.dropRightSyllable(c);
		if (!codaFromContinual || !droppedSyllable) return;

		const auto boundary = (nonSpaces.size() - 1) * posMultiplier + Decomposer::boundaryId;
		bool inserted = false;
		auto* contNode = curNode->template nextOpt<arch>(trie, codaFromContinual);
		while (!contNode)
		{
			curNode = curNode->fail();
			if (!curNode) break;
			contNode = curNode->template nextOpt<arch>(trie, codaFromContinual);
		}

		if (!contNode) return;

		for (auto submatcher = contNode; submatcher; submatcher = submatcher->fail())
		{
			const Form* cand = submatcher->val(trie);
			if (!cand) break;
			else if (!trie.hasSubmatch(cand))
			{
				if (getForm<typoTolerant>(cand, formBase).form.size() <= 1) break;
				inserted = true;
				if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, allowedDialect, 0, boundary, continualTypoCost / 2)) break;
			}
		}

		if (!inserted) return;

		if (auto* dropNode = trie.root()->template nextOpt<arch>(trie, droppedSyllable))
		{
			continualTypoRightNodes.emplace_back(boundary, dropNode);
		}
	}
}

inline bool isDiscontinuous(POSTag prevTag, POSTag curTag, ScriptType prevScript, ScriptType curScript)
{
	if ((prevTag == POSTag::sl || prevTag == POSTag::sh || prevTag == POSTag::sw) &&
		(curTag == POSTag::sl || curTag == POSTag::sh || curTag == POSTag::sw))
	{
		return prevScript != curScript;
	}
	return prevTag != curTag;
}

using TrieNode = typename utils::FrozenTrie<kchar_t, const Form*>::Node;

template<bool lengtheningTypoTolerant>
struct LengtheningTypoNodes
{
	int getLengtheningTypoNodes() const // dummy
	{
		return 0;
	}
};

template<>
struct LengtheningTypoNodes<true>
{
	Vector<pair<size_t, const TrieNode*>> lengtheningTypoNodes;

	const Vector<pair<size_t, const TrieNode*>>& getLengtheningTypoNodes() const
	{
		return lengtheningTypoNodes;
	}
};

template<bool lengtheningTypoTolerant>
struct FormCandidate2
{
	const Form* form = nullptr;

	FormCandidate2(const Form* _form = nullptr, size_t _lengtheningSize = 0)
		: form{ _form }
	{
	}

	size_t getStartPos(size_t currentPos) const
	{
		return currentPos - form->sizeWithoutSpace();
	}

	size_t getEndPos(size_t currentPos) const
	{
		return currentPos;
	}

	size_t getFormSizeWithTypos(const size_t*) const
	{
		return form->form.size();
	}

	bool operator==(const Form* f) const
	{
		return form == f;
	}

	float getTypoCost(float lengtheningTypoCost) const
	{
		return 0;
	}
};

template<>
struct FormCandidate2<true>
{
	const Form* form = nullptr;
	size_t lengtheningSize = 0;

	FormCandidate2(const Form* _form = nullptr, size_t _lengtheningSize = 0)
		: form{ _form }, lengtheningSize{ _lengtheningSize }
	{
	}

	size_t getStartPos(size_t currentPos) const
	{
		return currentPos - form->sizeWithoutSpace() - lengtheningSize;
	}

	size_t getEndPos(size_t currentPos) const
	{
		return currentPos;
	}

	size_t getFormSizeWithTypos(const size_t*) const
	{
		return form->form.size();
	}

	bool operator==(const Form* f) const
	{
		return form == f;
	}

	float getTypoCost(float lengtheningTypoCost) const
	{
		return lengtheningSize > 0 ? lengtheningTypoCost * (3 + lengtheningSize) : 0;
	}
};

template<bool lengtheningTypoTolerant>
struct SearchState : public LengtheningTypoNodes<lengtheningTypoTolerant>
{
	const TrieNode* node;
	float accumulatedCost;
	uint32_t minFormLen;
	int32_t startPosOffset;
	uint32_t specialStartNsPos;
	uint32_t unkFormStartNsPos;
	char32_t lastChr;
	uint16_t startContinualTypoIdx;

	SearchState(
		const TrieNode* _node = nullptr,
		float _accumulatedCost = 0,
		uint32_t _minFormLen = 0,
		int32_t _startPosOffset = 0,
		uint32_t _specialStartNsPos = 0,
		uint32_t _unkFormStartPos = 0,
		char32_t _lastChr = 0,
		uint16_t _startContinualTypoIdx = 0
		)
		:
		node{ _node },
		accumulatedCost{ _accumulatedCost },
		minFormLen{ _minFormLen },
		startPosOffset{ _startPosOffset },
		specialStartNsPos{ _specialStartNsPos },
		unkFormStartNsPos{ _unkFormStartPos },
		lastChr{ _lastChr },
		startContinualTypoIdx{ _startContinualTypoIdx }
	{
	}
};

class Splitter
{
public:
	inline static thread_local Vector<pair<uint32_t, uint32_t>> endPosMap;
	inline static thread_local Vector<pair<uint32_t, uint32_t>> pretokenizedSpans;
	inline static thread_local Vector<tuple<size_t, uint32_t, POSTag>> matchedPatterns; // end, length, type
	inline static thread_local Vector<uint32_t> nsToPos, posToNs;
	inline static thread_local Vector<KGraphNode> out;
	inline static thread_local Vector<TypoGraphNode> typoGraph;
	inline static thread_local Vector<Vector<SearchState<false>>> _searchStates;
	inline static thread_local Vector<FormCandidate2<false>> _candidates;

	const Form* formBase;
	const size_t* typoPtrs;
	Match matchOptions;
	Dialect allowedDialect;
	size_t maxUnkFormSize;
	size_t maxUnkFormSizeFollowedByJClass;
	size_t spaceTolerance;
	float typoThreshold;
	const PretokenizedSpanGroup::Span* nextPretokenizedPattern;
	const PretokenizedSpanGroup::Span* lastPretokenizedPattern;

	U16StringView rawStr;
	size_t posMultiplierBit = 0;
	float continualTypoCost = 0;
	float lengtheningTypoCost = 0;
	Vector<tuple<size_t, uint32_t, POSTag>>::const_iterator nextMatchedPattern;

	void init(
		const Form* formBase,
		const size_t* typoPtrs,
		Match matchOptions,
		Dialect allowedDialect,
		size_t maxUnkFormSize,
		size_t maxUnkFormSizeFollowedByJClass,
		size_t spaceTolerance,
		float typoThreshold
	)
	{
		endPosMap.clear();
		pretokenizedSpans.clear();
		matchedPatterns.clear();
		nsToPos.clear();
		posToNs.clear();
		out.clear();
		typoGraph.clear();

		this->formBase = formBase;
		this->typoPtrs = typoPtrs;
		this->matchOptions = matchOptions;
		this->allowedDialect = allowedDialect;
		this->maxUnkFormSize = maxUnkFormSize;
		this->maxUnkFormSizeFollowedByJClass = maxUnkFormSizeFollowedByJClass;
		this->spaceTolerance = spaceTolerance;
		this->typoThreshold = typoThreshold;
	}

	size_t preparePattern(
		U16StringView str,
		size_t startOffset,
		const PretokenizedSpanGroup::Span*& pretokenizedFirst,
		const PretokenizedSpanGroup::Span* pretokenizedLast
	)
	{
		nsToPos.reserve(str.size());
		posToNs.reserve(str.size());
		nextPretokenizedPattern = pretokenizedFirst;
		lastPretokenizedPattern = pretokenizedLast;
		size_t n = 0;
		POSTag lastChrType = POSTag::unknown;
		for (; n < str.size(); ++n)
		{
			// Pretokenized 매칭
			if (pretokenizedFirst < pretokenizedLast && pretokenizedFirst->begin == n + startOffset)
			{
				const size_t length = pretokenizedFirst->end - pretokenizedFirst->begin;
				n += length - 1;
				pretokenizedSpans.emplace_back(pretokenizedFirst->begin - startOffset, pretokenizedFirst->end - startOffset);
				pretokenizedFirst++;
				continue;
			}

			// 패턴 매칭
			{
				auto [matchedLength, matchedType] = matchPattern(n ? str[n - 1] : u' ', str.data() + n, str.data() + str.size(), matchOptions);
				if (matchedType != POSTag::unknown)
				{
					matchedPatterns.emplace_back(n + matchedLength, matchedLength, matchedType);
					n += matchedLength - 1;
					continue;
				}
			}

			char16_t c = str[n];
			char32_t c32 = c;
			if (isHighSurrogate(c32) && n + 1 < str.size())
			{
				c32 = mergeSurrogate(c32, str[n + 1]);
			}

			const POSTag chrType = identifySpecialChr(c32);
			// 문장 종결 지점이 나타나거나 Graph가 너무 길어지면 공백 문자에서 중단
			if (chrType == POSTag::unknown && ((lastChrType == POSTag::sf && n >= 4) || n > 4096))
			{
				if (!isSpace(str[n - 3]) && !isSpace(str[n - 2]))
				{
					break;
				}
			}
			else if (n >= 8192)
			{
				break;
			}
			
			if (c32 >= 0x10000) ++n;
			lastChrType = chrType;
		}

		for (size_t i = 0; i < n; ++i)
		{
			if (!isSpace(str[i]))
			{
				posToNs.emplace_back(nsToPos.size());
				nsToPos.emplace_back(i);
				if (isHighSurrogate(str[i]) && i + 1 < n)
				{
					posToNs.emplace_back(nsToPos.size());
					nsToPos.emplace_back(++i);
				}
			}
			else
			{
				posToNs.emplace_back(nsToPos.size());
			}
		}
		posToNs.emplace_back(nsToPos.size());
		sort(matchedPatterns.begin(), matchedPatterns.end());
		nextMatchedPattern = matchedPatterns.begin();
		return n;
	}

	void buildTypoGraph(
		U16StringView str,
		const PreparedTypoTransformer* typoTransformer
	)
	{
		rawStr = str;
		if (typoTransformer)
		{
			typoTransformer->generateGraph(str, typoGraph, pretokenizedSpans.data(), pretokenizedSpans.data() + pretokenizedSpans.size());
		}
		else
		{
			typoGraph.emplace_back(str.substr(0, 0), 0);
			typoGraph.emplace_back(str, str.size(), 0, 1);
		}
		posMultiplierBit = typoTransformer && isfinite(typoTransformer->getContinualTypoCost()) ? 3 : 0;
		continualTypoCost = typoTransformer ? typoTransformer->getContinualTypoCost() : INFINITY;
		lengtheningTypoCost = typoTransformer ? typoTransformer->getLengtheningTypoCost() : INFINITY;

		endPosMap.resize((nsToPos.size() << posMultiplierBit) + 1, make_pair(-1, -1));
		endPosMap[0] = make_pair(0, 1);
		out.emplace_back();
	}

	bool hasFormAlready(size_t multipliedStartPos, size_t multipliedEndPos) const
	{
		const auto scanStart = max(endPosMap[multipliedEndPos].first, (uint32_t)1), scanEnd = endPosMap[multipliedEndPos].second;
		return any_of(out.begin() + scanStart, out.begin() + scanEnd, [&](const KGraphNode& g)
		{
			const size_t startPos = g.endPos - (g.uform.empty() ? g.form->sizeWithoutSpace() : g.uform.size()) << posMultiplierBit;
			return g.endPos == multipliedEndPos && startPos == multipliedStartPos;
		});
	}

	pair<bool, bool> isZFollowable(size_t pos) const
	{
		if (pos >= nsToPos.size()) return make_pair(false, false);
		const auto scanStart = endPosMap[pos << posMultiplierBit].first, scanEnd = endPosMap[pos << posMultiplierBit].second;
		bool zCodaFollowable = false, zSiotFollowable = false;
		for (auto it = out.begin() + scanStart; it != out.begin() + scanEnd; ++it)
		{
			if (it->endPos != (pos << posMultiplierBit) || !it->form) continue;
			zCodaFollowable = zCodaFollowable || it->form->zCodaAppendable;
			zSiotFollowable = zSiotFollowable || it->form->zSiotAppendable;
		}
		return make_pair(zCodaFollowable, zSiotFollowable);
	}

	void insertUnkForm(size_t startPos, size_t endPos, bool hasJClass, bool insertAlways = false)
	{
		if (startPos >= endPos
			|| hasFormAlready(startPos << posMultiplierBit, endPos << posMultiplierBit))
		{
			return;
		}

		size_t lastPos = out.back().endPos;
		if (lastPos < endPos)
		{
			if (lastPos && isHangulCoda(rawStr[nsToPos[lastPos]])) lastPos--; // prevent coda to be matched alone.
			if (lastPos != startPos && !hasFormAlready(lastPos << posMultiplierBit, endPos << posMultiplierBit))
			{
				auto str = rawStr.substr(nsToPos[lastPos], nsToPos[endPos - 1] + 1 - nsToPos[lastPos]);
				while (!str.empty() && isSpace(str.back())) str = str.substr(0, str.size() - 1);
				appendNewNode(out, endPosMap,
					lastPos << posMultiplierBit, endPos << posMultiplierBit,
					str);
			}
		}

		const size_t newNodeLength = endPos - startPos;
		const size_t lengthLimit = hasJClass ? maxUnkFormSizeFollowedByJClass : maxUnkFormSize;
		if (insertAlways || newNodeLength <= lengthLimit)
		{
			auto str = rawStr.substr(nsToPos[startPos], nsToPos[endPos - 1] + 1 - nsToPos[startPos]);
			while (!str.empty() && isSpace(str.back())) str = str.substr(0, str.size() - 1);
			appendNewNode(out, endPosMap,
				startPos << posMultiplierBit, endPos << posMultiplierBit,
				str);
		}
	}

	template<bool lengtheningTypoTolerant>
	void flushCandidates(size_t endPosition, ptrdiff_t startPosOffset, 
		size_t unkFormStartNsPos, float typoCost,
		uint8_t startContinualTypoIdx, uint8_t endContinualTypoIdx)
	{
		auto& candidates = reinterpret_cast<Vector<FormCandidate2<lengtheningTypoTolerant>>&>(_candidates);
		for (const auto& cand : candidates)
		{
			const size_t nBegin = cand.getStartPos(endPosition) + startPosOffset;
			const size_t nEnd = cand.getEndPos(endPosition);
			const size_t lengthWithSpaces = countChrWithNormalizedSpace(nsToPos.begin() + nBegin, nsToPos.begin() + nEnd);
			const size_t formSizeWithTypos = cand.getFormSizeWithTypos(typoPtrs);

			// insert unknown form 
			if (startContinualTypoIdx == 0
				&& !isHangulCoda(cand.form->form[0]))
			{
				insertUnkForm(unkFormStartNsPos, nBegin, cand.form->hasJClass);
			}

			size_t spaceErrors = 0;
			if ((spaceErrors = countSpaceErrors(cand.form->form, &nsToPos[nBegin], &nsToPos[nEnd])) <= spaceTolerance)
			{
				//if (!cand.form->numSpaces && lengthWithSpaces > formSizeWithTypos) spaceErrors = lengthWithSpaces - formSizeWithTypos; // 오타교정과 섞여있을 경우 spaceErrors 계산 고도화 필요.
				const size_t nBeginWithCT = startContinualTypoIdx ? (nBegin << posMultiplierBit) + startContinualTypoIdx : nBegin << posMultiplierBit;
				const size_t nEndWithCT = endContinualTypoIdx ? ((nEnd - 1) << posMultiplierBit) + endContinualTypoIdx : nEnd << posMultiplierBit;
				if (appendNewNode(out, endPosMap,
					nBeginWithCT, nEndWithCT,
					cand.form, typoCost + cand.getTypoCost(lengtheningTypoCost)))
				{
					out.back().spaceErrors = spaceErrors;
				}
			}
		}
		candidates.clear();
	}

	template<ArchType arch, bool lengtheningTypoTolerant>
	void progressNode(
		const utils::FrozenTrie<kchar_t, const Form*>& trie,
		const TypoGraphNode& prevTypoNode,
		const TypoGraphNode& typoNode,
		const SearchState<lengtheningTypoTolerant>& state,
		Vector<SearchState<lengtheningTypoTolerant>>& curStates,
		size_t startOffset
	)
	{
		const Form* const fallbackFormBegin = trie.value((size_t)POSTag::nng);
		const Form* const fallbackFormEnd = trie.value((size_t)POSTag::max);

		float typoCost = state.accumulatedCost;
		typoCost += (typoNode.typoCost * (
			typoNode.continualTypoIdx || prevTypoNode.continualTypoIdx ? continualTypoCost : 1.f
		));
		if (typoCost > typoThreshold)
		{
			return;
		}
		auto& candidates = reinterpret_cast<Vector<FormCandidate2<lengtheningTypoTolerant>>&>(_candidates);
		char32_t prevChr = state.lastChr;
		POSTag lastChrType = prevChr ? identifySpecialChr(prevChr) : POSTag::unknown;
		ScriptType lastScriptType = prevChr ? chr2ScriptType(prevChr) : ScriptType::unknown;
		size_t specialStartNsPos = state.specialStartNsPos;
		size_t unkFormStartNsPos = state.unkFormStartNsPos;

		size_t minFormLen = state.minFormLen;
		int32_t startPosOffset = state.startPosOffset;
		if (typoNode.typoCost > 0)
		{
			startPosOffset += (ptrdiff_t)typoNode.form.size() - (ptrdiff_t)(typoNode.endPos - prevTypoNode.endPos);
		}

		auto lengtheningTypoNodes = state.getLengtheningTypoNodes();
		auto* curNode = state.node;
		for (size_t j = 0; j < typoNode.form.size(); ++j)
		{
			const char16_t c = typoNode.form[j];
			char32_t c32 = c;
			if (isHighSurrogate(c32) && j + 1 < typoNode.form.size())
			{
				c32 = mergeSurrogate(c32, typoNode.form[j + 1]);
			}

			if (typoCost == 0)
			{
				const bool isInPattern = nextMatchedPattern != matchedPatterns.end() &&
					(typoNode.endPos + j - typoNode.form.size()) >= get<0>(*nextMatchedPattern) - get<1>(*nextMatchedPattern);

				POSTag chrType = identifySpecialChr(c32);
				ScriptType scriptType = chr2ScriptType(c32);
				if (lastChrType == POSTag::sw &&
					(c32 == 0x200d || // zero width joiner
						(0x1f3fb <= c32 && c32 <= 0x1f3ff) || // skin color modifier
						scriptType == ScriptType::variation_selectors)) // variation selectors
				{
					chrType = lastChrType;
					scriptType = lastScriptType;
				}

				if (isDiscontinuous(lastChrType, isInPattern ? POSTag::unknown : chrType, lastScriptType, scriptType)
					|| lastChrType == POSTag::sso || lastChrType == POSTag::ssc)
				{
					if (lastChrType != POSTag::max && lastChrType != POSTag::unknown)
					{
						if (lastChrType != POSTag::ss) // ss 태그는 morpheme 내에 등록된 후보에서 직접 탐색하도록 한다
						{
							insertUnkForm(
								unkFormStartNsPos,
								specialStartNsPos,
								false);
							auto str = rawStr.substr(nsToPos[specialStartNsPos], (typoNode.endPos + j - typoNode.form.size()) - nsToPos[specialStartNsPos]);
							while (!str.empty() && isSpace(str.back())) str = str.substr(0, str.size() - 1);
							if (appendNewNode(out, endPosMap,
								specialStartNsPos << posMultiplierBit,
								(posToNs[typoNode.endPos + j - 1 - typoNode.form.size()] + 1) << posMultiplierBit,
								str))
							{
								out.back().form = trie.value((size_t)lastChrType);
							}
						}
					}
					unkFormStartNsPos = specialStartNsPos;
					specialStartNsPos = posToNs[typoNode.endPos + j - typoNode.form.size()];
				}
				else if (chrType == POSTag::max)
				{
					unkFormStartNsPos = specialStartNsPos;
				}

				lastChrType = isInPattern ? POSTag::unknown : chrType;
				lastScriptType = scriptType;

				if (c32 >= 0x10000)
				{
				}
				else
				{
					// 공백 문자
					if (chrType == POSTag::unknown)
					{
						insertUnkForm(
							unkFormStartNsPos,
							posToNs[typoNode.endPos + j + 1 - typoNode.form.size()],
							false, true);
						unkFormStartNsPos = posToNs[typoNode.endPos + j + 1 - typoNode.form.size()];
						continue;
					}

					const size_t pos = typoNode.endPos + j - typoNode.form.size();
					const auto [zCodaFollowable, zSiotFollowable] = isZFollowable(posToNs[pos]);
					if (!!(matchOptions & Match::zCoda) && zCodaFollowable && isHangulCoda(c) && (pos + 1 >= rawStr.size() || !isHangulSyllable(rawStr[pos + 1])))
					{
						candidates.emplace_back(formBase + defaultTagSize + (c - 0x11A8) - 1);
					}
					else if (!!(matchOptions & (Match::splitSaisiot | Match::mergeSaisiot)) && zSiotFollowable && c == 0x11BA && pos + 1 < rawStr.size() && isHangulSyllable(rawStr[pos + 1]))
					{
						candidates.emplace_back(formBase + defaultTagSize + (0x11BA - 0x11A8) - 1);
					}
				}
			}

			if (typoNode.typoCost == 0 && nextMatchedPattern != matchedPatterns.end())
			{
				const auto currentEnd = typoNode.endPos + j + (c32 >= 0x10000 ? 2 : 1) - typoNode.form.size();
				while (nextMatchedPattern != matchedPatterns.end() && get<0>(*nextMatchedPattern) == currentEnd)
				{
					const auto [matchedEnd, matchedLength, matchedType] = *nextMatchedPattern;
					const auto matchedStart = matchedEnd - matchedLength;
					insertUnkForm(
						unkFormStartNsPos,
						posToNs[matchedStart],
						false);
					if (appendNewNode(out, endPosMap,
						posToNs[matchedStart] << posMultiplierBit,
						(posToNs[matchedEnd - 1] + 1) << posMultiplierBit,
						rawStr.substr(matchedStart, matchedEnd - matchedStart)
					))
					{
						out.back().form = trie.value((size_t)matchedType);
					}
					++nextMatchedPattern;
				}
			}
			if (typoNode.typoCost == 0 && nextPretokenizedPattern != lastPretokenizedPattern
				&& nextPretokenizedPattern->begin - startOffset == typoNode.endPos + j - typoNode.form.size())
			{
				insertUnkForm(
					unkFormStartNsPos,
					posToNs[nextPretokenizedPattern->begin - startOffset],
					false);
				if (appendNewNode(out, endPosMap,
					posToNs[nextPretokenizedPattern->begin - startOffset] << posMultiplierBit,
					(posToNs[nextPretokenizedPattern->end - startOffset - 1] + 1) << posMultiplierBit,
					nextPretokenizedPattern->form
				))
				{
					if (within(nextPretokenizedPattern->form, fallbackFormBegin, fallbackFormEnd))
					{
						out.back().uform = rawStr.substr(nextPretokenizedPattern->begin - startOffset, nextPretokenizedPattern->end - nextPretokenizedPattern->begin);
					}
				}
				
				j += (nextPretokenizedPattern->end - nextPretokenizedPattern->begin) - 1;
				++nextPretokenizedPattern;
				lastChrType = POSTag::unknown;
				curNode = trie.root();
				if constexpr (lengtheningTypoTolerant)
				{
					lengtheningTypoNodes.clear();
				}
				specialStartNsPos = unkFormStartNsPos = posToNs[typoNode.endPos + j + 1 - typoNode.form.size()];
				continue;
			}
			if (c32 >= 0x10000)
			{
				++j;
				prevChr = c32;
				continue;
			}

			if constexpr (lengtheningTypoTolerant)
			{
				static uint8_t lengtheningVowelTable[] = {
					0, // ㅏ
					1, // ㅐ
					0, // ㅑ
					1, // ㅒ
					4, // ㅓ
					5, // ㅔ
					4, // ㅕ
					5, // ㅖ
					8, // ㅗ
					0, // ㅘ
					1, // ㅙ
					1, // ㅚ
					8, // ㅛ
					13, // ㅜ
					4, // ㅝ
					5, // ㅞ
					20, // ㅟ
					13, // ㅠ
					18, // ㅡ
					20, // ㅢ
					20, // ㅣ
				};

				const size_t prevLengtheningSize = lengtheningTypoNodes.size();
				if (prevChr && isHangulSyllable(prevChr) &&
					(u'아' <= c && c < u'자') && lengtheningVowelTable[extractVowel(prevChr)] == extractVowel(c))
				{
					lengtheningTypoNodes.emplace_back(1, curNode);
					for (size_t i = 0; i < prevLengtheningSize; ++i)
					{
						auto& node = lengtheningTypoNodes[i];
						lengtheningTypoNodes.emplace_back(node.first + 1, node.second);
					}
				}

				size_t outputIdx = 0;
				for (size_t i = 0; i < prevLengtheningSize; ++i)
				{
					auto& node = lengtheningTypoNodes[i];
					node.second = node.second->template nextOpt<arch>(trie, c);
					if (!node.second) continue;
					if (find(lengtheningTypoNodes.begin(), lengtheningTypoNodes.begin() + outputIdx, node) != lengtheningTypoNodes.begin() + outputIdx) continue;
					lengtheningTypoNodes[outputIdx++] = node;
				}
				for (size_t i = prevLengtheningSize; i < lengtheningTypoNodes.size(); ++i)
				{
					auto& node = lengtheningTypoNodes[i];
					if (find(lengtheningTypoNodes.begin(), lengtheningTypoNodes.begin() + outputIdx, node) != lengtheningTypoNodes.begin() + outputIdx) continue;
					lengtheningTypoNodes[outputIdx++] = node;
				}
				lengtheningTypoNodes.erase(lengtheningTypoNodes.begin() + outputIdx, lengtheningTypoNodes.end());
			}
			prevChr = c32;

			if (minFormLen > 0 || typoNode.typoCost > 0) ++minFormLen;
			auto* nextNode = curNode->template nextOpt<arch>(trie, c);
			while (!nextNode)
			{
				curNode = curNode->fail();
				if (!curNode) break;
				nextNode = curNode->template nextOpt<arch>(trie, c);
			}
			if (nextNode)
			{
				curNode = nextNode;
				// 오타가 있는 경우 전체 형태가 포함된 후보만 탐색.
				if (typoNode.typoCost == 0 || j == typoNode.form.size() - 1)
				{
					if (typoCost > 0 && curNode->depth < minFormLen)
					{
						// early pruning
					}
					else
					{
						for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
						{
							const Form* cand = submatcher->val(trie);
							if (!cand) break;
							else if (!trie.hasSubmatch(cand))
							{
								if (cand->form.size() < minFormLen) break;
								candidates.emplace_back(cand);
							}
						}
					}

					if constexpr (lengtheningTypoTolerant)
					{
						for (auto [lengtheningSize, node] : lengtheningTypoNodes)
						{
							const Form* cand = node->val(trie);
							if (cand && !trie.hasSubmatch(cand))
							{
								if (cand->form.size() < minFormLen) continue;
								candidates.emplace_back(cand, lengtheningSize);
							}
						}
					}
				}
			}
			else
			{
				if constexpr (lengtheningTypoTolerant)
				{
					lengtheningTypoNodes.clear();
				}

				if (typoCost == 0)
				{
					curNode = trie.root();
				}
				else
				{
					return;
				}
			}

			const size_t endPos = typoNode.endPos + j + 1 - typoNode.form.size();
			flushCandidates<lengtheningTypoTolerant>(posToNs[endPos - 1] + 1, startPosOffset, unkFormStartNsPos, typoCost, state.startContinualTypoIdx, typoNode.continualTypoIdx);
		}
		if (typoCost == 0 && lastChrType != POSTag::max && lastChrType != POSTag::unknown)
		{
			if (lastChrType != POSTag::ss) // ss 태그는 morpheme 내에 등록된 후보에서 직접 탐색하도록 한다
			{
				insertUnkForm(
					unkFormStartNsPos,
					specialStartNsPos,
					false);
				auto str = rawStr.substr(nsToPos[specialStartNsPos], typoNode.endPos - nsToPos[specialStartNsPos]);
				while (!str.empty() && isSpace(str.back())) str = str.substr(0, str.size() - 1);
				if (appendNewNode(out, endPosMap,
					specialStartNsPos << posMultiplierBit,
					(posToNs[typoNode.endPos - 1] + 1) << posMultiplierBit,
					str))
				{
					out.back().form = trie.value((size_t)lastChrType);
				}
				unkFormStartNsPos = specialStartNsPos;
			}
		}

		if (curNode)
		{
			if (typoNode.continualTypoIdx)
			{
				curNode = trie.root();
				typoCost = 0;
				minFormLen = 0;
				startPosOffset = -1;
				if (!curStates.empty()) return;
				if constexpr (lengtheningTypoTolerant) lengtheningTypoNodes.clear();
			}
			if (typoCost > 0 && curNode->depth < minFormLen && !lengtheningTypoTolerant)
			{
				// early pruning
			}
			else
			{
				curStates.emplace_back(curNode, typoCost, minFormLen, startPosOffset, specialStartNsPos, unkFormStartNsPos, prevChr, typoNode.continualTypoIdx ? typoNode.continualTypoIdx : state.startContinualTypoIdx);
			}
			if constexpr (lengtheningTypoTolerant)
			{
				curStates.back().lengtheningTypoNodes = std::move(lengtheningTypoNodes);
			}
		}
	}

	template<ArchType arch, bool lengtheningTypoTolerant>
	void search(const utils::FrozenTrie<kchar_t, const Form*>& trie, size_t startOffset)
	{
		const size_t totEndPos = nsToPos.back() + 1;
		auto& searchStates = reinterpret_cast<Vector<Vector<SearchState<lengtheningTypoTolerant>>>&>(_searchStates);
		searchStates.resize(typoGraph.size());
		searchStates[0].emplace_back(trie.root());
		for (size_t i = 1; i < typoGraph.size(); ++i)
		{
			const auto& typoNode = typoGraph[i];
			auto& curStates = searchStates[i];
			for (auto* prev = typoNode.getPrev(); prev; prev = prev->getSibling())
			{
				const auto& prevStates = searchStates[prev - typoGraph.data()];
				for (auto& state : prevStates)
				{
					progressNode<arch, lengtheningTypoTolerant>(trie, *prev, typoNode, state, curStates, startOffset);
				}
			}

			if (typoNode.typoCost == 0 && typoNode.endPos == totEndPos)
			{
				// this is end of input node with no typo
				for (auto& state : curStates)
				{
					insertUnkForm(state.unkFormStartNsPos, 
						posToNs[totEndPos - 1] + 1, false, true);
				}
			}
		}
		appendNewNode(out, endPosMap, (nsToPos.size() << posMultiplierBit), (nsToPos.size() << posMultiplierBit) + 1, nullptr);
		out.back().endPos = nsToPos.size();
		searchStates.clear();
	}

	void writeResult(Vector<KGraphNode>& ret, size_t startOffset, size_t stopOffset)
	{
		removeUnconnected(ret, out, endPosMap);
		for (size_t i = 1; i < ret.size() - 1; ++i)
		{
			auto& r = ret[i];
			r.startPos = nsToPos[r.startPos >> posMultiplierBit] + startOffset;
			r.endPos = nsToPos[((r.endPos + (1 << posMultiplierBit) - 1) >> posMultiplierBit) - 1] + 1 + startOffset;
		}
		ret.back().startPos = ret.back().endPos = startOffset + stopOffset;
	}
};

template<ArchType arch>
size_t splitByTrieUsingTypo(
	Vector<KGraphNode>& ret,
	const Form* formBase,
	const size_t* typoPtrs,
	const utils::FrozenTrie<kchar_t, const Form*>& trie,
	U16StringView str,
	size_t startOffset,
	Match matchOptions,
	Dialect allowedDialect,
	size_t maxUnkFormSize,
	size_t maxUnkFormSizeFollowedByJClass,
	size_t spaceTolerance,
	const PreparedTypoTransformer* typoTransformer,
	float typoThreshold,
	const PretokenizedSpanGroup::Span*& pretokenizedFirst,
	const PretokenizedSpanGroup::Span* pretokenizedLast
)
{
	Splitter splitter;
	splitter.init(
		formBase,
		typoPtrs,
		matchOptions,
		allowedDialect,
		maxUnkFormSize,
		maxUnkFormSizeFollowedByJClass,
		spaceTolerance,
		typoThreshold
	);

	size_t stopPos = splitter.preparePattern(
		str, 
		startOffset, 
		pretokenizedFirst,
		pretokenizedLast);
	if (splitter.nsToPos.empty())
	{
		ret.emplace_back();
		ret.emplace_back(0, 0, nullptr);
		while (stopPos < str.size() && isSpace(str[stopPos])) ++stopPos;
		return stopPos + startOffset;
	}
	splitter.buildTypoGraph(str.substr(0, stopPos), typoTransformer);
	if (isfinite(splitter.lengtheningTypoCost))
	{
		splitter.search<arch, true>(trie, startOffset);
	}
	else
	{
		splitter.search<arch, false>(trie, startOffset);
	}
	splitter.writeResult(ret, startOffset, stopPos);
	return stopPos + startOffset;
}

template<ArchType arch,
	bool typoTolerant,
	bool continualTypoTolerant,
	bool lengtheningTypoTolerant
>
size_t kiwi::splitByTrie(
	Vector<KGraphNode>& ret,
	const Form* formBase,
	const size_t* typoPtrs,
	const utils::FrozenTrie<kchar_t, const Form*>& trie,
	U16StringView str,
	size_t startOffset,
	Match matchOptions,
	Dialect allowedDialect,
	size_t maxUnkFormSize,
	size_t maxUnkFormSizeFollowedByJClass,
	size_t spaceTolerance,
	const PreparedTypoTransformer* typoTransformer,
	float typoThreshold,
	float continualTypoCost,
	float lengtheningTypoCost,
	const PretokenizedSpanGroup::Span*& pretokenizedFirst,
	const PretokenizedSpanGroup::Span* pretokenizedLast
)
{
	if (!(matchOptions & Match::useOldSplitter)) return splitByTrieUsingTypo<arch>(ret, formBase, typoPtrs, trie, str, startOffset, matchOptions, allowedDialect, maxUnkFormSize, maxUnkFormSizeFollowedByJClass, spaceTolerance, typoTransformer, typoThreshold, pretokenizedFirst, pretokenizedLast);
	/*
	* posMultiplier는 연철 교정 모드(continualTypoTolerant)에서 사용된다.
	* 이 경우 음절 경계로 분할되는 형태소들은 모두 4의 배수로 인덱싱되고
	* 연철되어 두 음절에 걸쳐 있는 형태소들은 4n + 1, 2, 3으로 인덱싱된다.
	* 4n + 1: 받침 + 초성 ㅇ이 연철되어 결합한 경우(ex: 사람이 -> 사라미)
	* 4n + 2: 받침 + 초성 ㅎ이 연철되어 결합한 경우(ex: 급하다 -> 그파다)
	* 4n + 3: ㅎ 받침 + ㅎ이 아닌 초성이 연철되어 결합한 경우(ex: 않다 -> 안타)
	*
	* 연철 교정 모드가 사용되지 않을 경우
	* '사라ㅁ이'에서 형태소 '사라ㅁ'과 '이'의 (시작, 끝지점)은 각각 (0, 3), (3, 4)가 된다.
	* 연철 교정 모드가 사용될 경우
	* '사라ㅁ이'에서 형태소 '사라ㅁ'과 '이'의 (시작, 끝지점)은 각각 (0, 12), (12, 16)가 된다.
	* 그리고 '사라미'에서는 형태소 '사라ㅁ'은 (0, 9), '이'는 (9, 12)가 된다.
	*/
	static constexpr size_t posMultiplier = continualTypoTolerant ? 4 : 1;

	/*
	* endPosMap[i]에는 out[x].endPos == i를 만족하는 첫번째 x(first)와 마지막 x + 1(second)가 들어 있다.
	* first == second인 경우 endPos가 i인 노드가 없다는 것을 의미한다.
	* first <= x && x < second인 out[x] 중에는 endPos가 i가 아닌 것도 있을 수 있으므로 주의해야 한다.
	*/
	thread_local Vector<pair<uint32_t, uint32_t>> endPosMap;
	endPosMap.clear();
	endPosMap.resize(str.size() * posMultiplier + 1, make_pair<uint32_t, uint32_t>(-1, -1));
	endPosMap[0] = make_pair(0, 1);
	
	thread_local Vector<uint32_t> nonSpaces;
	nonSpaces.clear();
	nonSpaces.reserve(str.size() * posMultiplier);

	thread_local Vector<KGraphNode> out;
	out.clear();
	out.emplace_back();
	size_t n = 0;
	Vector<FormCandidate<typoTolerant, continualTypoTolerant, lengtheningTypoTolerant>> candidates;
	using NodePtrTy = decltype(trie.root());
	auto* curNode = trie.root();
	auto* curNodeForTypo = trie.root();
	auto* nextNode = trie.root();
	Vector<pair<size_t, NodePtrTy>> continualTypoRightNodes;
	Vector<pair<size_t, NodePtrTy>> lengtheningTypoNodes;

	size_t lastSpecialEndPos = 0, specialStartPos = 0;
	POSTag chrType, lastChrType = POSTag::unknown, lastMatchedPattern = POSTag::unknown;
	ScriptType scriptType, lastScriptType = ScriptType::unknown;
	auto flushBranch = [&](size_t unkFormEndPos = 0, size_t unkFormEndPosWithSpace = 0, bool specialMatched = false)
	{
		if (!candidates.empty())
		{
			bool alreadySpecialChrProcessed = false;
			for (auto& cand : candidates)
			{
				const size_t nBegin = cand.getStartPos(nonSpaces.size() * posMultiplier) / posMultiplier,
					nBeginWithMultiplier = cand.getStartPos(nonSpaces.size() * posMultiplier),
					nEndWithMultiplier = cand.getEndPos(nonSpaces.size() * posMultiplier);
				const auto scanStart = max(endPosMap[nBeginWithMultiplier].first, (uint32_t)1), scanEnd = endPosMap[nBeginWithMultiplier].second;
				const bool longestMatched = scanStart < scanEnd && any_of(out.begin() + scanStart, out.begin() + scanEnd, [&](const KGraphNode& g)
				{
					const size_t startPos = g.endPos - (g.uform.empty() ? g.form->sizeWithoutSpace() : g.uform.size()) * posMultiplier;
					return nBeginWithMultiplier == g.endPos && (lastSpecialEndPos * posMultiplier == startPos || specialStartPos * posMultiplier == startPos);
				});

				// insert unknown form 
				if (nBeginWithMultiplier % posMultiplier == 0
					&& nEndWithMultiplier % posMultiplier == 0
					&& nBegin > lastSpecialEndPos && !longestMatched
					&& !isHangulCoda(cand.form->form[0]))
				{
					{
						size_t lastPos = out.back().endPos;

						if (lastPos < nBegin)
						{
							if (lastPos && isHangulCoda(str[nonSpaces[lastPos]])) lastPos--; // prevent coda to be matched alone.
							if (lastPos != lastSpecialEndPos)
							{
								appendNewNode(out, endPosMap,
									lastPos * posMultiplier, nBegin * posMultiplier,
									str.substr(nonSpaces[lastPos], nonSpaces[nBegin] - nonSpaces[lastPos]));
							}
						}
					}

					const size_t newNodeLength = nBegin - lastSpecialEndPos;
					const size_t lengthLimit = cand.form->hasJClass ? maxUnkFormSizeFollowedByJClass : maxUnkFormSize;
					if (newNodeLength <= lengthLimit)
					{
						appendNewNode(out, endPosMap,
							lastSpecialEndPos * posMultiplier, nBegin * posMultiplier,
							str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[nBegin] - nonSpaces[lastSpecialEndPos]));
					}
				}

				// if special character
				if (cand.form->candidate[0] <= trie.value((size_t)POSTag::sn)->candidate[0])
				{
					// special character should be processed one by one chr.
					if (!alreadySpecialChrProcessed)
					{
						if (appendNewNode(out, endPosMap,
							(nonSpaces.size() - 1) * posMultiplier, nEndWithMultiplier,
							U16StringView{ cand.form->form.data() + cand.form->form.size() - 1, 1 }))
						{
							out.back().form = trie.value((size_t)cand.form->candidate[0]->tag);
						}
						lastSpecialEndPos = nonSpaces.size();
						alreadySpecialChrProcessed = true;
					}
				}
				else
				{
					const size_t lengthWithSpaces = countChrWithNormalizedSpace(nonSpaces.begin() + nBegin, nonSpaces.end());
					const size_t formSizeWithTypos = cand.getFormSizeWithTypos(typoPtrs);
					size_t spaceErrors = 0;
					if (lengthWithSpaces <= formSizeWithTypos + spaceTolerance
						&& (!cand.form->numSpaces || (spaceErrors = countSpaceErrors(cand.form->form, nonSpaces.data() + nBegin, nonSpaces.data() + nonSpaces.size())) <= spaceTolerance))
					{
						if (!cand.form->numSpaces && lengthWithSpaces > formSizeWithTypos) spaceErrors = lengthWithSpaces - formSizeWithTypos;
						const float typoCost = cand.getTypoCost();
						if (appendNewNode(out, endPosMap,
							nBeginWithMultiplier, nEndWithMultiplier,
							cand.form, typoCost))
						{
							out.back().spaceErrors = spaceErrors;
							if (typoTolerant)
							{
								out.back().typoFormId = cand.getTypoId();
							}
						}
					}
				}
			}
			candidates.clear();
		}
		else if (out.size() > 1 && !specialMatched)
		{
			const size_t lastPos = out.back().endPos;
			if (lastPos < unkFormEndPos && !isHangulCoda(str[nonSpaces[lastPos]]))
			{
				appendNewNode(out, endPosMap,
					lastPos * posMultiplier, unkFormEndPos * posMultiplier,
					str.substr(nonSpaces[lastPos], unkFormEndPosWithSpace - nonSpaces[lastPos]));
			}
		}

		const auto scanStart = max(endPosMap[unkFormEndPos * posMultiplier].first, (uint32_t)1), scanEnd = endPosMap[unkFormEndPos * posMultiplier].second;
		const bool duplicated = scanStart < scanEnd && any_of(out.begin() + scanStart, out.begin() + scanEnd, [&](const KGraphNode& g)
		{
			const size_t startPos = g.endPos - (g.uform.empty() ? g.form->sizeWithoutSpace() : g.uform.size()) * posMultiplier;
			return startPos == lastSpecialEndPos * posMultiplier && g.endPos == unkFormEndPos * posMultiplier;
		});
		if (unkFormEndPos > lastSpecialEndPos && !duplicated)
		{
			appendNewNode(out, endPosMap,
				lastSpecialEndPos * posMultiplier, unkFormEndPos * posMultiplier,
				str.substr(nonSpaces[lastSpecialEndPos], unkFormEndPosWithSpace - nonSpaces[lastSpecialEndPos]));
		}
	};

	bool zCodaFollowable = false, zSiotFollowable = false;
	const Form* const fallbackFormBegin = trie.value((size_t)POSTag::nng);
	const Form* const fallbackFormEnd = trie.value((size_t)POSTag::max);
	for (; n < str.size(); ++n)
	{
		char16_t c = str[n];
		char32_t c32 = c;
		if (isHighSurrogate(c32) && n + 1 < str.size())
		{
			c32 = mergeSurrogate(c32, str[n + 1]);
		}

		// Pretokenized 매칭
		if (pretokenizedFirst < pretokenizedLast && pretokenizedFirst->begin == n + startOffset)
		{
			if (lastChrType != POSTag::unknown)
			{
				// sequence of speical characters found
				if (lastChrType != POSTag::max && !isWebTag(lastChrType))
				{
					if (appendNewNode(out, endPosMap,
						specialStartPos * posMultiplier, nonSpaces.size() * posMultiplier,
						U16StringView{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }))
					{
						out.back().form = trie.value((size_t)lastChrType);
					}
				}
				lastSpecialEndPos = specialStartPos;
				specialStartPos = nonSpaces.size();
			}

			uint32_t length = pretokenizedFirst->end - pretokenizedFirst->begin;
			flushBranch(nonSpaces.size(), n);
			if (appendNewNode(out, endPosMap,
				nonSpaces.size() * posMultiplier, (nonSpaces.size() + length) * posMultiplier,
				pretokenizedFirst->form))
			{
				if (within(pretokenizedFirst->form, fallbackFormBegin, fallbackFormEnd))
				{
					out.back().uform = U16StringView{ &str[n], length };
				}
			}

			nonSpaces.resize(nonSpaces.size() + length);
			iota(nonSpaces.end() - length, nonSpaces.end(), n);
			n += length - 1;
			specialStartPos = lastSpecialEndPos = nonSpaces.size();
			pretokenizedFirst++;
			chrType = POSTag::max;
			curNode = trie.root();
			goto continueFor;
		}

		// 패턴 매칭
		{
			auto m = matchPattern(n ? str[n - 1] : u' ', str.data() + n, str.data() + str.size(), matchOptions);
			chrType = m.second;
			if (chrType != POSTag::unknown)
			{
				if (lastChrType != POSTag::unknown)
				{
					// sequence of speical characters found
					if (lastChrType != POSTag::max && !isWebTag(lastChrType))
					{
						if (appendNewNode(out, endPosMap,
							specialStartPos * posMultiplier, nonSpaces.size() * posMultiplier,
							U16StringView{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }))
						{
							out.back().form = trie.value((size_t)lastChrType);
						}
					}
					lastSpecialEndPos = specialStartPos;
					specialStartPos = nonSpaces.size();
				}

				size_t patStart = nonSpaces.size();
				for (size_t i = 0; i < m.first; ++i)
				{
					flushBranch(nonSpaces.size(), n + i, i > 0);
					nextNode = curNode->template nextOpt<arch>(trie, str[n + i]);
					while (!nextNode) // if curNode has no exact next node, goto fail
					{
						if (curNode->fail())
						{
							curNode = curNode->fail();
							nextNode = curNode->template nextOpt<arch>(trie, str[n + i]);
						}
						else
						{
							nonSpaces.emplace_back(n + i);
							goto continuePatternFor;
						}
					}
					nonSpaces.emplace_back(n + i);
					// from this, curNode has the exact next node
					curNode = nextNode;
					// if it has exit node, a pattern has found
					for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
					{
						const Form* cand = submatcher->val(trie);
						if (!cand) break;
						else if (!trie.hasSubmatch(cand))
						{
							if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, allowedDialect)) break;
						}
					}
				continuePatternFor:;
				}
				flushBranch(nonSpaces.size(), n + m.first, true);

				if (appendNewNode(out, endPosMap,
					patStart * posMultiplier, (patStart + m.first) * posMultiplier,
					U16StringView{ &str[n], m.first }))
				{
					out.back().form = trie.value((size_t)chrType);
				}

				n += m.first - 1;
				lastMatchedPattern = m.second;
				// SN태그 패턴 매칭의 경우 Web태그로 치환하여 Web와 동일하게 처리되도록 한다
				if (chrType == POSTag::sn)
				{
					chrType = POSTag::w_url;
					lastMatchedPattern = POSTag::w_url;
				}
				goto continueFor;
			}
		}

		chrType = identifySpecialChr(c32);
		scriptType = chr2ScriptType(c32);
		if (lastChrType == POSTag::sw &&
			(c32 == 0x200d || // zero width joiner
				(0x1f3fb <= c32 && c32 <= 0x1f3ff) || // skin color modifier
				scriptType == ScriptType::variation_selectors)) // variation selectors
		{
			chrType = lastChrType;
			scriptType = lastScriptType;
		}

		if (isDiscontinuous(lastChrType, chrType, lastScriptType, scriptType) || lastChrType == POSTag::sso || lastChrType == POSTag::ssc)
		{
			// sequence of speical characters found
			if (lastChrType != POSTag::max && lastChrType != POSTag::unknown && lastChrType != lastMatchedPattern)
			{
				const auto scanStart = max(endPosMap[specialStartPos * posMultiplier].first, (uint32_t)1), scanEnd = endPosMap[specialStartPos * posMultiplier].second;
				const bool duplicated = scanStart < scanEnd && any_of(out.begin() + scanStart, out.begin() + scanEnd, [&](const KGraphNode& g)
				{
					return specialStartPos * posMultiplier == g.endPos;
				});
				if (nonSpaces.size() > lastSpecialEndPos && specialStartPos > lastSpecialEndPos && !duplicated)
				{
					appendNewNode(out, endPosMap,
						lastSpecialEndPos * posMultiplier, specialStartPos * posMultiplier,
						str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[specialStartPos] - nonSpaces[lastSpecialEndPos]));
				}

				if (lastChrType != POSTag::ss) // ss 태그는 morpheme 내에 등록된 후보에서 직접 탐색하도록 한다
				{
					if (appendNewNode(out, endPosMap,
						specialStartPos * posMultiplier, nonSpaces.size() * posMultiplier,
						U16StringView{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }))
					{
						out.back().form = trie.value((size_t)lastChrType);
					}
				}
			}
			lastSpecialEndPos = (lastChrType == POSTag::sso || lastChrType == POSTag::ssc) ? nonSpaces.size() : specialStartPos;
			specialStartPos = nonSpaces.size();
		}
		lastMatchedPattern = POSTag::unknown;

		// 문장 종결 지점이 나타나거나 Graph가 너무 길어지면 공백 문자에서 중단
		if (chrType == POSTag::unknown && ((lastChrType == POSTag::sf && n >= 4) || n > 4096))
		{
			if (!isSpace(str[n - 3]) && !isSpace(str[n - 2]))
			{
				lastChrType = chrType;
				lastScriptType = scriptType;
				break;
			}
		}
		// 혹은 공백 문자가 아예 없는 경우 너무 길어지는 것을 방지하기 위해 강제로 중단
		else if (n >= 8192)
		{
			lastChrType = chrType;
			lastScriptType = scriptType;
			break;
		}

		// 공백문자를 무시하고 분할 진행
		if (chrType == POSTag::unknown)
		{
			flushBranch(nonSpaces.size(), n);
			lastSpecialEndPos = nonSpaces.size();
			goto continueFor;
		}

		if (isOldHangulToneMark(c))
		{
			flushBranch(nonSpaces.size(), n);
			goto continueFor;
		}

		curNodeForTypo = curNode;
		nextNode = curNode->template nextOpt<arch>(trie, c);
		while (!nextNode) // if curNode has no exact next node, goto fail
		{
			if (curNode->fail())
			{
				curNode = curNode->fail();
				nextNode = curNode->template nextOpt<arch>(trie, c);
			}
			else
			{
				if (chrType != POSTag::max)
				{
					flushBranch(specialStartPos, specialStartPos < nonSpaces.size() ? nonSpaces[specialStartPos] : n);
				}
				else
				{
					flushBranch();
				}

				// spaceTolerance == 0이고 공백 문자인 경우
				if (chrType == POSTag::unknown)
				{
					lastSpecialEndPos = nonSpaces.size();
				}
				// 그 외의 경우
				else
				{
					nonSpaces.emplace_back(n);
					if (c32 >= 0x10000) nonSpaces.emplace_back(++n);
					if (chrType != POSTag::max)
					{
						lastSpecialEndPos = nonSpaces.size();
					}
				}

				if (!!(matchOptions & Match::zCoda) && zCodaFollowable && isHangulCoda(c) && (n + 1 >= str.size() || !isHangulSyllable(str[n + 1])))
				{
					candidates.emplace_back(formBase + defaultTagSize + (c - 0x11A8) - 1, 0, (nonSpaces.size() - 1) * posMultiplier);
				}
				else if (!!(matchOptions & (Match::splitSaisiot | Match::mergeSaisiot)) && zSiotFollowable && c == 0x11BA && n + 1 < str.size() && isHangulSyllable(str[n + 1]))
				{
					candidates.emplace_back(formBase + defaultTagSize + (0x11BA - 0x11A8) - 1, 0, (nonSpaces.size() - 1) * posMultiplier);
				}
				zCodaFollowable = false;
				zSiotFollowable = false;

				// invalidate typo nodes
				if (continualTypoTolerant)
				{
					continualTypoRightNodes.clear();
				}

				if (lengtheningTypoTolerant)
				{
					lengtheningTypoNodes.clear();
				}

				goto continueFor;
			}
		}

		if (continualTypoTolerant)
		{
			size_t outputIdx = 0;
			for (auto& rn : continualTypoRightNodes)
			{
				rn.second = rn.second->template nextOpt<arch>(trie, c);
				if (!rn.second) continue;
				continualTypoRightNodes[outputIdx++] = rn;
			}
			continualTypoRightNodes.resize(outputIdx);
		}

		if (lengtheningTypoTolerant)
		{
			static uint8_t lengthenVowelTable[] = {
				0, // ㅏ
				1, // ㅐ
				0, // ㅑ
				1, // ㅒ
				4, // ㅓ
				5, // ㅔ
				4, // ㅕ
				5, // ㅖ
				8, // ㅗ
				0, // ㅘ
				1, // ㅙ
				1, // ㅚ
				8, // ㅛ
				13, // ㅜ
				4, // ㅝ
				5, // ㅞ
				20, // ㅟ
				13, // ㅠ
				18, // ㅡ
				20, // ㅢ
				20, // ㅣ
			};
			const size_t prevLengtheningSize = lengtheningTypoNodes.size();
			if (n > 0 && isHangulSyllable(str[n - 1]) &&
				(u'아' <= c && c < u'자') && lengthenVowelTable[extractVowel(str[n - 1])] == extractVowel(c))
			{
				lengtheningTypoNodes.emplace_back(1, curNodeForTypo);
				for (size_t i = 0; i < prevLengtheningSize; ++i)
				{
					auto& node = lengtheningTypoNodes[i];
					lengtheningTypoNodes.emplace_back(node.first + 1, node.second);
				}
			}

			thread_local UnorderedSet<pair<size_t, NodePtrTy>> uniq;
			uniq.clear();
			size_t outputIdx = 0;
			for (size_t i = 0; i < prevLengtheningSize; ++i)
			{
				auto& node = lengtheningTypoNodes[i];
				node.second = node.second->template nextOpt<arch>(trie, c);
				if (!node.second) continue;
				if (!uniq.emplace(node).second) continue;
				lengtheningTypoNodes[outputIdx++] = node;
			}
			for (size_t i = prevLengtheningSize; i < lengtheningTypoNodes.size(); ++i)
			{
				auto& node = lengtheningTypoNodes[i];
				if (!uniq.emplace(node).second) continue;
				lengtheningTypoNodes[outputIdx++] = node;
			}
			lengtheningTypoNodes.erase(lengtheningTypoNodes.begin() + outputIdx, lengtheningTypoNodes.end());
		}

		if (chrType != POSTag::max)
		{
			flushBranch(specialStartPos, specialStartPos < nonSpaces.size() ? nonSpaces[specialStartPos] : n);
		}
		else
		{
			flushBranch();
		}

		nonSpaces.emplace_back(n);

		if (!!(matchOptions & Match::zCoda) && zCodaFollowable && isHangulCoda(c) && (n + 1 >= str.size() || !isHangulSyllable(str[n + 1])))
		{
			candidates.emplace_back(formBase + defaultTagSize + (c - 0x11A8) - 1, 0, (nonSpaces.size() - 1) * posMultiplier);
		}
		else if (!!(matchOptions & (Match::splitSaisiot | Match::mergeSaisiot)) && zSiotFollowable && c == 0x11BA && n + 1 < str.size() && isHangulSyllable(str[n + 1]))
		{
			candidates.emplace_back(formBase + defaultTagSize + (0x11BA - 0x11A8) - 1, 0, (nonSpaces.size() - 1) * posMultiplier);
		}
		zCodaFollowable = false;
		zSiotFollowable = false;

		if (continualTypoTolerant && lastChrType == POSTag::max)
		{
			insertContinualTypoNode<arch>(candidates, continualTypoRightNodes, ContinualIeungDecomposer{},
				continualTypoCost, c, formBase, typoPtrs, trie, str, nonSpaces, curNodeForTypo, allowedDialect);
			insertContinualTypoNode<arch>(candidates, continualTypoRightNodes, ContinualHieutDecomposer{},
				continualTypoCost, c, formBase, typoPtrs, trie, str, nonSpaces, curNodeForTypo, allowedDialect);
			insertContinualTypoNode<arch>(candidates, continualTypoRightNodes, ContinualCodaDecomposer{},
				continualTypoCost, c, formBase, typoPtrs, trie, str, nonSpaces, curNodeForTypo, allowedDialect);
		}

		// from this, curNode has the exact next node
		curNode = nextNode;
		// if it has exit node, patterns have been found
		for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
		{
			const Form* cand = submatcher->val(trie);
			if (!cand) break;
			else if (!trie.hasSubmatch(cand))
			{
				zCodaFollowable = zCodaFollowable || getForm<typoTolerant>(cand, formBase).zCodaAppendable;
				zSiotFollowable = zSiotFollowable || getForm<typoTolerant>(cand, formBase).zSiotAppendable;
				if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, allowedDialect)) break;
			}
		}

		if (continualTypoTolerant)
		{
			for (auto& rn : continualTypoRightNodes)
			{
				const Form* cand = rn.second->val(trie);
				if (cand && !trie.hasSubmatch(cand))
				{
					if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, allowedDialect, rn.first, 0, continualTypoCost / 2)) break;
				}
			}
		}

		if (lengtheningTypoTolerant)
		{
			for (auto& node : lengtheningTypoNodes)
			{
				const Form* cand = node.second->val(trie);
				if (cand && !trie.hasSubmatch(cand))
				{
					insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, allowedDialect, 0, 0, lengtheningTypoCost * (3 + node.first), node.first);
				}
			}
		}

	continueFor:
		lastChrType = chrType;
		lastScriptType = scriptType;
	}

	// sequence of speical characters found
	if (lastChrType != POSTag::max && lastChrType != POSTag::unknown && !isWebTag(lastChrType))
	{
		const auto scanStart = max(endPosMap[specialStartPos * posMultiplier].first, (uint32_t)1), scanEnd = endPosMap[specialStartPos * posMultiplier].second;
		const bool duplicated = scanStart < scanEnd && any_of(out.begin() + scanStart, out.begin() + scanEnd, [&](const KGraphNode& g)
		{
			return specialStartPos * posMultiplier == g.endPos;
		});
		if (nonSpaces.size() > lastSpecialEndPos && specialStartPos > lastSpecialEndPos && !duplicated)
		{
			appendNewNode(out, endPosMap,
				lastSpecialEndPos * posMultiplier, specialStartPos * posMultiplier,
				str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[specialStartPos] - nonSpaces[lastSpecialEndPos]));
		}
		if (specialStartPos < nonSpaces.size() && appendNewNode(out, endPosMap,
			specialStartPos * posMultiplier, nonSpaces.size() * posMultiplier,
			U16StringView{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }))
		{
			out.back().form = trie.value((size_t)lastChrType);
		}
	}
	lastSpecialEndPos = specialStartPos;

	curNode = curNode->fail();
	while (curNode)
	{
		if (curNode->val(trie) && !trie.hasSubmatch(curNode->val(trie)))
		{
			const Form* cand = curNode->val(trie);
			if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, allowedDialect)) break;
		}
		curNode = curNode->fail();
	}
	flushBranch(nonSpaces.size(), n);

	appendNewNode(out, endPosMap,
		nonSpaces.size() * posMultiplier, (nonSpaces.size() + 1) * posMultiplier, nullptr);
	out.back().endPos = nonSpaces.size() * posMultiplier;

	nonSpaces.emplace_back(n);

	removeUnconnected(ret, out, endPosMap);
	for (size_t i = 1; i < ret.size() - 1; ++i)
	{
		auto& r = ret[i];
		r.startPos = nonSpaces[r.startPos / posMultiplier] + startOffset;
		r.endPos = nonSpaces[(r.endPos + posMultiplier - 1) / posMultiplier - 1] + 1 + startOffset;
	}
	ret.back().startPos = ret.back().endPos = str.size() + startOffset;
	while (n < str.size() && isSpace(str[n])) ++n;
	return n + startOffset;
}

template<ArchType arch, bool typoTolerant>
const Form* kiwi::findForm(
	const utils::FrozenTrie<kchar_t, const Form*>& trie,
	const Form* formData,
	const KString& str
)
{
	auto* node = trie.root();
	for (auto c : str)
	{
		node = node->template nextOpt<arch>(trie, c);
		if (!node) return nullptr;
	}
	if (trie.hasSubmatch(node->val(trie))) return nullptr;
	auto ret = node->val(trie);
	if (typoTolerant)
	{
		ret = &reinterpret_cast<const TypoForm*>(ret)->form(formData);
	}
	return ret;
}

template<ArchType arch, bool typoTolerant>
pair<const Form*, size_t> kiwi::findFormWithPrefix(
	const utils::FrozenTrie<kchar_t, const Form*>& trie,
	const Form* formData,
	const KString& prefix
)
{
	auto* node = trie.root();
	size_t matchedPrefixLen = 0;
	for (auto c : prefix)
	{
		auto nnode = node->template nextOpt<arch>(trie, c);
		if (!nnode) break;
		++matchedPrefixLen;
		node = nnode;
	}

	while (!trie.hasMatch(node->val(trie)))
	{
		node = trie.firstChild(node);
		if (!node) return make_pair<const Form*, size_t>(nullptr, 0);
	}

	auto ret = node->val(trie);
	if (typoTolerant)
	{
		ret = &reinterpret_cast<const TypoForm*>(ret)->form(formData);
	}
	return make_pair(ret, matchedPrefixLen);
}

namespace kiwi
{
	template<bool typoTolerant, bool continualTypoTolerant, bool lengtheningTypoTolerant>
	struct SplitByTrieGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnSplitByTrie value = &splitByTrie<static_cast<ArchType>(i), typoTolerant, continualTypoTolerant, lengtheningTypoTolerant>;
		};
	};
}

FnSplitByTrie kiwi::getSplitByTrieFn(ArchType arch, bool typoTolerant, bool continualTypoTolerant, bool lengtheningTypoTolerant)
{
	static std::array<tp::Table<FnSplitByTrie, AvailableArch>, 8> table{
		SplitByTrieGetter<false, false, false>{},
		SplitByTrieGetter<true, false, false>{},
		SplitByTrieGetter<false, true, false>{},
		SplitByTrieGetter<true, true, false>{},
		SplitByTrieGetter<false, false, true>{},
		SplitByTrieGetter<true, false, true>{},
		SplitByTrieGetter<false, true, true>{},
		SplitByTrieGetter<true, true, true>{},
	};

	size_t idx = 0;
	if (typoTolerant) idx += 1;
	if (continualTypoTolerant) idx += 2;
	if (lengtheningTypoTolerant) idx += 4;
	return table[idx][static_cast<std::ptrdiff_t>(arch)];
}

namespace kiwi
{
	template<bool typoTolerant>
	struct FindFormGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnFindForm value = &findForm<static_cast<ArchType>(i), typoTolerant>;
		};
	};
}

FnFindForm kiwi::getFindFormFn(ArchType arch, bool typoTolerant)
{
	static std::array<tp::Table<FnFindForm, AvailableArch>, 2> table{
		FindFormGetter<false>{},
		FindFormGetter<true>{},
	};

	return table[typoTolerant ? 1 : 0][static_cast<std::ptrdiff_t>(arch)];
}

namespace kiwi
{
	template<bool typoTolerant>
	struct FindFormWithPrefixGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnFindFormWithPrefix value = &findFormWithPrefix<static_cast<ArchType>(i), typoTolerant>;
		};
	};
}

FnFindFormWithPrefix kiwi::getFindFormWithPrefixFn(ArchType arch, bool typoTolerant)
{
	static std::array<tp::Table<FnFindFormWithPrefix, AvailableArch>, 2> table{
		FindFormWithPrefixGetter<false>{},
		FindFormWithPrefixGetter<true>{},
	};

	return table[typoTolerant ? 1 : 0][static_cast<std::ptrdiff_t>(arch)];
}
