#include "stdafx.h"
#include "Utils.h"
#include "Kiwi.h"
#include "KFeatureTestor.h"
#include "logPoisson.h"
#include <future>

using namespace std;

//#define LOAD_TXT

Kiwi::Kiwi(const char * modelPath, size_t _maxCache, size_t _numThread) 
	: workers(_numThread ? _numThread : thread::hardware_concurrency()),
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
		detector.loadPOSModel(ifs);
		detector.loadNounTailModel(ifs);
	}
#endif
	mdl = make_unique<KModelMgr>(modelPath);
}

int Kiwi::addUserWord(const u16string & str, KPOSTag tag, float userScore)
{
	mdl->addUserWord(normalizeHangul({ str.begin(), str.end() }), tag, userScore);
	return 0;
}


int Kiwi::loadUserDictionary(const char * userDictPath)
{
	ifstream ifs{ userDictPath };
	ifs.exceptions(std::istream::failbit | std::istream::badbit);
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
			if (pos != KPOSTag::MAX)
			{
				addUserWord(chunks[0], pos);
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

vector<KWordDetector::WordInfo> Kiwi::extractWords(const function<u16string(size_t)>& reader, size_t minCnt, size_t maxWordLen, float minScore)
{
	detector.setParameters(minCnt, maxWordLen, minScore);
	return detector.extractWords(reader);
}

vector<KWordDetector::WordInfo> Kiwi::filterExtractedWords(vector<KWordDetector::WordInfo>&& words, float posThreshold) const
{
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
		}
		return true;
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
			allForms.emplace(subNormForm);
			ret.emplace_back(r);
			ret.back().form = subForm;
		}
	}
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

template<class _multimap, class _key, class _value, class _comp>
void emplaceMaxCnt(_multimap& dest, _key key, _value value, size_t maxCnt, _comp comparator)
{
	auto itp = dest.equal_range(key);
	if (std::distance(itp.first, itp.second) < maxCnt)
	{
		dest.emplace(key, value);
	}
	else
	{
		auto itm = itp.first;
		++itp.first;
		for (; itp.first != itp.second; ++itp.first)
		{
			if (comparator(itp.first->second, itm->second))
			{
				itm = itp.first;
			}
		}
		if (comparator(itm->second, value)) itm->second = value;
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
typedef vector<pair<MInfos, float>, pool_allocator<pair<MInfos, float>>> WordLLs;

template<class _Type>
auto evalTrigram(const KNLangModel * knlm, const KMorpheme* morphBase, const pair<MInfos, float>** wBegin, const pair<MInfos, float>** wEnd, 
	array<WID, 5> seq, size_t chSize, const KMorpheme* curMorph, const KGraphNode* node, uint8_t combSocket, _Type maxWidLL)
{
	for (; wBegin != wEnd; ++wBegin)
	{
		const auto* wids = &(*wBegin)->first;
		float candScore = (*wBegin)->second;
		seq[chSize] = wids->back().wid;
		if (wids->back().combineSocket)
		{
			// always merge <V> <chunk> with the same socket
			if (wids->back().combineSocket != curMorph->combineSocket || curMorph->chunks)
			{
				continue;
			}
			seq[0] = curMorph->getCombined() - morphBase;
			seq[1] = (wids->end() - 2)->wid;
			seq[2] = (wids->end() - 3)->wid;
		}
		else if (curMorph->combineSocket && !curMorph->chunks)
		{
			continue;
		}
		else if (wids->size() + chSize > 2)
		{
			if (wids->size() > 1) seq[chSize + 1] = (wids->end() - 2)->wid;
		}


		if (!KFeatureTestor::isMatched(node->uform.empty() ? curMorph->kform : &node->uform, wids->back().condVowel, wids->back().condPolar))
		{
			continue;
		}

		auto orgSeq = seq[chSize + 1];
		if (wids->size() + chSize > 2)
		{
			if (seq[chSize + 1] >= knlm->getVocabSize()) seq[chSize + 1] = (size_t)morphBase[seq[chSize + 1]].tag + 1;
			if (seq[chSize + 2] >= knlm->getVocabSize()) seq[chSize + 2] = (size_t)morphBase[seq[chSize + 2]].tag + 1;
			for (size_t ch = 0; ch < chSize - (wids->size() == 1 ? 1 : 0); ++ch)
			{
				if (ch == 0 && combSocket) continue;
				float ct;
				candScore += ct = knlm->evaluateLL(&seq[ch], 3);
#ifdef DEBUG_PRINT
				if (ct <= -100)
				{
					cout << knlm->evaluateLL(&seq[ch], 3);
					cout << "@Warn\t";
					cout << seq[ch] << ' ' << morphBase[seq[ch]] << '\t';
					cout << seq[ch + 1] << ' ' << morphBase[seq[ch + 1]] << '\t';
					cout << seq[ch + 2] << ' ' << morphBase[seq[ch + 2]] << '\t';
					cout << endl;
				}
#endif
			}
		}
		else
		{
			candScore = 0;
		}

		emplaceMaxCnt(maxWidLL, orgSeq, make_pair(wids, candScore), 5, [](const auto& a, const auto& b) { return a.second < b.second; });
	}
	return move(maxWidLL);
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
			array<WID, 5> seq = { 0, };
			size_t combSocket = 0;
			KCondVowel condV = KCondVowel::none;
			KCondPolarity condP = KCondPolarity::none;
			size_t chSize = 1;
			WID orgSeq = 0;
			bool isUserWord = false;
			// if the morpheme is chunk set
			if (curMorph->chunks)
			{
				chSize = curMorph->chunks->size();
				for (size_t i = 0; i < chSize; ++i)
				{
					seq[i] = (*curMorph->chunks)[i] - morphBase;
				}
				combSocket = curMorph->combineSocket;
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
			}
			orgSeq = seq[0];
			condV = curMorph->vowel;
			condP = curMorph->polar;

			unordered_multimap<WID, pair<const MInfos*, float>, hash<WID>, equal_to<WID>, pool_allocator<void*>> maxWidLL;
			vector<const pair<MInfos, float>*, pool_allocator<void*>> works;
			for (size_t i = 0; i < KGraphNode::MAX_NEXT; ++i)
			{
				auto* next = node->getNext(i);
				if (!next) break;
				works.reserve(works.size() + cache[next - startNode].size());
				for (auto& p : cache[next - startNode])
				{
					works.emplace_back(&p);
				}
			}

			/*
			if (workers.size() > 1 && works.size() >= 256)
			{
				size_t numWorking = min(works.size() / 64, workers.size());
				vector<future<unordered_multimap<WID, pair<const MInfos*, float>>>> futures(numWorking);
				for (size_t id = 0; id < numWorking; ++id)
				{
					size_t beginId = works.size() * id / numWorking;
					size_t endId = works.size() * (id + 1) / numWorking;
					futures[id] = workers[id].setWork(evalTrigram<unordered_multimap<WID, pair<const MInfos*, float>>>, knlm, morphBase, &works[0] + beginId, &works[0] + endId, seq, chSize, curMorph, node, combSocket, unordered_multimap<WID, pair<const MInfos*, float>>{});
				}

				for (auto&& f : futures)
				{
					auto res = f.get();
					for (auto& r : res)
					{
						emplaceMaxCnt(maxWidLL, r.first, r.second, 5, [](const auto& a, const auto& b) { return a.second < b.second; });
					}
				}
			}
			else
			*/
			{
				maxWidLL = evalTrigram(knlm, morphBase, &works[0], &works[0] + works.size(), seq, chSize, curMorph, node, combSocket, move(maxWidLL));
			}

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
				estimatedLL -= 20;
			}

			for (auto& p : maxWidLL)
			{
				p.second.second += estimatedLL;
				tMax = max(tMax, p.second.second);
			}

			auto& nCache = cache[i];
			for (auto& p : maxWidLL)
			{
				if (p.second.second <= tMax - cutOffThreshold) continue;
				nCache.emplace_back(MInfos{}, p.second.second);
				auto& wids = nCache.back().first;
				wids.reserve(p.second.first->size() + chSize);
				wids = *p.second.first;
				if (!curMorph->combineSocket || curMorph->chunks)
				{
					size_t lastPos = node->lastPos;
					for (size_t ch = chSize; ch-- > 0;)
					{
						if (ch) wids.emplace_back(seq[ch], 0, KCondVowel::none, KCondPolarity::none, 0, lastPos);
						else wids.emplace_back(isUserWord ? curMorph - morphBase : seq[ch], combSocket, condV, condP, ownFormId, lastPos);
						lastPos -= morphBase[seq[ch]].kform->size() - 1;
					}
				}
				else
				{
					wids.back() = MInfo{ (WID)(curMorph->getCombined() - morphBase), 0, KCondVowel::none, KCondPolarity::none, 0, wids.back().lastPos };
				}
			}
		}
		return tMax;
	};

	// end node
	cache.back().emplace_back(MInfos{ MInfo(1u) }, 0.f);

	// middle nodes
	for (size_t i = graph.size() - 2; i > 0; --i)
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
			if (c.second > tMax - cutOffThreshold) reduced.emplace_back(move(c));
		}
		cache[i] = move(reduced);

		/*
		sort(cache[i].begin(), cache[i].end(), [](const pair<MInfos, float>& a, const pair<MInfos, float>& b)
		{
		return a.second > b.second;
		});
		size_t remainCnt = max((node->form ? node->form->candidate.size() : 2u) * topN * 2, (size_t)10);
		if (remainCnt < cache[i].size()) cache[i].erase(cache[i].begin() + remainCnt, cache[i].end());
		*/
#ifdef DEBUG_PRINT
		for (auto& tt : cache[i])
		{
			cout << tt.second << '\t';
			for (auto it = tt.first.rbegin(); it != tt.first.rend(); ++it)
			{
				cout << morphBase[it->wid] << '\t';
			}
			cout << endl;
		}
		cout << "========" << endl;
#endif
	}

	// start node
	WID seq[3] = { 0, };
	for (size_t i = 0; i < KGraphNode::MAX_NEXT; ++i)
	{
		auto* next = startNode->getNext(i);
		if (!next) break;
		for (auto&& p : cache[next - startNode])
		{
			if (p.first.back().combineSocket) continue;
			if (!KFeatureTestor::isMatched(nullptr, p.first.back().condVowel)) continue;
			seq[1] = p.first.back().wid;
			seq[2] = (p.first.end() - 2)->wid;

			if (seq[1] >= knlm->getVocabSize()) seq[1] = (size_t)morphBase[seq[1]].tag + 1;
			if (seq[2] >= knlm->getVocabSize()) seq[2] = (size_t)morphBase[seq[2]].tag + 1;
			float ct;
			float c = (ct = knlm->evaluateLL(seq, 3) + knlm->evaluateLL(seq, 2)) + p.second;
#ifdef DEBUG_PRINT
			if (ct <= -100)
			{
				cout << "@Warn\t";
				cout << seq[0] << ' ' << morphBase[seq[0]] << '\t';
				cout << seq[1] << ' ' << morphBase[seq[1]] << '\t';
				cout << seq[2] << ' ' << morphBase[seq[2]] << '\t';
				cout << endl;
			}
#endif
			cache[0].emplace_back(p.first, c);
		}
	}

	auto& cand = cache[0];
	sort(cand.begin(), cand.end(), [](const pair<MInfos, float>& a, const pair<MInfos, float>& b) { return a.second > b.second; });

#ifdef DEBUG_PRINT
	for (auto& tt : cache[0])
	{
		cout << tt.second << '\t';
		for (auto it = tt.first.rbegin(); it != tt.first.rend(); ++it)
		{
			cout << morphBase[it->wid] << '\t';
		}
		cout << endl;
	}
	cout << "========" << endl;

#endif

	vector<pair<path, float>> ret;
	for (size_t i = 0; i < min(topN, cand.size()); ++i)
	{
		path mv(cand[i].first.size() - 1);
		transform(cand[i].first.rbegin(), cand[i].first.rend() - 1, mv.begin(), [morphBase, &ownFormList](const MInfo& m)
		{
			if (m.ownFormId)	return make_tuple(morphBase + m.wid, ownFormList[m.ownFormId - 1], m.lastPos);
			else return make_tuple(morphBase + m.wid, k_string{}, m.lastPos);
		});
		ret.emplace_back(mv, cand[i].second);
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
	vector<future<void>> futures;
	struct
	{
		map<size_t, vector<KResult>> res;
		mutex resMutex;
		size_t outputId = 0;

		void addResult(size_t id, vector<KResult>&& r, const function<void(size_t, vector<KResult>&&)>& receiver)
		{
			lock_guard<mutex> l(resMutex);
			if (id == outputId) receiver(outputId++, move(r));
			else
			{
				res.emplace(id, r);
			}

			while (!res.empty() && res.begin()->first == outputId)
			{
				receiver(outputId++, move(res.begin()->second));
				res.erase(res.begin());
			}
		}
	} sharedResult;

	for (size_t id = 0; ; ++id)
	{
		auto ustr = reader(id);
		if (ustr.empty()) break;
		if (workers.getNumWorkers() > 1)
		{
			futures.emplace_back(workers.enqueue([this, topN, id, ustr, &receiver, &sharedResult](size_t tid)
			{
				auto r = analyze(ustr, topN);
				sharedResult.addResult(id, move(r), receiver);
			}));
		}
		else
		{
			auto r = analyze(ustr, topN);
			receiver(id, move(r));
		}
	}

	if (workers.getNumWorkers() > 1)
	{
		for (auto& f : futures) f.get();
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
	for (auto i = 0; i < nstr.size(); ++i)
	{
		posMap[i + 1] = posMap[i] + (isHangulCoda(nstr[i]) ? 0 : 1);
	}

	auto nodes = mdl->getTrie()->split(nstr);
	vector<KResult> ret;
	if (nodes.size() <= 2) return ret;
	auto res = findBestPath(nodes, mdl->getLangModel(), mdl->getMorphemes(), topN);
	for (auto&& r : res)
	{
		vector<KWordPair> rarr;
		for (auto&& s : r.first)
		{
			rarr.emplace_back(joinHangul(get<1>(s).empty() ? *get<0>(s)->kform : get<1>(s)), get<0>(s)->tag, 0, 0);
			size_t nlen = (get<1>(s).empty() ? *get<0>(s)->kform : get<1>(s)).size();
			size_t nlast = get<2>(s);
			rarr.back().pos() = posMap[nlast - nlen];
			rarr.back().len() = posMap[nlast] - posMap[nlast - nlen];
		}
		ret.emplace_back(rarr, r.second);
	}
	if (ret.empty()) ret.emplace_back();
	return ret;
}

vector<KResult> Kiwi::analyze(const u16string & str, size_t topN) const
{
	auto chunk = str.begin();
	vector<u16string::const_iterator> sents;
	sents.emplace_back(chunk);
	while (1)
	{
		KPOSTag tag = identifySpecialChr(*chunk);
		while (chunk != str.end() && !(tag == KPOSTag::SF || tag == KPOSTag::SS || tag == KPOSTag::SE || tag == KPOSTag::SW || *chunk == u':'))
		{
			++chunk;
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
	sents.emplace_back(str.end());
	vector<KResult> ret = analyzeSent(sents[0], sents[1], topN);
	if (ret.empty())
	{
		ret.emplace_back();
		return ret;
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
	return 50;
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
