#include <iostream>
#include <fstream>
#include <future>

#include "Kiwi.h"
#include "Utils.h"
#include "KFeatureTestor.h"
#include "logPoisson.h"

using namespace std;
using namespace kiwi;


//#define LOAD_TXT

Kiwi::Kiwi(const char * modelPath, size_t _maxCache, size_t _numThread, size_t options) 
	: numThreads(_numThread ? _numThread : thread::hardware_concurrency()),
	detector(10, 10, 0.1, _numThread)
{
#ifdef LOAD_TXT
	detector.loadPOSModelFromTxt(ifstream{ modelPath + string{ "RPosModel.txt" } });
	detector.loadNounTailModelFromTxt(ifstream{ modelPath + string{ "NounTailList.txt" } });
	{
		ofstream ofs{ modelPath + string{ "extract.mdl" }, ios_base::binary };
		detector.savePOSModel(ofs);
		detector.saveNounTailModel(ofs);
	}
#else
	{
		ifstream ifs{ modelPath + string{ "extract.mdl" }, ios_base::binary };
		if (ifs.fail()) throw Exception{ std::string{"[Kiwi] Failed to find model file '"} +modelPath + "extract.mdl'." };
		detector.loadPOSModel(ifs);
		detector.loadNounTailModel(ifs);
	}
#endif
	mdl = make_unique<KModelMgr>(modelPath);

	if (options & LOAD_DEFAULT_DICT)
	{
		loadUserDictionary((modelPath + string{ "default.dict" }).c_str());
	}

	integrateAllomorph = options & INTEGRATE_ALLOMORPH;

	pm = PatternMatcher::create();
}

int Kiwi::addUserWord(const u16string & str, POSTag tag, float userScore)
{
	mdl->addUserWord(normalizeHangul({ str.begin(), str.end() }), tag, userScore);
	return 0;
}


int Kiwi::loadUserDictionary(const char * userDictPath)
{
	ifstream ifs{ userDictPath };
	if (ifs.fail()) throw Exception(std::string{ "[loadUserDictionary] Failed to open '" } +userDictPath + "'");
	ifs.exceptions(std::istream::badbit);
	string line;
	while (getline(ifs, line))
	{
		auto wstr = utf8To16(line);
		if (wstr[0] == u'#') continue;
		auto chunks = split(wstr, u'\t');
		if (chunks.size() < 2) continue;
		if (!chunks[1].empty()) 
		{
			auto pos = toPOSTag(chunks[1]);
			float score = 0.f;
			if (chunks.size() > 2) score = stof(chunks[2].begin(), chunks[2].end());
			if (pos != POSTag::max)
			{
				addUserWord(chunks[0], pos, score);
			}
			else
			{
				throw Exception("[loadUserDictionary] Unknown Tag '" + utf16To8(chunks[1]) + "'");
			}
		}
	}
	return 0;
}

int Kiwi::prepare()
{
	mdl->solidify();
	workers = make_unique<utils::ThreadPool>(numThreads, numThreads * 64);
	return 0;
}

void Kiwi::setCutOffThreshold(float _cutOffThreshold)
{
	cutOffThreshold = std::max(_cutOffThreshold, 1.f);
}

int Kiwi::getOption(size_t option) const
{
	if (option == INTEGRATE_ALLOMORPH)
	{
		return integrateAllomorph;
	}
	return 0;
}

void Kiwi::setOption(size_t option, int value)
{
	if (option == INTEGRATE_ALLOMORPH)
	{
		integrateAllomorph = value;
	}
}

vector<KWordDetector::WordInfo> Kiwi::extractWords(const U16MultipleReader& reader, size_t minCnt, size_t maxWordLen, float minScore)
{
	detector.setParameters(minCnt, maxWordLen, minScore);
	return detector.extractWords(reader);
}

vector<KWordDetector::WordInfo> Kiwi::filterExtractedWords(vector<KWordDetector::WordInfo>&& words, float posThreshold) const
{
	auto old = make_unique<KModelMgr>(*mdl);
	swap(old, const_cast<Kiwi*>(this)->mdl);
	const_cast<Kiwi*>(this)->prepare();
	
	const auto& specialChrTest = [](const u16string& form) -> bool
	{
		POSTag pos;
		switch (pos = identifySpecialChr(form.back()))
		{
		case POSTag::sf:
		case POSTag::sp:
		case POSTag::ss:
		case POSTag::se:
		case POSTag::so:
		case POSTag::sw:
			return false;
		case POSTag::sl:
		case POSTag::sn:
		case POSTag::sh:
			if (all_of(form.begin(), form.end(), [pos](char16_t c) {
				return pos == identifySpecialChr(c);
			})) return false;
		default:
			return true;
		}
	};
	vector<KWordDetector::WordInfo> ret;
	auto allForms = mdl->getAllForms();
	for (auto& r : words)
	{
		if (r.posScore[POSTag::nnp] < posThreshold || !r.posScore[POSTag::nnp]) continue;
		char16_t bracket = 0;
		switch (r.form.back())
		{
		case u')':
			if (r.form.find(u'(') == r.form.npos) continue;
			bracket = u'(';
			break;
		case u']':
			if (r.form.find(u'[') == r.form.npos) continue;
			bracket = u'[';
			break;
		case u'}':
			if (r.form.find(u'{') == r.form.npos) continue;
			bracket = u'{';
			break;
		case u'(':
		case u'[':
		case u'{':
			r.form.pop_back();
		default:
			if (r.form.find(u'(') != r.form.npos && r.form.find(u')') == r.form.npos)
			{
				bracket = u'(';
				goto onlyBracket;
			}
			else if (r.form.find(u'[') != r.form.npos && r.form.find(u']') == r.form.npos)
			{
				bracket = u'[';
				goto onlyBracket;
			}
			else if (r.form.find(u'{') != r.form.npos && r.form.find(u'}') == r.form.npos)
			{
				bracket = u'{';
				goto onlyBracket;
			}
			if (!specialChrTest(r.form)) continue;
		}

		{
			auto normForm = normalizeHangul(r.form);
			if (allForms.count(normForm)) continue;

			KResult kr = analyze(r.form, Match::none);
			if (any_of(kr.first.begin(), kr.first.end(), [](const TokenInfo& kp)
			{
				return POSTag::jks <= kp.tag && kp.tag <= POSTag::etm;
			}) && kr.second >= -35)
			{
				continue;
			}

			allForms.emplace(normForm);
			ret.emplace_back(r);
		}
	onlyBracket:;
		if (bracket)
		{
			auto subForm = r.form.substr(0, r.form.find(bracket));
			if (subForm.empty()) continue;
			if (!specialChrTest(subForm)) continue;
			auto subNormForm = normalizeHangul(subForm);
			if (allForms.count(subNormForm)) continue;

			KResult kr = analyze(subForm, Match::none);
			if (any_of(kr.first.begin(), kr.first.end(), [](const TokenInfo& kp)
			{
				return POSTag::jks <= kp.tag && kp.tag <= POSTag::etm;
			}) && kr.second >= -35)
			{
				continue;
			}

			allForms.emplace(subNormForm);
			ret.emplace_back(r);
			ret.back().form = subForm;
		}
	}
	swap(old, const_cast<Kiwi*>(this)->mdl);
	return ret;
}

vector<KWordDetector::WordInfo> Kiwi::extractAddWords(const U16MultipleReader& reader, size_t minCnt, size_t maxWordLen, float minScore, float posThreshold)
{
	detector.setParameters(minCnt, maxWordLen, minScore);
	auto ret = filterExtractedWords(detector.extractWords(reader), posThreshold);
	for (auto& r : ret)
	{
		addUserWord(r.form, POSTag::nnp, 10.f);
	}
	return ret;
}

//#define DEBUG_PRINT

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

typedef KNLangModel::WID WID;
struct MInfo
{
	WID wid;
	uint8_t combineSocket;
	CondVowel condVowel;
	CondPolarity condPolar;
	uint8_t ownFormId;
	uint32_t lastPos;
	MInfo(WID _wid = 0, uint8_t _combineSocket = 0,
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
		: morphs{_morphs}, accScore{_accScore}, node{_node}
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
		: morphs{_morphs}, accScore{_accScore}, node{_node}
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
		if(filter(*first)) v.emplace_back(fn(*first));
	}
	if (v.empty()) return {};
	std::sort(v.rbegin(), v.rend());
	return v[std::min(nth, v.size() - 1)];
}

template<class _Type>
void evalTrigram(const KNLangModel::Node* rootNode, const Morpheme* morphBase, const Vector<KString>& ownForms, const WordLL** wBegin, const WordLL** wEnd, 
	array<WID, 4> seq, size_t chSize, const Morpheme* curMorph, const KGraphNode* node, _Type& maxWidLL)
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
		WID lSeq = 0;
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

Vector<pair<Kiwi::Path, float>> Kiwi::findBestPath(const Vector<KGraphNode>& graph, const KNLangModel * knlm, const Morpheme* morphBase, size_t topN) const
{
	Vector<WordLLs> cache(graph.size());
	const KGraphNode* startNode = &graph.front();
	const KGraphNode* endNode = &graph.back();
	Vector<KString> ownFormList;

	vector<const Morpheme*> unknownNodeCands, unknownNodeLCands;
	unknownNodeCands.emplace_back(morphBase + (size_t)POSTag::nng + 1);
	unknownNodeCands.emplace_back(morphBase + (size_t)POSTag::nnp + 1);

	unknownNodeLCands.emplace_back(morphBase + (size_t)POSTag::nnp + 1);

	const auto& evalPath = [&, this](const KGraphNode* node, size_t i, size_t ownFormId, const vector<const Morpheme*>& cands, bool unknownForm)
	{
		float tMax = -INFINITY;
		for (auto& curMorph : cands)
		{
			array<WID, 4> seq = { 0, };
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
					seq[i] = (*curMorph->chunks)[i] - morphBase;
				}
			}
			else
			{
				if ((curMorph->getCombined() ? curMorph->getCombined() : curMorph) - morphBase >= knlm->getVocabSize())
				{
					isUserWord = true;
					seq[0] = mdl->getDefaultMorpheme(curMorph->tag) - morphBase;
				}
				else
				{
					seq[0] = curMorph - morphBase;
				}
				combSocket = curMorph->combineSocket;
			}
			condV = curMorph->vowel;
			condP = curMorph->polar;

			UnorderedMap<WID, Vector<WordLLP>> maxWidLL;
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

			evalTrigram(knlm->getRoot(), morphBase, ownFormList, &works[0], &works[0] + works.size(), seq, chSize, curMorph, node, maxWidLL);

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
							wids.back() = MInfo{ (WID)(morphBase[wids.back().wid].getCombined() - morphBase),
								0, CondVowel::none, CondPolarity::none, 0, wids.back().lastPos};
							lastPos += morphBase[seq[0]].kform->size() - 1;
							for (size_t ch = 1; ch < chSize; ++ch)
							{
								wids.emplace_back(seq[ch], 0, condV, condP, 0, lastPos);
								lastPos += morphBase[seq[ch]].kform->size() - 1;
							}
						}
						else
						{
							for (size_t ch = 0; ch < chSize; ++ch)
							{
								wids.emplace_back(seq[ch], 0, condV, condP, 0, lastPos);
								lastPos += morphBase[seq[ch]].kform->size() - 1;
							}
						}
					}
					else
					{
						wids.emplace_back(isUserWord ? curMorph - morphBase : seq[0], 
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
			if (!KFeatureTestor::isMatched(nullptr, p.morphs.back().condVowel)) continue;

			float c = p.accScore + p.node->getLL(1, 2);
			cache.back().emplace_back(WordLL{p.morphs, c, nullptr});
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
		transform(cand[i].morphs.begin() + 1, cand[i].morphs.end(), mv.begin(), [morphBase, &ownFormList](const MInfo& m)
		{
			if (m.ownFormId)	return make_tuple(morphBase + m.wid, ownFormList[m.ownFormId - 1], m.lastPos);
			else return make_tuple(morphBase + m.wid, KString{}, m.lastPos);
		});
		ret.emplace_back(mv, cand[i].accScore);
	}
	return ret;
}


KResult Kiwi::analyze(const u16string & str, Match matchOptions) const
{
	return analyze(str, 1, matchOptions)[0];
}

KResult Kiwi::analyze(const string & str, Match matchOptions) const
{
	return analyze(utf8To16(str), matchOptions);
}

vector<KResult> Kiwi::analyze(const string & str, size_t topN, Match matchOptions) const
{
	return analyze(utf8To16(str), topN, matchOptions);
}

future<vector<KResult>> Kiwi::asyncAnalyze(const string & str, size_t topN, Match matchOptions) const
{
	return workers->enqueue([&, str, topN, matchOptions](size_t)
	{
		return analyze(str, topN, matchOptions);
	});
}

void Kiwi::analyze(size_t topN, const U16Reader& reader, const function<void(size_t, vector<KResult>&&)>& receiver, Match matchOptions) const
{
	if (numThreads > 1)
	{
		struct
		{
			map<size_t, vector<KResult>> res;
			mutex resMutex;
			size_t outputId = 0;

			void addResult(size_t id, vector<KResult>&& r)
			{
				lock_guard<mutex> l(resMutex);
				res.emplace(id, r);
			}

			void consumeResult(const function<void(size_t, vector<KResult>&&)>& receiver)
			{
				lock_guard<mutex> l(resMutex);
				while (!res.empty() && res.begin()->first == outputId)
				{
					receiver(outputId++, move(res.begin()->second));
					res.erase(res.begin());
				}
			}

		} sharedResult;

		size_t id;
		{
			for (id = 0; ; ++id)
			{
				auto ustr = reader();
				if (ustr.empty()) break;

				sharedResult.consumeResult(receiver);
				workers->enqueue([this, topN, matchOptions, id, ustr, &receiver, &sharedResult](size_t tid)
				{
					auto r = analyze(ustr, topN, matchOptions);
					sharedResult.addResult(id, move(r));
				});
			}
		}
	
		while (sharedResult.outputId < id)
		{
			sharedResult.consumeResult(receiver);
		}
	}
	else
	{
		for (size_t id = 0; ; ++id)
		{
			auto ustr = reader();
			if (ustr.empty()) break;
			auto r = analyze(ustr, topN, matchOptions);
			receiver(id, move(r));
		}
	}
}

void Kiwi::perform(size_t topN, const U16MultipleReader& reader, const function<void(size_t, vector<KResult>&&)>& receiver, Match matchOptions, size_t minCnt, size_t maxWordLen, float minScore, float posThreshold) const
{
	auto old = make_unique<KModelMgr>(*mdl);
	swap(old, const_cast<Kiwi*>(this)->mdl);
	const_cast<Kiwi*>(this)->extractAddWords(reader, minCnt, maxWordLen, minScore, posThreshold);
	const_cast<Kiwi*>(this)->prepare();
	analyze(topN, reader(), receiver, matchOptions);
	swap(old, const_cast<Kiwi*>(this)->mdl);
}

std::vector<KResult> Kiwi::analyzeSent(const std::u16string::const_iterator & sBegin, const std::u16string::const_iterator & sEnd, size_t topN, Match matchOptions) const
{
	auto nstr = normalizeHangul({ sBegin, sEnd });
	Vector<uint32_t> posMap(nstr.size() + 1);
	for (size_t i = 0; i < nstr.size(); ++i)
	{
		posMap[i + 1] = posMap[i] + (isHangulCoda(nstr[i]) ? 0 : 1);
	}

	auto nodes = splitByTrie(mdl->fTrie, nstr, pm.get(), matchOptions);
	vector<KResult> ret;
	if (nodes.size() <= 2)
	{
		ret.emplace_back();
		return ret;
	}
	auto res = findBestPath(nodes, mdl->getLangModel(), mdl->getMorphemes(), topN);
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

vector<KResult> Kiwi::analyze(const u16string & str, size_t topN, Match matchOptions) const
{
	if (!mdl->ready()) throw Exception("Model should be prepared before analyzing.");
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
	if(sents.back() != str.end()) sents.emplace_back(str.end());
	if (sents.size() <= 1) return vector<KResult>(1);
	vector<KResult> ret = analyzeSent(sents[0], sents[1], topN, matchOptions);
	if (ret.empty())
	{
		return vector<KResult>(1);
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

void Kiwi::clearCache()
{

}

const Morpheme* Kiwi::getMorphs() const
{
	return mdl->getMorphemes();
}

size_t Kiwi::getNumMorphs() const
{
	return mdl->getNumMorphemes();
}

std::u16string Kiwi::getFormOfMorph(size_t id) const
{
	auto& morph = mdl->getMorphemes()[id];
	if (!morph.kform) return {};
	if (mdl->ready())
	{
		auto& form = *morph.kform;
		return { form.begin(), form.end() };
	}
	else
	{
		auto& form = mdl->getForms()[(size_t)morph.kform].form;
		return { form.begin(), form.end() };
	}
}

const char* Kiwi::getVersion()
{
	return "0.10.0";
}

std::u16string Kiwi::toU16(const std::string & str)
{
	return utf8To16(str);
}

std::string Kiwi::toU8(const std::u16string & str)
{
	return utf16To8(str);
}

std::ostream& operator<<(std::ostream& os, const TokenInfo& kp)
{
	return os << utf16To8({ kp.str.begin(), kp.str.end() }) << '/' << tagToString(kp.tag);
}
