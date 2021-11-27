#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>
#include <kiwi/TemplateUtils.hpp>
#include <kiwi/Form.h>
#include "KTrie.h"
#include "FeatureTestor.h"
#include "logPoisson.h"
#include "FrozenTrie.hpp"
#include "Knlm.hpp"
#include "serializer.hpp"

using namespace std;

namespace kiwi
{
	class PathEvaluator
	{
	public:
		using Path = Vector<std::tuple<const Morpheme*, KString, uint32_t>>;

		template<ArchType arch, class LmType>
		static Vector<std::pair<Path, float>> findBestPath(const Kiwi* kw, const Vector<KGraphNode>& graph, size_t topN);

		template<ArchType arch, class LmType, class CandTy, class CacheTy>
		static float evalPath(const Kiwi* kw, const KGraphNode* startNode, const KGraphNode* node,
			CacheTy& cache, Vector<KString>& ownFormList,
			size_t i, size_t ownFormId, CandTy&& cands, bool unknownForm
		);
	};

	using FnFindBestPath = decltype(&PathEvaluator::findBestPath<ArchType::default_, uint8_t>);

	template<class IntTy, ptrdiff_t ...indices>
	inline FnFindBestPath getFindBestPathFn(ArchType arch, tp::seq<indices...>)
	{
		static FnFindBestPath table[] = {
			&PathEvaluator::findBestPath<static_cast<ArchType>(indices + 1), IntTy>...
		};
		return table[static_cast<int>(arch) - 1];
	}

	Kiwi::Kiwi(ArchType arch, size_t lmKeySize)
	{
		selectedArch = getSelectedArch(arch);
		dfSplitByTrie = (void*)getSplitByTrieFn(selectedArch);
		
		auto indexHolder = tp::GenSeq<static_cast<int>(ArchType::last)>{};
		switch (lmKeySize)
		{
		case 1:
			dfFindBestPath = (void*)getFindBestPathFn<uint8_t>(selectedArch, indexHolder);
			break;
		case 2:
			dfFindBestPath = (void*)getFindBestPathFn<uint16_t>(selectedArch, indexHolder);
			break;
		case 4:
			dfFindBestPath = (void*)getFindBestPathFn<uint32_t>(selectedArch, indexHolder);
			break;
		case 8:
			dfFindBestPath = (void*)getFindBestPathFn<uint64_t>(selectedArch, indexHolder);
			break;
		default:
			throw Exception{ "Wrong `lmKeySize`" };
		}
	}

	Kiwi::~Kiwi() = default;

	Kiwi::Kiwi(Kiwi&&) = default;

	Kiwi& Kiwi::operator=(Kiwi&&) = default;

	vector<TokenResult> Kiwi::analyze(const u16string& str, size_t topN, Match matchOptions) const
	{
		auto chunk = str.begin();
		Vector<u16string::const_iterator> sents;
		sents.emplace_back(chunk);
		while (chunk != str.end())
		{
			POSTag tag = identifySpecialChr(*chunk);
			while (chunk != str.end() && !(tag == POSTag::sf || tag == POSTag::ss || tag == POSTag::se || tag == POSTag::sw || *chunk == u':'))
			{
				++chunk;
				if (chunk - sents.back() >= 512)
				{
					sents.emplace_back(chunk);
					break;
				}
				if (chunk == str.end()) break;
				tag = identifySpecialChr(*chunk);
			}
			if (chunk == str.end()) break;
			++chunk;
			if (tag == POSTag::se)
			{
				sents.emplace_back(chunk);
				continue;
			}
			if (chunk != str.end() && (identifySpecialChr(*chunk) == POSTag::unknown || identifySpecialChr(*chunk) == POSTag::sf))
			{
				while (chunk != str.end() && (identifySpecialChr(*chunk) == POSTag::unknown || identifySpecialChr(*chunk) == POSTag::sf))
				{
					++chunk;
				}
				sents.emplace_back(chunk);
			}
		}
		if (sents.back() != str.end()) sents.emplace_back(str.end());
		if (sents.size() <= 1) return vector<TokenResult>(1);
		vector<TokenResult> ret = analyzeSent(sents[0], sents[1], topN, matchOptions);
		if (ret.empty())
		{
			return vector<TokenResult>(1);
		}
		while (ret.size() < topN) ret.emplace_back(ret.back());
		for (size_t i = 2; i < sents.size(); ++i)
		{
			auto res = analyzeSent(sents[i - 1], sents[i], topN, matchOptions);
			if (res.empty()) continue;
			for (size_t n = 0; n < topN; ++n)
			{
				auto& r = res[min(n, res.size() - 1)];
				transform(r.first.begin(), r.first.end(), back_inserter(ret[n].first), [&sents, i](TokenInfo& p)
				{
					p.position += distance(sents[0], sents[i - 1]);
					return p;
				});
				ret[n].second += r.second;
			}
		}
		return ret;
	}

	template<class _map, class _key, class _value, class _comp>
	void emplaceMaxCnt(_map& dest, _key&& key, _value&& value, size_t maxCnt, _comp comparator)
	{
		auto itp = dest.find(key);
		if (itp == dest.end())
		{
			typename _map::mapped_type emp;
			emp.reserve(maxCnt);
			itp = dest.emplace(key, move(emp)).first;
		}
		if (itp->second.size() < maxCnt)
		{
			itp->second.emplace_back(value);
			push_heap(itp->second.begin(), itp->second.end(), comparator);
		}
		else
		{
			pop_heap(itp->second.begin(), itp->second.end(), comparator);
			if (comparator(value, itp->second.back())) itp->second.back() = value;
			push_heap(itp->second.begin(), itp->second.end(), comparator);
		}
	}

	using Wid = uint32_t;
	struct MInfo
	{
		Wid wid;
		uint8_t combineSocket;
		CondVowel condVowel;
		CondPolarity condPolar;
		uint8_t ownFormId;
		uint32_t lastPos;
		MInfo(Wid _wid = 0, uint8_t _combineSocket = 0,
			CondVowel _condVowel = CondVowel::none,
			CondPolarity _condPolar = CondPolarity::none,
			uint8_t _ownFormId = 0, uint32_t _lastPos = 0)
			: wid(_wid), combineSocket(_combineSocket),
			condVowel(_condVowel), condPolar(_condPolar), ownFormId(_ownFormId), lastPos(_lastPos)
		{}
	};

	struct WordLL;
	using MInfos = Vector<MInfo>;
	using WordLLs = Vector<WordLL>;

	struct WordLL
	{
		MInfos morphs;
		float accScore = 0;
		ptrdiff_t node = 0;

		WordLL() = default;

		WordLL(const MInfos& _morphs, float _accScore, ptrdiff_t _node)
			: morphs{ _morphs }, accScore{ _accScore }, node{ _node }
		{
		}
	};

	struct WordLLP
	{
		const MInfos* morphs = nullptr;
		float accScore = 0;
		ptrdiff_t node = 0;

		WordLLP() = default;

		WordLLP(const MInfos* _morphs, float _accScore, ptrdiff_t _node)
			: morphs{ _morphs }, accScore{ _accScore }, node{ _node }
		{
		}
	};

	template<class _Iter, class _Key, class _Filter>
	auto findNthLargest(_Iter first, _Iter last, size_t nth, _Key&& fn, _Filter&& filter) -> decltype(fn(*first))
	{
		using KeyType = decltype(fn(*first));

		Vector<KeyType> v;
		for (; first != last; ++first)
		{
			if (filter(*first)) v.emplace_back(fn(*first));
		}
		if (v.empty()) return {};
		std::partial_sort(v.begin(), v.begin() + std::min(nth + 1, v.size()), v.end(), std::greater<KeyType>{});
		//std::sort(v.rbegin(), v.rend());
		return v[std::min(nth, v.size() - 1)];
	}

	template<ArchType arch, class LmType, class _Type>
	void evalTrigram(const lm::KnLangModel<LmType>* knlm, const Morpheme* morphBase, const Vector<KString>& ownForms, const Vector<WordLLs>& cache,
		array<Wid, 4> seq, size_t chSize, const Morpheme* curMorph, const KGraphNode* node, const KGraphNode* startNode, _Type& maxWidLL)
	{
		size_t vocabSize = knlm->getHeader().vocab_size;
		for (size_t i = 0; i < KGraphNode::max_prev; ++i)
		{
			auto* prev = node->getPrev(i);
			if (!prev) break;
			for (auto& p : cache[prev - startNode])
			{
				const auto* wids = &p.morphs;
				float candScore = p.accScore;
				if (wids->back().combineSocket)
				{
					// always merge <v> <chunk> with the same socket
					if (wids->back().combineSocket != curMorph->combineSocket || curMorph->chunks.empty())
					{
						continue;
					}
					seq[0] = morphBase[wids->back().wid].getCombined() - morphBase;
				}

				/*auto leftForm = wids->back().ownFormId ? &ownForms[wids->back().ownFormId - 1] : morphBase[wids->back().wid].kform;

				if (!FeatureTestor::isMatched(leftForm, curMorph->vowel, curMorph->polar))
				{
					continue;
				}*/

				auto cNode = p.node;
				Wid lSeq = 0;
				if (curMorph->combineSocket && curMorph->chunks.empty())
				{
					lSeq = wids->back().wid;
				}
				else
				{
					lSeq = seq[chSize - 1];
					for (size_t i = 0; i < chSize; ++i)
					{
						auto cn = seq[i];
						if (cn >= vocabSize)
						{
							if (morphBase[cn].tag == POSTag::v)
							{
								// prohibit <v> without <chunk>
								goto continueFor;
							}
							else
							{
								cn = (size_t)morphBase[cn].tag + 1;
							}
						}
						float ll = knlm->template progressOpt<arch>(cNode, cn);
						candScore += ll;
					}
				}
				emplaceMaxCnt(maxWidLL, lSeq, WordLLP{ wids, candScore, cNode }, 3, [](const WordLLP& a, const WordLLP& b) { return a.accScore > b.accScore; });
			continueFor:;
			}
		}
	}

	template<ArchType arch, class LmType, class CandTy, class CacheTy>
	float PathEvaluator::evalPath(const Kiwi* kw, const KGraphNode* startNode, const KGraphNode* node,
		CacheTy& cache, Vector<KString>& ownFormList,
		size_t i, size_t ownFormId, CandTy&& cands, bool unknownForm
	)
	{
		auto lm = static_cast<const lm::KnLangModel<LmType>*>(kw->langMdl.get());
		size_t langVocabSize = lm->getHeader().vocab_size;

		float tMax = -INFINITY;
		for (auto& curMorph : cands)
		{
			array<Wid, 4> seq = { 0, };
			uint8_t combSocket = 0;
			CondVowel condV = CondVowel::none;
			CondPolarity condP = CondPolarity::none;
			size_t chSize = 1;
			bool isUserWord = false;
			bool leftBoundary = !node->getPrev(0)->lastPos ||
				node->getPrev(0)->lastPos < node->lastPos - (node->form ? node->form->form.size() : node->uform.size());
			// if the morpheme is chunk set
			if (!curMorph->chunks.empty())
			{
				chSize = curMorph->chunks.size();
				for (size_t i = 0; i < chSize; ++i)
				{
					seq[i] = curMorph->chunks[i] - kw->morphemes.data();
				}
			}
			else
			{
				if ((curMorph->getCombined() ? curMorph->getCombined() : curMorph) - kw->morphemes.data() >= langVocabSize)
				{
					isUserWord = true;
					seq[0] = kw->getDefaultMorpheme(curMorph->tag) - kw->morphemes.data();
				}
				else
				{
					seq[0] = curMorph - kw->morphemes.data();
				}
				combSocket = curMorph->combineSocket;
			}
			condV = curMorph->vowel;
			condP = curMorph->polar;

			UnorderedMap<Wid, Vector<WordLLP>> maxWidLL;
			evalTrigram<arch>(lm, kw->morphemes.data(), ownFormList, cache, seq, chSize, curMorph, node, startNode, maxWidLL);

			float estimatedLL = 0;
			if (isUserWord)
			{
				estimatedLL = curMorph->userScore;
			}
			// if a form of the node is unknown, calculate log poisson distribution for word-tag
			else if (unknownForm)
			{
				size_t unknownLen = node->uform.empty() ? node->form->form.size() : node->uform.size();
				if (curMorph->tag == POSTag::nng) estimatedLL = LogPoisson::getLL(4.622955f, unknownLen);
				else if (curMorph->tag == POSTag::nnp) estimatedLL = LogPoisson::getLL(5.177622f, unknownLen);
				else if (curMorph->tag == POSTag::mag) estimatedLL = LogPoisson::getLL(4.557326f, unknownLen);
				estimatedLL -= 5 + unknownLen * 3;
			}

			float discountForCombining = 0;
			if (curMorph->combineSocket) discountForCombining -= 15.f;
			if (isUserWord && !leftBoundary)
			{
				estimatedLL -= 10.f;
			}

			for (auto& p : maxWidLL)
			{
				for (auto& q : p.second)
				{
					q.accScore += estimatedLL;
					tMax = max(tMax, q.accScore + discountForCombining);
				}
			}

			auto& nCache = cache[i];
			for (auto& p : maxWidLL)
			{
				for (auto& q : p.second)
				{
					if (q.accScore <= tMax - kw->cutOffThreshold) continue;
					nCache.emplace_back(WordLL{ MInfos{}, q.accScore, q.node });
					auto& wids = nCache.back().morphs;
					wids.reserve(q.morphs->size() + chSize);
					wids = *q.morphs;
					if (!curMorph->chunks.empty())
					{
						size_t lastPos = node->lastPos;
						if (curMorph->combineSocket)
						{
							wids.back() = MInfo{ (Wid)(kw->morphemes[wids.back().wid].getCombined() - kw->morphemes.data()),
								0, CondVowel::none, CondPolarity::none, 0, wids.back().lastPos };
							lastPos += kw->morphemes[seq[0]].kform->size() - 1;
							for (size_t ch = 1; ch < chSize; ++ch)
							{
								wids.emplace_back(seq[ch], 0, condV, condP, 0, lastPos);
								lastPos += kw->morphemes[seq[ch]].kform->size() - 1;
							}
						}
						else
						{
							for (size_t ch = 0; ch < chSize; ++ch)
							{
								wids.emplace_back(seq[ch], 0, condV, condP, 0, lastPos);
								lastPos += kw->morphemes[seq[ch]].kform->size() - 1;
							}
						}
					}
					else
					{
						wids.emplace_back(isUserWord ? curMorph - kw->morphemes.data() : seq[0],
							combSocket, CondVowel::none, CondPolarity::none, ownFormId, node->lastPos);
					}
				}
			}
		}
		return tMax;
	}

	template<ArchType arch, class LmType>
	Vector<pair<PathEvaluator::Path, float>> PathEvaluator::findBestPath(const Kiwi* kw, const Vector<KGraphNode>& graph, size_t topN)
	{
		Vector<WordLLs> cache(graph.size());
		Vector<KString> ownFormList;
		Vector<const Morpheme*> unknownNodeCands, unknownNodeLCands;

		auto lm = static_cast<const lm::KnLangModel<LmType>*>(kw->langMdl.get());

		size_t langVocabSize = lm->getHeader().vocab_size;

		const KGraphNode* startNode = &graph.front();
		const KGraphNode* endNode = &graph.back();

		unknownNodeCands.emplace_back(kw->getDefaultMorpheme(POSTag::nng));
		unknownNodeCands.emplace_back(kw->getDefaultMorpheme(POSTag::nnp));
		unknownNodeLCands.emplace_back(kw->getDefaultMorpheme(POSTag::nnp));

		// start node
		ptrdiff_t bosNode = 0;
		lm->template progressOpt<arch>(bosNode, 0);
		cache.front().emplace_back(WordLL{ MInfos{ MInfo(0u) }, 0.f, bosNode });

		// middle nodes
		for (size_t i = 1; i < graph.size() - 1; ++i)
		{
			auto* node = &graph[i];
			size_t ownFormId = 0;
			if (!node->uform.empty())
			{
				ownFormList.emplace_back(node->uform);
				ownFormId = ownFormList.size();
			}
			float tMax = -INFINITY;

			if (node->form)
			{
				tMax = evalPath<arch, LmType>(kw, startNode, node, cache, ownFormList, i, ownFormId, node->form->candidate, false);
				if (all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const Morpheme* m)
				{
					return m->combineSocket || !m->chunks.empty();
				}))
				{
					ownFormList.emplace_back(node->form->form);
					ownFormId = ownFormList.size();
					tMax = min(tMax, evalPath<arch, LmType>(kw, startNode, node, cache, ownFormList, i, ownFormId, unknownNodeLCands, true));
				};
			}
			else
			{
				tMax = evalPath<arch, LmType>(kw, startNode, node, cache, ownFormList, i, ownFormId, unknownNodeCands, true);
			}

			// heuristically remove cands with lower ll to speed up
			if (cache[i].size() > topN)
			{
				WordLLs reduced;
				float combinedCutOffScore = findNthLargest(cache[i].begin(), cache[i].end(), topN,
					[](const WordLL& c)
				{
					return c.accScore;
				},
					[](const WordLL& c)
				{
					if (c.morphs.empty()) return false;
					return !!c.morphs.back().combineSocket;
				}
				);

				float otherCutOffScore = findNthLargest(cache[i].begin(), cache[i].end(), topN,
					[](const WordLL& c)
				{
					return c.accScore;
				},
					[](const WordLL& c)
				{
					if (c.morphs.empty()) return true;
					return !c.morphs.back().combineSocket;
				}
				);

				combinedCutOffScore = min(tMax - kw->cutOffThreshold, combinedCutOffScore);
				otherCutOffScore = min(tMax - kw->cutOffThreshold, otherCutOffScore);
				for (auto& c : cache[i])
				{
					float cutoff = (c.morphs.empty() || !c.morphs.back().combineSocket) ? otherCutOffScore : combinedCutOffScore;
					if (reduced.size() < topN || c.accScore >= cutoff) reduced.emplace_back(move(c));
				}
				cache[i] = move(reduced);
			}

#ifdef DEBUG_PRINT
			cout << "== " << i << " (tMax: " << tMax << ") ==" << endl;
			for (auto& tt : cache[i])
			{
				cout << tt.accScore << '\t';
				for (auto& m : tt.morphs)
				{
					morphBase[m.wid].print(cout) << '\t';
				}
				cout << endl;
			}
			cout << "========" << endl;
#endif
		}

		// end node
		for (size_t i = 0; i < KGraphNode::max_prev; ++i)
		{
			auto* prev = endNode->getPrev(i);
			if (!prev) break;
			for (auto&& p : cache[prev - startNode])
			{
				if (p.morphs.back().combineSocket) continue;
				if (!FeatureTestor::isMatched(nullptr, p.morphs.back().condVowel)) continue;

				float c = p.accScore + lm->template progressOpt<arch>(p.node, 1);
				cache.back().emplace_back(WordLL{ p.morphs, c, 0 });
			}
		}

		auto& cand = cache.back();
		sort(cand.begin(), cand.end(), [](const WordLL& a, const WordLL& b) { return a.accScore > b.accScore; });

#ifdef DEBUG_PRINT
		cout << "== LAST ==" << endl;
		for (auto& tt : cache.back())
		{
			cout << tt.accScore << '\t';
			for (auto& m : tt.morphs)
			{
				morphBase[m.wid].print(cout) << '\t';
			}
			cout << endl;
		}
		cout << "========" << endl;

#endif

		Vector<pair<Path, float>> ret;
		for (size_t i = 0; i < min(topN, cand.size()); ++i)
		{
			Path mv(cand[i].morphs.size() - 1);
			transform(cand[i].morphs.begin() + 1, cand[i].morphs.end(), mv.begin(), [&](const MInfo& m)
			{
				if (m.ownFormId) return make_tuple(&kw->morphemes[m.wid], ownFormList[m.ownFormId - 1], m.lastPos);
				else return make_tuple(&kw->morphemes[m.wid], KString{}, m.lastPos);
			});
			ret.emplace_back(mv, cand[i].accScore);
		}
		return ret;
	}

	template<class It> //여러가지 타입에 대응가능함.
	inline void normalizeCoda(It begin, It end) 
	{
		char16_t before = 0;
		for (auto it = begin; it != end; ++it) 
		{ 
			if (before == 4543 && *it == 12619) // 키윜ㅋㅋ -> 키위ㅋㅋㅋ
			{
				it[-1] = 12619;
			}
			else if (before == 4546 && *it == 12622) // 키윟ㅎㅎ->키위ㅎㅎㅎ
			{ 
				it[-1] = 12622;
			}
			else if (before == 4525 && *it == 12622) // 키윊ㅎㅎ->키윈ㅎㅎ
			{ 
				it[-1] = 4523;
			}
			else if (before == 4534 && *it == 12622) // 키윓ㅎㅎ->키윌ㅎㅎ
			{ 
				it[-1] = 4527;
			}
			else if (before == 4528 && *it == 12593) // 키윍ㄱㄱ->키윌ㄱㄱ
			{ 
				it[-1] = 4527;
			}
			else if (before == 4520 && *it == 12593) // 키윅ㄱㄱ->키위ㄱㄱㄱ
			{ 
				it[-1] = 12593;
			}
			else if (before == 4523 && *it == 12596) // 키윈ㄴㄴ->키위ㄴㄴㄴ
			{ 
				it[-1] = 12596;
			}
			before = *it;

		}
	}

  /**
   * @brief 주어진 문자열에 나타난 개별 문자들에 대해 어절번호(wordPosition) 생성하여 반환한다.
   * @details 문자열의 길이와 동일한 크기의 std::vector<uint16_t>를 생성한 뒤, 문자열 내 개별 문자가
   * 나타난 인덱스와 동일한 위치에 각 문자에 대한 어절번호(wordPosition)를 기록한다.
   * 어절번호는 단순히 각 어절의 등장순서로부터 얻어진다.
   * 예를 들어, '나는 학교에 간다'라는 문자열의 경우 '나는'이 첫 번째 어절이므로 여기에포함된
   * 개별 문자인 '나'와 '는'의 어절번호는 0이다. 마찬가지로 '학교에'에 포함된 문자들의 어절번호는 1, 
   * '간다'에 포함된 문자들의 어절번호는 2이다.
   * 따라서 최종적으로 문자열 '나는 학교에 간다'로부터 {0, 0, 0, 1, 1, 1, 1, 2, 2}의 어절번호가
   * 생성된다.
   *
   * @param sentence 어절번호를 생성할 문장.
   * @return sentence 에 대한 어절번호를 담고있는 vector.
   */
	const std::vector<uint16_t> getWordPositions(const std::u16string& sentence)
	{
		std::vector<uint16_t> wordPositions(sentence.size());
		uint32_t position = 0;

		for (auto i = 0; i < sentence.size(); ++i)
		{
			wordPositions[i] = position;

			if (sentence[i] == u' ')
			{
				++position; 
			}
		}

		return wordPositions;
	}

	std::vector<TokenResult> Kiwi::analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, size_t topN, Match matchOptions) const
	{
		auto nstr = normalizeHangul({ sBegin, sEnd });
		Vector<uint32_t> posMap(nstr.size() + 1);
		for (size_t i = 0; i < nstr.size(); ++i)
		{
			posMap[i + 1] = posMap[i] + (isHangulCoda(nstr[i]) ? 0 : 1);
		}

		if (!!(matchOptions & Match::normalizeCoda)) normalizeCoda(nstr.begin(), nstr.end());
		// 분석할 문장에 포함된 개별 문자에 대해 어절번호를 생성한다
		std::vector<uint16_t> wordPositions = getWordPositions({ sBegin, sEnd });

		auto nodes = (*reinterpret_cast<FnSplitByTrie>(dfSplitByTrie))(formTrie, nstr, matchOptions);
		vector<TokenResult> ret;
		if (nodes.size() <= 2)
		{
			ret.emplace_back();
			return ret;
		}

		Vector<std::pair<PathEvaluator::Path, float>> res = (*reinterpret_cast<FnFindBestPath>(dfFindBestPath))(this, nodes, topN);
		for (auto&& r : res)
		{
			vector<TokenInfo> rarr;
			const KString* prevMorph = nullptr;
			for (auto&& s : r.first)
			{
				if (!get<1>(s).empty() && get<1>(s)[0] == ' ') continue;
				u16string joined;
				do
				{
					if (!integrateAllomorph)
					{
						if (POSTag::ep <= get<0>(s)->tag && get<0>(s)->tag <= POSTag::etm)
						{
							if ((*get<0>(s)->kform)[0] == u'\uC5B4') // 어
							{
								if (prevMorph && prevMorph[0].back() == u'\uD558') // 하
								{
									joined = joinHangul(u"\uC5EC" + get<0>(s)->kform->substr(1)); // 여
									break;
								}
								else if (FeatureTestor::isMatched(prevMorph, CondPolarity::positive))
								{
									joined = joinHangul(u"\uC544" + get<0>(s)->kform->substr(1)); // 아
									break;
								}
							}
						}
					}
					joined = joinHangul(get<1>(s).empty() ? *get<0>(s)->kform : get<1>(s));
				} while (0);
				rarr.emplace_back(joined, get<0>(s)->tag);
				rarr.back().morph = get<0>(s);
				size_t nlen = (get<1>(s).empty() ? *get<0>(s)->kform : get<1>(s)).size();
				size_t nlast = get<2>(s);
				size_t nllast = min(max(nlast, nlen) - nlen, posMap.size() - 1);
				rarr.back().position = posMap[nllast];
				rarr.back().length = posMap[min(nlast, posMap.size() - 1)] - posMap[nllast];

				// Token의 시작위치(position)을 이용해 Token이 포함된 어절번호(wordPosition)를 얻음
        		rarr.back().wordPosition = wordPositions[rarr.back().position];
				prevMorph = get<0>(s)->kform;
			}
			ret.emplace_back(rarr, r.second);
		}
		if (ret.empty()) ret.emplace_back();
		return ret;
	}

	const Morpheme* Kiwi::getDefaultMorpheme(POSTag tag) const
	{
		return &morphemes[(size_t)tag + 1];
	}

	future<vector<TokenResult>> Kiwi::asyncAnalyze(const string& str, size_t topN, Match matchOptions) const
	{
		return pool->enqueue([&, str, topN, matchOptions](size_t)
		{
			return analyze(str, topN, matchOptions);
		});
	}
}
