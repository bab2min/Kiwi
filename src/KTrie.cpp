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

		FormCandidate(const Form* _form = nullptr, 
			float _cost = 0, 
			uint32_t _start = 0, 
			uint32_t _typoId = 0, 
			uint32_t _end = 0, 
			uint32_t = 0)
			: form{ _form }, 
			cost{ _cost }, 
			start{ _start }, 
			typoId{ _typoId }, 
			end{ _end }
		{}

		size_t getStartPos(size_t ) const
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
			return typoPtrs[typoId + 1] - typoPtrs[typoId];
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

		FormCandidate(const Form* _form = nullptr, float = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0)
			: form{ _form }
		{}

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
			uint32_t _lengthenedSize = 0)
			: FormCandidate<typoTolerant, continualTypoTolerant, false>{ _form, _cost, _start, _typoId, _end, _lengthenedSize },
			lengthenedSize{ _lengthenedSize }
		{}

		size_t getFormSizeWithTypos(const size_t* typoPtrs) const
		{
			return BaseType::getFormSizeWithTypos(typoPtrs) + lengthenedSize;
		}
	};

	template<bool typoTolerant>
	bool getZCodaAppendable(
		const Form* foundCand,
		const Form* formBase
	)
	{
		if (typoTolerant)
		{
			auto tCand = reinterpret_cast<const TypoForm*>(foundCand);
			return tCand->form(formBase).zCodaAppendable;
		}
		else
		{
			return foundCand->zCodaAppendable;
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
					&& FeatureTestor::isMatchedApprox(&str[0], &str[nonSpaces[nonSpaces.size() - typoFormSize]], cand->vowel, cand->polar))
				{
					candidates.emplace_back(cand, 
						tCand->score() + cost, 
						startPosition ? startPosition : ((nonSpaces.size() - typoFormSize) * posMultiplier), 
						tCand->typoId, 
						endPosition, 
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
				if (FeatureTestor::isMatchedApprox(&str[0], &str[nonSpaces[nonSpaces.size() - foundCand->form.size()]], foundCand->vowel, foundCand->polar))
				{
					candidates.emplace_back(foundCand);
				}
				if (foundCand[0].formHash != foundCand[1].formHash) break;
				++foundCand;
			}
		}
		return true;
	}

	template<bool typoTolerant>
	size_t getFormLength(
		const Form* form,
		const Form* formBase
	)
	{
		if (typoTolerant)
		{
			auto tCand = reinterpret_cast<const TypoForm*>(form);
			return tCand->form(formBase).form.size();
		}
		else
		{
			return form->form.size();
		}
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
		const utils::FrozenTrie<kchar_t, const Form*>::Node* curNode
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
				if (getFormLength<typoTolerant>(cand, formBase) <= 1) break;
				inserted = true;
				if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, 0, boundary, continualTypoCost / 2)) break;
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
	size_t maxUnkFormSize, 
	size_t spaceTolerance,
	float continualTypoCost,
	float lengtheningTypoCost,
	const PretokenizedSpanGroup::Span*& pretokenizedFirst,
	const PretokenizedSpanGroup::Span* pretokenizedLast
)
{
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
					return nBeginWithMultiplier == g.endPos && lastSpecialEndPos == g.endPos - (g.uform.empty() ? g.form->sizeWithoutSpace() : g.uform.size()) * posMultiplier;
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
					if (maxUnkFormSize && newNodeLength <= maxUnkFormSize)
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
			size_t lastPos = out.back().endPos;
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
			size_t startPos = g.endPos - (g.uform.empty() ? g.form->sizeWithoutSpace() : g.uform.size()) * posMultiplier;
			return startPos == lastSpecialEndPos * posMultiplier && g.endPos == unkFormEndPos * posMultiplier;
		});
		if (unkFormEndPos > lastSpecialEndPos && !duplicated)
		{
			appendNewNode(out, endPosMap, 
				lastSpecialEndPos * posMultiplier, unkFormEndPos * posMultiplier, 
				str.substr(nonSpaces[lastSpecialEndPos], unkFormEndPosWithSpace - nonSpaces[lastSpecialEndPos]));
		}
	};

	bool zCodaFollowable = false;
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
							for (auto submatcher = curNode; submatcher; submatcher = submatcher->fail())
							{
								const Form* cand = submatcher->val(trie);
								if (!cand) break;
								else if (!trie.hasSubmatch(cand))
								{
									if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces)) break;
								}
							}
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
							if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces)) break;
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
					candidates.emplace_back(formBase + defaultTagSize + (c - 0x11A8) - 1, 0, nonSpaces.size() - 1);
				}
				zCodaFollowable = false;

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
			candidates.emplace_back(formBase + defaultTagSize + (c - 0x11A8) - 1, 0, nonSpaces.size() - 1);
		}
		zCodaFollowable = false;

		if (continualTypoTolerant && lastChrType == POSTag::max)
		{
			insertContinualTypoNode<arch>(candidates, continualTypoRightNodes, ContinualIeungDecomposer{}, 
				continualTypoCost, c, formBase, typoPtrs, trie, str, nonSpaces, curNodeForTypo);
			insertContinualTypoNode<arch>(candidates, continualTypoRightNodes, ContinualHieutDecomposer{},
				continualTypoCost, c, formBase, typoPtrs, trie, str, nonSpaces, curNodeForTypo);
			insertContinualTypoNode<arch>(candidates, continualTypoRightNodes, ContinualCodaDecomposer{}, 
				continualTypoCost, c, formBase, typoPtrs, trie, str, nonSpaces, curNodeForTypo);
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
				zCodaFollowable = zCodaFollowable || getZCodaAppendable<typoTolerant>(cand, formBase);
				if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces)) break;
			}
		}

		if (continualTypoTolerant)
		{
			for (auto& rn : continualTypoRightNodes)
			{
				const Form* cand = rn.second->val(trie);
				if (cand && !trie.hasSubmatch(cand))
				{
					if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, rn.first, 0, continualTypoCost / 2)) break;
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
					insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces, 0, 0, lengtheningTypoCost * (3 + node.first), node.first);
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
		if (nonSpaces.size() > lastSpecialEndPos && specialStartPos > lastSpecialEndPos  && !duplicated)
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
			if (!insertCandidates(candidates, cand, formBase, typoPtrs, str, nonSpaces)) break;
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

template<ArchType arch>
const Form* kiwi::findForm(
	const utils::FrozenTrie<kchar_t, const Form*>& trie,
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
	return node->val(trie);
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
	struct FindFormGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnFindForm value = &findForm<static_cast<ArchType>(i)>;
		};
	};
}

FnFindForm kiwi::getFindFormFn(ArchType arch)
{
	static tp::Table<FnFindForm, AvailableArch> table{ FindFormGetter{} };

	return table[static_cast<std::ptrdiff_t>(arch)];
}
