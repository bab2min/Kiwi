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
	inline bool appendNewNode(Vector<KGraphNode>& nodes, Vector<pair<uint32_t, uint32_t>>& endPosMap, size_t startPos, Args&&... args)
	{
		static constexpr uint32_t npos = -1;

		if (endPosMap[startPos].first == npos)
		{
			return false;
		}

		size_t newId = nodes.size();
		nodes.emplace_back(forward<Args>(args)...);
		auto& nnode = nodes.back();
		nnode.startPos = startPos;

		nnode.prev = newId - endPosMap[startPos].first;
		if (nnode.endPos >= endPosMap.size()) return true;

		if (endPosMap[nnode.endPos].first == npos)
		{
			endPosMap[nnode.endPos].first = newId;
			endPosMap[nnode.endPos].second = newId;
		}
		else
		{
			nodes[endPosMap[nnode.endPos].second].sibling = newId - endPosMap[nnode.endPos].second;
			endPosMap[nnode.endPos].second = newId;
		}
		return true;
	}

	struct TypoCostInfo
	{
		float cost;
		uint32_t start;
		uint32_t typoId;

		TypoCostInfo(float _cost = 0, uint32_t _start = 0, uint32_t _typoId = 0)
			: cost{ _cost }, start{ _start }, typoId{ _typoId }
		{}
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

	template<bool typoTolerant>
	bool insertCandidates(
		Vector<const Form*>& candidates,
		Vector<TypoCostInfo>& candTypoCostStarts,
		const Form* foundCand,
		const Form* formBase,
		const size_t* typoPtrs,
		U16StringView str,
		const Vector<uint32_t>& nonSpaces
	)
	{
		if (typoTolerant)
		{
			auto tCand = reinterpret_cast<const TypoForm*>(foundCand);
			if (find(candidates.begin(), candidates.end(), &tCand->form(formBase)) != candidates.end()) return false;

			while (1)
			{
				auto typoFormSize = typoPtrs[tCand->typoId + 1] - typoPtrs[tCand->typoId];
				auto cand = &tCand->form(formBase);
				if (FeatureTestor::isMatched(&str[0], &str[nonSpaces[nonSpaces.size() - typoFormSize]], tCand->leftCond)
					&& FeatureTestor::isMatchedApprox(&str[0], &str[nonSpaces[nonSpaces.size() - typoFormSize]], cand->vowel, cand->polar))
				{
					candidates.emplace_back(cand);
					candTypoCostStarts.emplace_back(tCand->score(), nonSpaces.size() - typoFormSize, tCand->typoId);
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

	inline void removeUnconnected(Vector<KGraphNode>& ret, const Vector<KGraphNode>& graph)
	{
		Vector<uint8_t> connectedList(graph.size());
		Vector<uint16_t> newIndexDiff(graph.size());
		connectedList[graph.size() - 1] = true;
		connectedList[0] = true;
		// forward searching
		for (size_t i = 1; i < graph.size(); ++i)
		{
			bool connected = false;
			for (auto prev = graph[i].getPrev(); prev; prev = prev->getSibling())
			{
				if (connectedList[prev - graph.data()])
				{
					connected = true;
					break;
				}
			}
			connectedList[i] = connected ? 1 : 0;
		}
		// backward searching
		for (size_t i = graph.size() - 1; i-- > 1; )
		{
			bool connected = false;
			for (size_t j = i + 1; j < graph.size(); ++j)
			{
				for (auto prev = graph[j].getPrev(); prev; prev = prev->getSibling())
				{
					if (prev > &graph[i]) break;
					if (prev < &graph[i]) continue;
					if (connectedList[j])
					{
						connected = true;
						goto break_2;
					}
				}
			}
		break_2:
			connectedList[i] = (connectedList[i] && connected) ? 1 : 0;
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

}

template<ArchType arch, bool typoTolerant>
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
	float typoCostWeight
)
{
	thread_local Vector<pair<uint32_t, uint32_t>> endPosMap;
	endPosMap.clear();
	endPosMap.resize(str.size() + 1, make_pair<uint32_t, uint32_t>(-1, -1));
	endPosMap[0] = make_pair(0, 0);
	
	thread_local Vector<uint32_t> nonSpaces;
	nonSpaces.clear();
	nonSpaces.reserve(str.size());

	thread_local Vector<KGraphNode> out;
	out.clear();
	out.emplace_back();
	size_t n = 0;
	Vector<const Form*> candidates;
	Vector<TypoCostInfo> candTypoCostStarts;
	auto* curNode = trie.root();
	auto* nextNode = trie.root();
	
	
	size_t lastSpecialEndPos = 0, specialStartPos = 0;
	POSTag chrType, lastChrType = POSTag::unknown, lastMatchedPattern = POSTag::unknown;
	auto branchOut = [&](size_t unkFormEndPos = 0, size_t unkFormEndPosWithSpace = 0, bool specialMatched = false)
	{
		if (!candidates.empty())
		{
			bool alreadySpecialChrProcessed = false;
			for (auto& cand : candidates)
			{
				size_t nBegin = typoTolerant ? candTypoCostStarts[&cand - candidates.data()].start : (nonSpaces.size() - cand->form.size());
				bool longestMatched = any_of(out.begin() + 1, out.end(), [&](const KGraphNode& g)
				{
					return nBegin == g.endPos && lastSpecialEndPos == g.endPos - (g.uform.empty() ? g.form->form.size() : g.uform.size());
				});

				// insert unknown form 
				if (nBegin > lastSpecialEndPos && !longestMatched
					&& !isHangulCoda(cand->form[0]))
				{
					{
						size_t lastPos = out.back().endPos;

						if (lastPos < nBegin)
						{
							if (lastPos && isHangulCoda(str[nonSpaces[lastPos]])) lastPos--; // prevent coda to be matched alone.
							if (lastPos != lastSpecialEndPos)
							{
								appendNewNode(out, endPosMap, lastPos, str.substr(nonSpaces[lastPos], nonSpaces[nBegin] - nonSpaces[lastPos]), (uint16_t)nBegin);
							}
						}
					}

					size_t newNodeLength = nBegin - lastSpecialEndPos;
					if (maxUnkFormSize && newNodeLength <= maxUnkFormSize)
					{
						appendNewNode(out, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[nBegin] - nonSpaces[lastSpecialEndPos]), (uint16_t)nBegin);
					}
				}				

				// if special character
				if (cand->candidate[0] <= trie.value((size_t)POSTag::sn)->candidate[0])
				{
					// special character should be processed one by one chr.
					if (!alreadySpecialChrProcessed)
					{
						if (appendNewNode(out, endPosMap, nonSpaces.size() - 1, U16StringView{ cand->form.data() + cand->form.size() - 1, 1 }, (uint16_t)nonSpaces.size()))
						{
							out.back().form = trie.value((size_t)cand->candidate[0]->tag);
						}
						lastSpecialEndPos = nonSpaces.size();
						alreadySpecialChrProcessed = true;
					}
				}
				else
				{
					size_t lengthWithSpaces = nonSpaces.back() + 1 - nonSpaces[nBegin];
					if (lengthWithSpaces <= cand->form.size() + spaceTolerance)
					{
						float typoCost = typoTolerant ? candTypoCostStarts[&cand - candidates.data()].cost : 0.f;
						if (appendNewNode(out, endPosMap, nBegin, cand, (uint16_t)nonSpaces.size(), typoCost) && typoTolerant)
						{
							out.back().typoFormId = candTypoCostStarts[&cand - candidates.data()].typoId;
						}
					}
				}
			}
			candidates.clear();
			if (typoTolerant) candTypoCostStarts.clear();
		}
		else if (out.size() > 1 && !specialMatched)
		{
			size_t lastPos = out.back().endPos;
			if (lastPos < unkFormEndPos && !isHangulCoda(str[nonSpaces[lastPos]]))
			{
				appendNewNode(out, endPosMap, lastPos, str.substr(nonSpaces[lastPos], unkFormEndPosWithSpace - nonSpaces[lastPos]), (uint16_t)unkFormEndPos);
			}
		}

		bool duplicated = any_of(out.begin() + 1, out.end(), [&](const KGraphNode& g)
		{
			size_t startPos = g.endPos - (g.uform.empty() ? g.form->form.size() : g.uform.size());
			return startPos == lastSpecialEndPos && g.endPos == unkFormEndPos;
		});
		if (unkFormEndPos > lastSpecialEndPos && !duplicated)
		{
			appendNewNode(out, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], unkFormEndPosWithSpace - nonSpaces[lastSpecialEndPos]), (uint16_t)unkFormEndPos);
		}
	};

	bool zCodaFollowable = false;
	for (; n < str.size(); ++n)
	{
		char16_t c = str[n];
		char32_t c32 = c;
		if (isHighSurrogate(c32) && n + 1 < str.size())
		{
			c32 = mergeSurrogate(c32, str[n + 1]);
		}

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
						if (appendNewNode(out, endPosMap, specialStartPos, U16StringView{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }, (uint16_t)nonSpaces.size()))
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
					branchOut(nonSpaces.size(), n + i, i > 0);
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
									if (!insertCandidates<typoTolerant>(candidates, candTypoCostStarts, cand, formBase, typoPtrs, str, nonSpaces)) break;
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
							if (!insertCandidates<typoTolerant>(candidates, candTypoCostStarts, cand, formBase, typoPtrs, str, nonSpaces)) break;
						}
					}
				continuePatternFor:;
				}
				branchOut(nonSpaces.size(), n + m.first, true);

				if (appendNewNode(out, endPosMap, patStart, U16StringView{ &str[n], m.first }, (uint16_t)(patStart + m.first)))
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

		if (lastChrType != chrType || lastChrType == POSTag::sso || lastChrType == POSTag::ssc)
		{
			// sequence of speical characters found
			if (lastChrType != POSTag::max && lastChrType != POSTag::unknown && lastChrType != lastMatchedPattern)
			{
				bool duplicated = any_of(out.begin() + 1, out.end(), [&](const KGraphNode& g)
				{
					return nonSpaces.size() == g.endPos;
				});
				if (nonSpaces.size() > lastSpecialEndPos && specialStartPos > lastSpecialEndPos && !duplicated)
				{
					appendNewNode(out, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[specialStartPos] - nonSpaces[lastSpecialEndPos]), (uint16_t)specialStartPos);
				}

				if (lastChrType != POSTag::ss) // ss 태그는 morpheme 내에 등록된 후보에서 직접 탐색하도록 한다
				{
					if (appendNewNode(out, endPosMap, specialStartPos, U16StringView{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }, (uint16_t)nonSpaces.size()))
					{
						out.back().form = trie.value((size_t)lastChrType);
					}
				}
			}
			lastSpecialEndPos = (lastChrType == POSTag::sso || lastChrType == POSTag::ssc) ? nonSpaces.size() : specialStartPos;
			specialStartPos = nonSpaces.size();
		}
		lastMatchedPattern = POSTag::unknown;

		// 문장 종결 지점이 나타나면 중단
		if (chrType == POSTag::unknown && lastChrType == POSTag::sf)
		{
			lastChrType = chrType;
			break;
		}

		// spaceTolerance > 0이면 공백문자를 무시하고 분할 진행
		if (spaceTolerance > 0 && chrType == POSTag::unknown)
		{
			branchOut(nonSpaces.size(), n);
			lastSpecialEndPos = nonSpaces.size();
			goto continueFor;
		}

		if (isOldHangulToneMark(c))
		{
			branchOut(nonSpaces.size(), n);
			goto continueFor;
		}

		nextNode = curNode->template nextOpt<arch>(trie, c);
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
						zCodaFollowable = zCodaFollowable || getZCodaAppendable<typoTolerant>(cand, formBase);
						if (!insertCandidates<typoTolerant>(candidates, candTypoCostStarts, cand, formBase, typoPtrs, str, nonSpaces)) break;
					}
				}
				nextNode = curNode->template nextOpt<arch>(trie, c);
			}
			else
			{
				if (chrType != POSTag::max)
				{
					branchOut(specialStartPos, specialStartPos < nonSpaces.size() ? nonSpaces[specialStartPos] : n);
				}
				else
				{
					branchOut();
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
					candidates.emplace_back(formBase + defaultTagSize + (c - 0x11A8) - 1);
					if (typoTolerant)
					{
						candTypoCostStarts.emplace_back(0, nonSpaces.size() - 1);
					}
				}
				zCodaFollowable = false;

				goto continueFor; 
			}
		}

		if (chrType != POSTag::max)
		{
			branchOut(specialStartPos, specialStartPos < nonSpaces.size() ? nonSpaces[specialStartPos] : n);
		}
		else
		{
			branchOut();
		}
		
		nonSpaces.emplace_back(n);

		if (!!(matchOptions & Match::zCoda) && zCodaFollowable && isHangulCoda(c) && (n + 1 >= str.size() || !isHangulSyllable(str[n + 1])))
		{
			candidates.emplace_back(formBase + defaultTagSize + (c - 0x11A8) - 1);
			if (typoTolerant)
			{
				candTypoCostStarts.emplace_back(0, nonSpaces.size() - 1);
			}
		}
		zCodaFollowable = false;

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
				if (!insertCandidates<typoTolerant>(candidates, candTypoCostStarts, cand, formBase, typoPtrs, str, nonSpaces)) break;
			}
		}
	continueFor:
		lastChrType = chrType;
	}

	// sequence of speical characters found
	if (lastChrType != POSTag::max && lastChrType != POSTag::unknown && !isWebTag(lastChrType))
	{
		bool duplicated = any_of(out.begin() + 1, out.end(), [&](const KGraphNode& g)
		{
			return nonSpaces.size() == g.endPos;
		});
		if (nonSpaces.size() > lastSpecialEndPos && specialStartPos > lastSpecialEndPos  && !duplicated)
		{
			appendNewNode(out, endPosMap, lastSpecialEndPos, str.substr(nonSpaces[lastSpecialEndPos], nonSpaces[specialStartPos] - nonSpaces[lastSpecialEndPos]), (uint16_t)specialStartPos);
		}
		if (appendNewNode(out, endPosMap, specialStartPos, U16StringView{ &str[nonSpaces[specialStartPos]], n - nonSpaces[specialStartPos] }, (uint16_t)nonSpaces.size()))
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
			if (!insertCandidates<typoTolerant>(candidates, candTypoCostStarts, cand, formBase, typoPtrs, str, nonSpaces)) break;
		}
		curNode = curNode->fail();
	}
	branchOut(nonSpaces.size(), n);

	appendNewNode(out, endPosMap, nonSpaces.size(), nullptr, nonSpaces.size() + 1);
	out.back().endPos = nonSpaces.size();

	nonSpaces.emplace_back(n);

	removeUnconnected(ret, out);
	for (size_t i = 1; i < ret.size() - 1; ++i)
	{
		auto& r = ret[i];
		r.startPos = nonSpaces[r.startPos] + startOffset;
		r.endPos = nonSpaces[r.endPos - 1] + 1 + startOffset;
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
	template<bool typoTolerant>
	struct SplitByTrieGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnSplitByTrie value = &splitByTrie<static_cast<ArchType>(i), typoTolerant>;
		};
	};
}

FnSplitByTrie kiwi::getSplitByTrieFn(ArchType arch, bool typoTolerant)
{
	static tp::Table<FnSplitByTrie, AvailableArch> table{ SplitByTrieGetter<false>{} };
	static tp::Table<FnSplitByTrie, AvailableArch> tableTT{ SplitByTrieGetter<true>{} };
	
	if (typoTolerant)
	{
		return tableTT[static_cast<std::ptrdiff_t>(arch)];
	}
	else
	{
		return table[static_cast<std::ptrdiff_t>(arch)];
	}
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
