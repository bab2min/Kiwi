#include <kiwi/Kiwi.h>
#include "Utils.h"
#include "KTrie.h"
#include "KFeatureTestor.h"
#include "logPoisson.h"
#include "KNLangModel.h"
#include "FrozenTrie.hpp"

using namespace std;

namespace kiwi
{
	namespace v1
	{
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

		using Wid = uint16_t;
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
			const KNLangModel::Node* node = nullptr;

			WordLL()
			{
			}

			WordLL(const MInfos& _morphs, float _accScore, const KNLangModel::Node* _node)
				: morphs{ _morphs }, accScore{ _accScore }, node{ _node }
			{
			}
		};

		struct WordLLP
		{
			const MInfos* morphs = nullptr;
			float accScore = 0;
			const KNLangModel::Node* node = nullptr;

			WordLLP()
			{
			}

			WordLLP(const MInfos* _morphs, float _accScore, const KNLangModel::Node* _node)
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
			std::sort(v.rbegin(), v.rend());
			return v[std::min(nth, v.size() - 1)];
		}

		template<class _Type>
		void evalTrigram(const KNLangModel::Node* rootNode, const Morpheme* morphBase, const Vector<KString>& ownForms, const WordLL** wBegin, const WordLL** wEnd,
			array<Wid, 4> seq, size_t chSize, const Morpheme* curMorph, const KGraphNode* node, _Type& maxWidLL)
		{
			for (; wBegin != wEnd; ++wBegin)
			{
				const auto* wids = &(*wBegin)->morphs;
				float candScore = (*wBegin)->accScore;
				if (wids->back().combineSocket)
				{
					// always merge <v> <chunk> with the same socket
					if (wids->back().combineSocket != curMorph->combineSocket || !curMorph->chunks)
					{
						continue;
					}
					seq[0] = morphBase[wids->back().wid].getCombined() - morphBase;
				}

				/*auto leftForm = wids->back().ownFormId ? &ownForms[wids->back().ownFormId - 1] : morphBase[wids->back().wid].kform;

				if (!KFeatureTestor::isMatched(leftForm, curMorph->vowel, curMorph->polar))
				{
					continue;
				}*/

				auto cNode = (*wBegin)->node;
				Wid lSeq = 0;
				if (curMorph->combineSocket && !curMorph->chunks)
				{
					lSeq = wids->back().wid;
				}
				else
				{
					lSeq = seq[chSize - 1];
					for (size_t i = 0; i < chSize; ++i)
					{
						auto cn = seq[i];
						float ll;
						auto nextNode = cNode->getNextTransition(cn, 2, ll);
						candScore += ll;
						cNode = nextNode ? nextNode : rootNode;
					}
				}
				emplaceMaxCnt(maxWidLL, lSeq, WordLLP{ wids, candScore, cNode }, 3, [](const WordLLP& a, const WordLLP& b) { return a.accScore > b.accScore; });
			}
		}

		auto Kiwi::findBestPath(const Vector<KGraphNode>& graph, const KNLangModel* knlm, size_t topN) const -> Vector<std::pair<Path, float>>
		{
			Vector<WordLLs> cache(graph.size());
			const KGraphNode* startNode = &graph.front();
			const KGraphNode* endNode = &graph.back();
			Vector<KString> ownFormList;

			vector<const Morpheme*> unknownNodeCands, unknownNodeLCands;
			unknownNodeCands.emplace_back(morphemes.data() + (size_t)POSTag::nng + 1);
			unknownNodeCands.emplace_back(morphemes.data() + (size_t)POSTag::nnp + 1);

			unknownNodeLCands.emplace_back(morphemes.data() + (size_t)POSTag::nnp + 1);

			const auto& evalPath = [&, this](const KGraphNode* node, size_t i, size_t ownFormId, const vector<const Morpheme*>& cands, bool unknownForm)
			{
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
					if (curMorph->chunks)
					{
						chSize = curMorph->chunks->size();
						for (size_t i = 0; i < chSize; ++i)
						{
							seq[i] = (*curMorph->chunks)[i] - morphemes.data();
						}
					}
					else
					{
						if ((curMorph->getCombined() ? curMorph->getCombined() : curMorph) - morphemes.data() >= knlm->getVocabSize())
						{
							isUserWord = true;
							seq[0] = getDefaultMorpheme(curMorph->tag) - morphemes.data();
						}
						else
						{
							seq[0] = curMorph - morphemes.data();
						}
						combSocket = curMorph->combineSocket;
					}
					condV = curMorph->vowel;
					condP = curMorph->polar;

					UnorderedMap<Wid, Vector<WordLLP>> maxWidLL;
					Vector<const WordLL*> works;
					works.reserve(8);
					float discountForCombining = 0;

					for (size_t i = 0; i < KGraphNode::max_prev; ++i)
					{
						auto* prev = node->getPrev(i);
						if (!prev) break;
						for (auto& p : cache[prev - startNode])
						{
							works.emplace_back(&p);
						}
					}

					evalTrigram(knlm->getRoot(), morphemes.data(), ownFormList, &works[0], &works[0] + works.size(), seq, chSize, curMorph, node, maxWidLL);

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
							if (q.accScore <= tMax - cutOffThreshold) continue;
							nCache.emplace_back(WordLL{ MInfos{}, q.accScore, q.node });
							auto& wids = nCache.back().morphs;
							wids.reserve(q.morphs->size() + chSize);
							wids = *q.morphs;
							if (curMorph->chunks)
							{
								size_t lastPos = node->lastPos;
								if (curMorph->combineSocket)
								{
									wids.back() = MInfo{ (Wid)(morphemes[wids.back().wid].getCombined() - morphemes.data()),
										0, CondVowel::none, CondPolarity::none, 0, wids.back().lastPos };
									lastPos += morphemes[seq[0]].kform->size() - 1;
									for (size_t ch = 1; ch < chSize; ++ch)
									{
										wids.emplace_back(seq[ch], 0, condV, condP, 0, lastPos);
										lastPos += morphemes[seq[ch]].kform->size() - 1;
									}
								}
								else
								{
									for (size_t ch = 0; ch < chSize; ++ch)
									{
										wids.emplace_back(seq[ch], 0, condV, condP, 0, lastPos);
										lastPos += morphemes[seq[ch]].kform->size() - 1;
									}
								}
							}
							else
							{
								wids.emplace_back(isUserWord ? curMorph - morphemes.data() : seq[0],
									combSocket, CondVowel::none, CondPolarity::none, ownFormId, node->lastPos);
							}
						}
					}
				}
				return tMax;
			};

			// start node
			cache.front().emplace_back(WordLL{ MInfos{ MInfo(0u) }, 0.f, knlm->getRoot()->getNextFromBaked(0) });

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
					tMax = evalPath(node, i, ownFormId, node->form->candidate, false);
					if (all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const Morpheme* m)
					{
						return m->combineSocket || m->chunks;
					}))
					{
						ownFormList.emplace_back(node->form->form);
						ownFormId = ownFormList.size();
						tMax = min(tMax, evalPath(node, i, ownFormId, unknownNodeLCands, true));
					};
				}
				else
				{
					tMax = evalPath(node, i, ownFormId, unknownNodeCands, true);
				}

				// heuristically removing lower ll to speed up
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

					combinedCutOffScore = min(tMax - cutOffThreshold, combinedCutOffScore);
					otherCutOffScore = min(tMax - cutOffThreshold, otherCutOffScore);
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
						morphemes[m.wid].print(cout) << '\t';
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
					if (!KFeatureTestor::isMatched(nullptr, p.morphs.back().condVowel)) continue;

					float c = p.accScore + p.node->getLL(1, 2);
					cache.back().emplace_back(WordLL{ p.morphs, c, nullptr });
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
					morphemes[m.wid].print(cout) << '\t';
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
					if (m.ownFormId)	return make_tuple(&morphemes[m.wid], ownFormList[m.ownFormId - 1], m.lastPos);
					else return make_tuple(&morphemes[m.wid], KString{}, m.lastPos);
				});
				ret.emplace_back(mv, cand[i].accScore);
			}
			return ret;
		}

		std::vector<TokenResult> Kiwi::analyzeSent(const std::u16string::const_iterator& sBegin, const std::u16string::const_iterator& sEnd, size_t topN, Match matchOptions) const
		{
			auto nstr = normalizeHangul({ sBegin, sEnd });
			Vector<uint32_t> posMap(nstr.size() + 1);
			for (size_t i = 0; i < nstr.size(); ++i)
			{
				posMap[i + 1] = posMap[i] + (isHangulCoda(nstr[i]) ? 0 : 1);
			}

			auto nodes = splitByTrie(formTrie, nstr, pm.get(), matchOptions);
			vector<TokenResult> ret;
			if (nodes.size() <= 2)
			{
				ret.emplace_back();
				return ret;
			}
			auto res = findBestPath(nodes, /*getLangModel()*/nullptr, topN);
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
								if ((*get<0>(s)->kform)[0] == u'어')
								{
									if (prevMorph && prevMorph[0].back() == u'하')
									{
										joined = joinHangul(u"여" + get<0>(s)->kform->substr(1));
										break;
									}
									else if (KFeatureTestor::isMatched(prevMorph, CondPolarity::positive))
									{
										joined = joinHangul(u"아" + get<0>(s)->kform->substr(1));
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
					prevMorph = get<0>(s)->kform;
				}
				ret.emplace_back(rarr, r.second);
			}
			if (ret.empty()) ret.emplace_back();
			return ret;
		}

	}
}