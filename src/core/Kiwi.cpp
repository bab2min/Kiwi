#include <future>
#include "Kiwi.h"
#include "Utils.h"
#include "KFeatureTestor.h"
#include "logPoisson.h"

using namespace std;
using namespace kiwi;

//#define LOAD_TXT

Kiwi::Kiwi(const char * modelPath, size_t _maxCache, size_t _numThread, size_t options) 
	: numThread(_numThread ? _numThread : thread::hardware_concurrency()),
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
		if (ifs.fail()) throw KiwiException{ "[Kiwi] Failed to find model file '"s + modelPath + "extract.mdl'."  };
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
}

int Kiwi::addUserWord(const u16string & str, KPOSTag tag, float userScore)
{
	mdl->addUserWord(normalizeHangul({ str.begin(), str.end() }), tag, userScore);
	return 0;
}


int Kiwi::loadUserDictionary(const char * userDictPath)
{
	ifstream ifs{ userDictPath };
	if(ifs.fail()) throw KiwiException("[loadUserDictionary] Failed to open '"s + userDictPath + "'"s);
	ifs.exceptions(std::istream::badbit);
	string line;
	while (getline(ifs, line))
	{
		auto wstr = utf8_to_utf16(line);
		if (wstr[0] == u'#') continue;
		auto chunks = split(wstr, u'\t');
		if (chunks.size() < 2) continue;
		if (!chunks[1].empty()) 
		{
			auto pos = makePOSTag(chunks[1]);
			float score = 0.f;
			if (chunks.size() > 2) score = stof(chunks[2].begin(), chunks[2].end());
			if (pos != KPOSTag::MAX)
			{
				addUserWord(chunks[0], pos, score);
			}
			else
			{
				throw KiwiException("[loadUserDictionary] Unknown Tag '" + utf16_to_utf8(chunks[1]) + "'");
			}
		}
	}
	return 0;
}

int Kiwi::prepare()
{
	mdl->solidify();
	return 0;
}

void Kiwi::setCutOffThreshold(float _cutOffThreshold)
{
	cutOffThreshold = std::max(_cutOffThreshold, 1.f);
}

int kiwi::Kiwi::getOption(size_t option) const
{
	if (option == INTEGRATE_ALLOMORPH)
	{
		return integrateAllomorph;
	}
	return 0;
}

void kiwi::Kiwi::setOption(size_t option, int value)
{
	if (option == INTEGRATE_ALLOMORPH)
	{
		integrateAllomorph = value;
	}
}

vector<KWordDetector::WordInfo> Kiwi::extractWords(const function<u16string(size_t)>& reader, size_t minCnt, size_t maxWordLen, float minScore)
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
		KPOSTag pos;
		switch (pos = identifySpecialChr(form.back()))
		{
		case KPOSTag::SF:
		case KPOSTag::SP:
		case KPOSTag::SS:
		case KPOSTag::SE:
		case KPOSTag::SO:
		case KPOSTag::SW:
			return false;
		case KPOSTag::SL:
		case KPOSTag::SN:
		case KPOSTag::SH:
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
		if (r.posScore[KPOSTag::NNP] < posThreshold || !r.posScore[KPOSTag::NNP]) continue;
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

			KResult kr = analyze(r.form);
			if (any_of(kr.first.begin(), kr.first.end(), [](const KWordPair& kp)
			{
				return KPOSTag::JKS <= kp.tag() && kp.tag() <= KPOSTag::ETM;
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

			KResult kr = analyze(subForm);
			if (any_of(kr.first.begin(), kr.first.end(), [](const KWordPair& kp)
			{
				return KPOSTag::JKS <= kp.tag() && kp.tag() <= KPOSTag::ETM;
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

vector<KWordDetector::WordInfo> Kiwi::extractAddWords(const function<u16string(size_t)>& reader, size_t minCnt, size_t maxWordLen, float minScore, float posThreshold)
{
	detector.setParameters(minCnt, maxWordLen, minScore);
	auto ret = filterExtractedWords(detector.extractWords(reader), posThreshold);
	for (auto& r : ret)
	{
		addUserWord(r.form, KPOSTag::NNP, 10.f);
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
	KCondVowel condVowel;
	KCondPolarity condPolar;
	uint8_t ownFormId;
	uint32_t lastPos;
	MInfo(WID _wid = 0, uint8_t _combineSocket = 0,
		KCondVowel _condVowel = KCondVowel::none,
		KCondPolarity _condPolar = KCondPolarity::none,
		uint8_t _ownFormId = 0, uint32_t _lastPos = 0)
		: wid(_wid), combineSocket(_combineSocket),
		condVowel(_condVowel), condPolar(_condPolar), ownFormId(_ownFormId), lastPos(_lastPos)
	{}
};

typedef vector<MInfo, pool_allocator<MInfo>> MInfos;

struct WordLL
{
	MInfos morphs;
	float accScore = 0;
	const KNLangModel::Node* node = nullptr;
};

struct WordLLP
{
	const MInfos* morphs = nullptr;
	float accScore = 0;
	const KNLangModel::Node* node = nullptr;
};

typedef vector<WordLL, pool_allocator<WordLL>> WordLLs;

template<class _Type>
void evalTrigram(const KNLangModel::Node* rootNode, const KMorpheme* morphBase, const WordLL** wBegin, const WordLL** wEnd, 
	array<WID, 4> seq, size_t chSize, const KMorpheme* curMorph, const KGraphNode* node, _Type& maxWidLL)
{
	for (; wBegin != wEnd; ++wBegin)
	{
		const auto* wids = &(*wBegin)->morphs;
		float candScore = (*wBegin)->accScore;
		if (wids->back().combineSocket)
		{
			// always merge <V> <chunk> with the same socket
			if (wids->back().combineSocket != curMorph->combineSocket || !curMorph->chunks)
			{
				continue;
			}
			seq[0] = morphBase[wids->back().wid].getCombined() - morphBase;
		}

		/*if (!KFeatureTestor::isMatched(node->uform.empty() ? curMorph->kform : &node->uform, wids->back().condVowel, wids->back().condPolar))
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

vector<pair<Kiwi::path, float>> Kiwi::findBestPath(const vector<KGraphNode>& graph, const KNLangModel * knlm, const KMorpheme* morphBase, size_t topN) const
{
	vector<WordLLs, pool_allocator<WordLLs>> cache(graph.size());
	const KGraphNode* startNode = &graph.front();
	const KGraphNode* endNode = &graph.back();
	vector<k_string> ownFormList;

	vector<const KMorpheme*> unknownNodeCands, unknownNodeLCands;
	unknownNodeCands.emplace_back(morphBase + (size_t)KPOSTag::NNG + 1);
	unknownNodeCands.emplace_back(morphBase + (size_t)KPOSTag::NNP + 1);

	unknownNodeLCands.emplace_back(morphBase + (size_t)KPOSTag::NNP + 1);

	const auto& evalPath = [&, this](const KGraphNode* node, size_t i, size_t ownFormId, const vector<const KMorpheme*>& cands, bool unknownForm)
	{
		float tMax = -INFINITY;
		for (auto& curMorph : cands)
		{
			array<WID, 4> seq = { 0, };
			uint8_t combSocket = 0;
			KCondVowel condV = KCondVowel::none;
			KCondPolarity condP = KCondPolarity::none;
			size_t chSize = 1;
			bool isUserWord = false;
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
				if (isUserWord = (curMorph->getCombined() ? curMorph->getCombined() : curMorph) - morphBase >= knlm->getVocabSize())
				{
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

			unordered_map<WID, vector<WordLLP, pool_allocator<WordLLP>>, hash<WID>, equal_to<WID>, 
				pool_allocator<pair<const WID, vector<WordLLP, pool_allocator<WordLLP>>>>> maxWidLL;
			vector<const WordLL*, pool_allocator<const WordLL*>> works;
			works.reserve(8);
			for (size_t i = 0; i < KGraphNode::MAX_PREV; ++i)
			{
				auto* prev = node->getPrev(i);
				if (!prev) break;
				for (auto& p : cache[prev - startNode])
				{
					works.emplace_back(&p);
				}
			}

			evalTrigram(knlm->getRoot(), morphBase, &works[0], &works[0] + works.size(), seq, chSize, curMorph, node, maxWidLL);

			float estimatedLL = 0;
			if (isUserWord)
			{
				estimatedLL = curMorph->userScore;
			}
			// if a form of the node is unknown, calculate log poisson distribution for word-tag
			else if (unknownForm)
			{
				size_t unknownLen = node->uform.empty() ? node->form->form.size() : node->uform.size();
				if (curMorph->tag == KPOSTag::NNG) estimatedLL = LogPoisson::getLL(4.622955f, unknownLen);
				else if (curMorph->tag == KPOSTag::NNP) estimatedLL = LogPoisson::getLL(5.177622f, unknownLen);
				else if (curMorph->tag == KPOSTag::MAG) estimatedLL = LogPoisson::getLL(4.557326f, unknownLen);
				estimatedLL -= 5 + unknownLen * 3;
			}

			float discountForCombining = curMorph->combineSocket ? -15.f : 0.f;
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
								0, KCondVowel::none, KCondPolarity::none, 0, wids.back().lastPos};
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
							combSocket, KCondVowel::none, KCondPolarity::none, ownFormId, node->lastPos);
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
			if (all_of(node->form->candidate.begin(), node->form->candidate.end(), [](const KMorpheme* m)
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
		WordLLs reduced;
		for (auto& c : cache[i])
		{
			if (c.accScore > tMax - cutOffThreshold) reduced.emplace_back(move(c));
		}
		cache[i] = move(reduced);

#ifdef DEBUG_PRINT
		cout << "== " << i << " ==" << endl;
		for (auto& tt : cache[i])
		{
			cout << tt.accScore << '\t';
			for (auto& m : tt.morphs)
			{
				cout << morphBase[m.wid] << '\t';
			}
			cout << endl;
		}
		cout << "========" << endl;
#endif
	}

	// end node
	for (size_t i = 0; i < KGraphNode::MAX_PREV; ++i)
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
			cout << morphBase[m.wid] << '\t';
		}
		cout << endl;
	}
	cout << "========" << endl;

#endif

	vector<pair<path, float>> ret;
	for (size_t i = 0; i < min(topN, cand.size()); ++i)
	{
		path mv(cand[i].morphs.size() - 1);
		transform(cand[i].morphs.begin() + 1, cand[i].morphs.end(), mv.begin(), [morphBase, &ownFormList](const MInfo& m)
		{
			if (m.ownFormId)	return make_tuple(morphBase + m.wid, ownFormList[m.ownFormId - 1], m.lastPos);
			else return make_tuple(morphBase + m.wid, k_string{}, m.lastPos);
		});
		ret.emplace_back(mv, cand[i].accScore);
	}
	return ret;
}


KResult Kiwi::analyze(const u16string & str) const
{
	return analyze(str, 1)[0];
}

KResult Kiwi::analyze(const string & str) const
{
	return analyze(utf8_to_utf16(str));
}

vector<KResult> Kiwi::analyze(const string & str, size_t topN) const
{
	return analyze(utf8_to_utf16(str), topN);
}

void Kiwi::analyze(size_t topN, const function<u16string(size_t)>& reader, const function<void(size_t, vector<KResult>&&)>& receiver) const
{
	if (numThread > 1)
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
			ThreadPool workers{ numThread - 1, numThread * 64 };
			for (id = 0; ; ++id)
			{
				auto ustr = reader(id);
				if (ustr.empty()) break;

				sharedResult.consumeResult(receiver);
				workers.enqueue([this, topN, id, ustr, &receiver, &sharedResult](size_t tid)
				{
					auto r = analyze(ustr, topN);
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
			auto ustr = reader(id);
			if (ustr.empty()) break;
			auto r = analyze(ustr, topN);
			receiver(id, move(r));
		}
	}
}

void Kiwi::perform(size_t topN, const function<u16string(size_t)>& reader, const function<void(size_t, vector<KResult>&&)>& receiver, size_t minCnt, size_t maxWordLen, float minScore, float posThreshold) const
{
	auto old = make_unique<KModelMgr>(*mdl);
	swap(old, const_cast<Kiwi*>(this)->mdl);
	const_cast<Kiwi*>(this)->extractAddWords(reader, minCnt, maxWordLen, minScore, posThreshold);
	const_cast<Kiwi*>(this)->prepare();
	analyze(topN, reader, receiver);
	swap(old, const_cast<Kiwi*>(this)->mdl);
}

std::vector<KResult> Kiwi::analyzeSent(const std::u16string::const_iterator & sBegin, const std::u16string::const_iterator & sEnd, size_t topN) const
{
	auto nstr = normalizeHangul({ sBegin, sEnd });
	vector<uint32_t> posMap(nstr.size() + 1);
	for (size_t i = 0; i < nstr.size(); ++i)
	{
		posMap[i + 1] = posMap[i] + (isHangulCoda(nstr[i]) ? 0 : 1);
	}

	auto nodes = mdl->getTrie()->split(nstr);
	vector<KResult> ret;
	if (nodes.size() <= 2)
	{
		ret.emplace_back();
		ret.back().first.emplace_back(u16string{sBegin, sEnd}, KPOSTag::UNKNOWN, 0, distance(sBegin, sEnd));
		return ret;
	}
	auto res = findBestPath(nodes, mdl->getLangModel(), mdl->getMorphemes(), topN);
	for (auto&& r : res)
	{
		vector<KWordPair> rarr;
		const k_string* prevMorph = nullptr;
		for (auto&& s : r.first)
		{
			if (!get<1>(s).empty() && get<1>(s)[0] == ' ') continue;
			u16string joined;
			do
			{
				if (!integrateAllomorph)
				{
					if (KPOSTag::EP <= get<0>(s)->tag && get<0>(s)->tag <= KPOSTag::ETM)
					{
						if ((*get<0>(s)->kform)[0] == u'어')
						{
							if (prevMorph && prevMorph[0].back() == u'하')
							{
								joined = joinHangul(u"여" + get<0>(s)->kform->substr(1));
								break;
							}
							else if (KFeatureTestor::isMatched(prevMorph, KCondPolarity::positive))
							{
								joined = joinHangul(u"아" + get<0>(s)->kform->substr(1));
								break;
							}
						}
					}
				}
				joined = joinHangul(get<1>(s).empty() ? *get<0>(s)->kform : get<1>(s));
			} while (0);
			rarr.emplace_back(joined, get<0>(s)->tag, 0, 0);
			size_t nlen = (get<1>(s).empty() ? *get<0>(s)->kform : get<1>(s)).size();
			size_t nlast = get<2>(s);
			size_t nllast = nlast >= nlen ? nlast - nlen : 0;
			rarr.back().pos() = posMap[nllast];
			rarr.back().len() = posMap[nlast] - posMap[nllast];
			prevMorph = get<0>(s)->kform;
		}
		ret.emplace_back(rarr, r.second);
	}
	if (ret.empty()) ret.emplace_back();
	return ret;
}

vector<KResult> Kiwi::analyze(const u16string & str, size_t topN) const
{
	if (!mdl->getTrie()) throw KiwiException("Model should be prepared before analyzing.");
	auto chunk = str.begin();
	vector<u16string::const_iterator> sents;
	sents.emplace_back(chunk);
	while (chunk != str.end())
	{
		KPOSTag tag = identifySpecialChr(*chunk);
		while (chunk != str.end() && !(tag == KPOSTag::SF || tag == KPOSTag::SS || tag == KPOSTag::SE || tag == KPOSTag::SW || *chunk == u':'))
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
		if (tag == KPOSTag::SE)
		{
			sents.emplace_back(chunk);
			continue;
		}
		if (chunk != str.end() && (identifySpecialChr(*chunk) == KPOSTag::UNKNOWN || identifySpecialChr(*chunk) == KPOSTag::SF))
		{
			while (chunk != str.end() && (identifySpecialChr(*chunk) == KPOSTag::UNKNOWN || identifySpecialChr(*chunk) == KPOSTag::SF))
			{
				++chunk;
			}
			sents.emplace_back(chunk);
		}
	}
	if(sents.back() != str.end()) sents.emplace_back(str.end());
	if (sents.size() <= 1) return vector<KResult>(1);
	vector<KResult> ret = analyzeSent(sents[0], sents[1], topN);
	if (ret.empty())
	{
		return vector<KResult>(1);
	}
	while (ret.size() < topN) ret.emplace_back(ret.back());
	for (size_t i = 2; i < sents.size(); ++i)
	{
		auto res = analyzeSent(sents[i - 1], sents[i], topN);
		if (res.empty()) continue;
		for (size_t n = 0; n < topN; ++n)
		{
			auto& r = res[min(n, res.size() - 1)];
			transform(r.first.begin(), r.first.end(), back_inserter(ret[n].first), [&sents, i](KWordPair& p)
			{
				p.pos() += distance(sents[0], sents[i - 1]);
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

int Kiwi::getVersion()
{
	return 70;
}

std::u16string Kiwi::toU16(const std::string & str)
{
	return utf8_to_utf16(str);
}

std::string Kiwi::toU8(const std::u16string & str)
{
	return utf16_to_utf8(str);
}

std::ostream & operator<<(std::ostream & os, const KWordPair & kp)
{
	return os << utf16_to_utf8({ kp.str().begin(), kp.str().end() }) << '/' << tagToString(kp.tag());
}
