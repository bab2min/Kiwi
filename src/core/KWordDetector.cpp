#include "KiwiHeader.h"
#include "KWordDetector.h"
#include "Utils.h"
#include "serializer.hpp"
#include "Trie.hpp"

using namespace std;
using namespace kiwi;

void KWordDetector::countUnigram(Counter& cdata, const function<u16string(size_t)>& reader) const
{
	auto ldUnigram = readProc<vector<uint32_t>>(reader, [this, &cdata](u16string ustr, size_t id, vector<uint32_t>& ld)
	{
		SpaceSplitIterator begin{ ustr.begin(), ustr.end() }, end;
		vector<uint16_t> ids;
		vector<size_t> chk;
		chk.emplace_back(0);
		cdata.chrDict.withLock([&]()
		{
			for (; begin != end; ++begin)
			{
				auto r = cdata.chrDict.getOrAddsWithoutLock(begin.strBegin(), begin.strEnd());
				ids.insert(ids.end(), r.begin(), r.end());
				chk.emplace_back(ids.size());
			}
		});
		if (ids.empty()) return;
		uint16_t maxId = *max_element(ids.begin(), ids.end());
		if (ld.size() <= maxId) ld.resize(maxId + 1);
		for (auto id : ids) ld[id]++;
	});

	uint16_t maxId = 0;
	for (auto& t : ldUnigram)
	{
		if (maxId < t.size()) maxId = t.size();
	}
	auto& unigramMerged = ldUnigram[0];
	unigramMerged.resize(maxId);
	for (size_t i = 1; i < ldUnigram.size(); ++i)
	{
		for (size_t n = 0; n < ldUnigram[i].size(); ++n) unigramMerged[n] += ldUnigram[i][n];
	}

	WordDictionary<char16_t, uint16_t> chrDictShrink;
	chrDictShrink.add(0);
	chrDictShrink.add(1);
	chrDictShrink.add(2);
	cdata.cntUnigram.resize(3);
	for (size_t i = 0; i < maxId; ++i)
	{
		if (unigramMerged[i] < minCnt) continue;
		chrDictShrink.add(cdata.chrDict.getStr(i));
		cdata.cntUnigram.emplace_back(unigramMerged[i]);
	}
	cdata.chrDict = move(chrDictShrink);
}

void KWordDetector::countBigram(Counter& cdata, const function<u16string(size_t)>& reader) const
{
	auto ldBigram = readProc<vector<uint32_t>>(reader, [this, &cdata](u16string ustr, size_t id, vector<uint32_t>& ld)
	{
		SpaceSplitIterator begin{ ustr.begin(), ustr.end() }, end;
		for (; begin != end; ++begin)
		{
			uint16_t a = 1;
			for (auto c : *begin)
			{
				uint16_t wid = cdata.chrDict.get(c);
				if (wid == cdata.chrDict.npos) wid = 0;
				if (a && wid) ++ld[a * cdata.chrDict.size() + wid];
				a = wid;
			}
			if (a) ++ld[a * cdata.chrDict.size() + 2];
		}
	}, vector<uint32_t>(cdata.chrDict.size() * cdata.chrDict.size()));

	auto& bigramMerged = ldBigram[0];
	for (size_t i = 1; i < ldBigram.size(); ++i)
	{
		for (size_t n = 0; n < ldBigram[i].size(); ++n) bigramMerged[n] += ldBigram[i][n];
	}

	for (size_t i = 0; i < bigramMerged.size(); ++i)
	{
		if (bigramMerged[i] < minCnt) continue;
		cdata.candBigram.emplace(i / cdata.chrDict.size(), i % cdata.chrDict.size());
	}
}

void KWordDetector::countNgram(Counter& cdata, const function<u16string(size_t)>& reader) const
{
	{
		ThreadPool rtForward{ 1, 2 }, rtBackward{ 1, 2 };
		vector<future<void>> futures;
		for (size_t id = 0; ; ++id)
		{
			auto ustr = reader(id);
			if (ustr.empty()) break;
			SpaceSplitIterator begin{ ustr.begin(), ustr.end() }, end;
			auto idss = make_shared<vector<vector<int16_t, pool_allocator<int16_t>>, pool_allocator<vector<int16_t, pool_allocator<int16_t>>>>>();
			for (; begin != end; ++begin)
			{
				vector<int16_t, pool_allocator<int16_t>> ids;
				ids.reserve(begin.strSize() + 2);
				ids.emplace_back(1); // Begin Chr
				for (auto c : *begin)
				{
					uint16_t id = cdata.chrDict.get(c);
					if (id == cdata.chrDict.npos) id = 0;
					ids.emplace_back(id);
				}
				ids.emplace_back(2); // End Chr
				idss->emplace_back(move(ids));
			}

			rtForward.enqueue([&, idss](size_t threadId)
			{
				for (auto& ids : *idss)
				{
					for (size_t i = 1; i < ids.size(); ++i)
					{
						if (!ids[i]) continue; // skip unknown chr
						for (size_t j = i + 1; j < min(i + 1 + maxWordLen, ids.size() + 1); ++j)
						{
							if (!ids[j - 1]) break;
							++cdata.forwardCnt[{ids.begin() + i, ids.begin() + j}];
							if (!cdata.candBigram.count(make_pair(ids[j - 2], ids[j - 1]))) break;
						}
					}
				}
			});

			rtBackward.enqueue([&, idss](size_t threadId)
			{
				for (auto& ids : *idss)
				{
					for (size_t i = 1; i < ids.size(); ++i)
					{
						if (!ids.rbegin()[i]) continue; // skip unknown chr
						for (size_t j = i + 1; j < min(i + 1 + maxWordLen, ids.size() + 1); ++j)
						{
							if (!ids.rbegin()[j - 1]) break;
							++cdata.backwardCnt[{ids.rbegin() + i, ids.rbegin() + j}];
							if (!cdata.candBigram.count(make_pair(ids.rbegin()[j - 1], ids.rbegin()[j - 2]))) break;
						}
					}
				}
			});
		}

	}
	
	u16light prefixToErase = {};
	for (auto it  = cdata.forwardCnt.cbegin(); it != cdata.forwardCnt.cend();)
	{
		auto& p = *it;
		if (prefixToErase.empty() || !p.first.startsWith(prefixToErase))
		{
			if (p.second < minCnt) prefixToErase = p.first;
			else prefixToErase = {};
			++it;
		}
		else
		{
			it = cdata.forwardCnt.erase(it);
		}
	}

	prefixToErase = {};
	for (auto it = cdata.backwardCnt.cbegin(); it != cdata.backwardCnt.cend();)
	{
		auto& p = *it;
		if (prefixToErase.empty() || !p.first.startsWith(prefixToErase))
		{
			if (p.second < minCnt) prefixToErase = p.first;
			else prefixToErase = {};
			++it;
		}
		else
		{
			it = cdata.backwardCnt.erase(it);
		}
	}
}

float KWordDetector::branchingEntropy(const map<u16light, uint32_t>& cnt, map<u16light, uint32_t>::iterator it, float defaultPerp) const
{
	u16light endKey = it->first;
	float tot = it->second;
	size_t len = endKey.size();
	endKey.back()++;
	++it;
	auto eit = cnt.lower_bound(endKey);
	size_t sum = 0;
	float entropy = 0;
	for (; it != eit; ++it)
	{
		if (it->first.size() != len + 1) continue;
		sum += it->second;
		float p = it->second / tot;
		// for begin, end, unknown
		if (it->first.back() < 3)
		{
			entropy -= p * log(p / defaultPerp);
		}
		else
		{
			entropy -= p * log(p);
		}
	}

	if (sum < tot)
	{
		float p = (tot - sum) / tot;
		entropy -= p * log(p / ((tot - sum) / minCnt));
	}

	return entropy;
}

map<KPOSTag, float> KWordDetector::getPosScore(Counter & cdata, const std::map<u16light, uint32_t>& cnt, std::map<u16light, uint32_t>::iterator it
	, bool coda, const u16string& realForm) const
{
	map<KPOSTag, float> ret;
	u16light endKey = it->first;
	float tot = it->second;
	size_t len = endKey.size();
	endKey.back()++;
	++it;
	auto eit = cnt.lower_bound(endKey);
	map<char16_t, float> rParts;
	float sum = 0;
	for (; it != eit; ++it)
	{
		if (it->first.size() != len + 1) continue;
		sum += (rParts[it->first.back() > 2 ? cdata.chrDict.getStr(it->first.back()) : u'$'] = it->second);
	}

	for (auto& posd : posScore)
	{
		if (posd.first.second != coda) continue;
		for (auto& dist : posd.second)
		{
			auto qit = rParts.find(dist.first);
			if (qit == rParts.end()) continue;
			ret[posd.first.first] += dist.second * qit->second / tot;
		}
	}

	for (auto& nount : nounTailScore)
	{
		if (realForm.size() < nount.first.size()) continue;
		if (equal(realForm.end() - nount.first.size(), realForm.end(), nount.first.begin()))
		{
			ret[KPOSTag::NNP] += nount.second * .25f;
			break;
		}
	}
	return ret;
}

void KWordDetector::loadPOSModelFromTxt(std::istream & is)
{
	string line;
	while (getline(is, line))
	{
		auto fields = split(utf8_to_utf16(line), u'\t');
		if (fields.size() < 4) continue;
		KPOSTag pos = makePOSTag(fields[0]);
		bool coda = !!stof(fields[1].begin(), fields[1].end());
		char16_t chr = fields[2][0];
		float p = stof(fields[3].begin(), fields[3].end());
		posScore[make_pair(pos, coda)][chr] = p;
	}
}

void KWordDetector::loadNounTailModelFromTxt(std::istream & is)
{
	string line;
	while (getline(is, line))
	{
		auto fields = split(utf8_to_utf16(line), u'\t');
		if (fields.size() < 4) continue;
		float p = stof(fields[1].begin(), fields[1].end());
		nounTailScore[fields[0]] = p;
	}
}

void KWordDetector::savePOSModel(std::ostream & os)
{
	serializer::writeToBinStream(os, posScore);
}

void KWordDetector::saveNounTailModel(std::ostream & os)
{
	serializer::writeToBinStream(os, nounTailScore);
}

void KWordDetector::loadPOSModel(std::istream & is)
{
	serializer::readFromBinStream(is, posScore);
}

void KWordDetector::loadNounTailModel(std::istream & is)
{
	serializer::readFromBinStream(is, nounTailScore);
}

vector<KWordDetector::WordInfo> KWordDetector::extractWords(const function<u16string(size_t)>& reader) const
{
	Counter cdata;
	countUnigram(cdata, reader);
	countBigram(cdata, reader);
	countNgram(cdata, reader);

	vector<WordInfo> cands, ret;
	for (auto it = cdata.forwardCnt.begin(); it != cdata.forwardCnt.end(); ++it)
	{
		auto& p = *it;
		if (p.second < minCnt) continue;
		if (p.first.size() >= maxWordLen || p.first.size() <= 1) continue;
		auto bit = cdata.backwardCnt.find({ p.first.rbegin(), p.first.rend() });
		if (bit == cdata.backwardCnt.end()) continue;
		
		float forwardCohesion = p.second / (float)cdata.cntUnigram[p.first.front()];
		float backwardCohesion = p.second / (float)cdata.cntUnigram[p.first.back()];
		if (p.first.size() == 3)
		{
			forwardCohesion *= p.second / (float)cdata.forwardCnt.find({p.first.begin(), p.first.begin() + 2})->second;
			backwardCohesion *= p.second / (float)cdata.backwardCnt.find({ p.first.rbegin(), p.first.rbegin() + 2 })->second;
			forwardCohesion = pow(forwardCohesion, 1.f / (p.first.size() * 2 - 3));
			backwardCohesion = pow(backwardCohesion, 1.f / (p.first.size() * 2 - 3));
		}
		else if (p.first.size() > 3)
		{
			forwardCohesion *= p.second / (float)cdata.forwardCnt.find({ p.first.begin(), p.first.begin() + 2 })->second;
			backwardCohesion *= p.second / (float)cdata.backwardCnt.find({ p.first.rbegin(), p.first.rbegin() + 2 })->second;
			forwardCohesion *= p.second / (float)cdata.forwardCnt.find({ p.first.begin(), p.first.begin() + 3 })->second;
			backwardCohesion *= p.second / (float)cdata.backwardCnt.find({ p.first.rbegin(), p.first.rbegin() + 3 })->second;
			forwardCohesion = pow(forwardCohesion, 1.f / (p.first.size() * 3 - 6));
			backwardCohesion = pow(backwardCohesion, 1.f / (p.first.size() * 3 - 6));
		}
		
		float forwardBranch = branchingEntropy(cdata.forwardCnt, it, 6);
		float backwardBranch = branchingEntropy(cdata.backwardCnt, bit, 6);

		float score = forwardCohesion * backwardCohesion * forwardBranch * backwardBranch;
		if (score < minScore) continue;
		u16string form;
		form.reserve(p.first.size());
		transform(p.first.begin(), p.first.end(), back_inserter(form), [this, &cdata](char16_t c) { return cdata.chrDict.getStr(c); });

		bool hasCoda = 0xAC00 <= p.first.back() && p.first.back() <= 0xD7A4 && (p.first.back() - 0xAC00) % 28;
		cands.emplace_back(form, score, backwardBranch, forwardBranch, backwardCohesion, forwardCohesion,
			p.second, getPosScore(cdata, cdata.forwardCnt, it, hasCoda, form));
	}

	map<u16string, float> rPartEntropy;
	for (auto bit = cdata.backwardCnt.begin(); bit != cdata.backwardCnt.end(); ++bit)
	{
		auto& p = *bit;
		if (p.second < minCnt) continue;
		if (p.first.size() > 3 || p.first.size() < 1) continue;
		float r = branchingEntropy(cdata.backwardCnt, bit);
		if (r >= 1)
		{
			u16string form;
			form.reserve(p.first.size());
			transform(p.first.begin(), p.first.end(), back_inserter(form), [this, &cdata](char16_t c) { return cdata.chrDict.getStr(c); });
			if (any_of(form.begin(), form.end(), [](char16_t c) { return isalnum(c); })) continue;
			rPartEntropy[form] = r;
		}
	}

	sort(cands.begin(), cands.end(), [](const WordInfo& a, const WordInfo& b)
	{
		return a.score > b.score;
	});

	vector<Trie<char16_t, uint32_t, OverriddenMap<map<char16_t, int32_t>>>> trieNodes(1), trieBackNodes(1);
	const auto& addNode = [&]()
	{
		trieNodes.emplace_back();
		return &trieNodes.back();
	};
	const auto& addBackNode = [&]()
	{
		trieBackNodes.emplace_back();
		return &trieBackNodes.back();
	};

	for (auto& r : cands)
	{
		/*
		removing unpaired surrogate
		*/
		if ((r.form.back() & 0xFC00) == 0xD800)
		{
			r.form.pop_back();
		}

		/*
		removing hyper-matched forms
		ex) correct form: ABC, matched forms: ABC, ABC_D, ABC_E ...
		remove ABC_D, ABC_E ... if branching entropy of D, E ... is higher
		*/
		auto pm = trieNodes[0].findMaximumMatch(r.form.begin(), r.form.end());
		if (pm.first && pm.second)
		{
			auto& pr = ret[*pm.first - 1];
			if (r.form.size() <= pr.form.size() + 3)
			{
				float rEntropy = 1;
				auto rpit = rPartEntropy.find({ r.form.rbegin(), r.form.rbegin() + r.form.size() - pr.form.size() });
				if (rpit != rPartEntropy.end()) rEntropy = max(rpit->second, rEntropy);
				if (rEntropy > 2.5f && r.freq < ((rEntropy - 1) / rEntropy) * pr.freq)
				{
					//cerr << utf16_to_utf8(r.form) << '\t' << r.freq << '\t' << utf16_to_utf8(pr.form) << '\t' << pr.freq << '\t' << rEntropy << endl;
					continue;
				}
			}
		}

		/*
		removing hypo-matched forms
		ex) correct forms: ABC_D, ABC_E ..., matched form: ABC_D, ABC_E, ... , ABC
		remove ABC if total number of ABC is nearly same as total number of ABC_D, ABC_E ...
		*/
		auto tr = trieNodes[0].findNode(r.form.begin(), r.form.end());
		if (tr)
		{
			vector<uint32_t> childFreqs;
			tr->traverse([&](uint32_t f)
			{
				auto& pr = ret[f - 1];
				childFreqs.emplace_back(pr.freq);
				return true;
			});
			size_t tot = accumulate(childFreqs.begin(), childFreqs.end(), 0);
			float entropy = 0;
			for (auto c : childFreqs)
			{
				float p = c / (float)tot;
				entropy -= p * log(p);
			}
			if (entropy < 1.5 && (tot + 25) > (r.freq + 25) * 0.9)
			{
				//cerr << utf16_to_utf8(r.form) << endl;
				continue;
			}
		}

		auto tbr = trieBackNodes[0].findNode(r.form.rbegin(), r.form.rend());
		if (tbr)
		{
			vector<uint32_t> childFreqs;
			tbr->traverse([&](uint32_t f)
			{
				auto& pr = ret[f - 1];
				childFreqs.emplace_back(pr.freq);
				return true;
			});
			size_t tot = accumulate(childFreqs.begin(), childFreqs.end(), 0);
			float entropy = 0;
			for (auto c : childFreqs)
			{
				float p = c / (float)tot;
				entropy -= p * log(p);
			}
			if (entropy < 1.5 && (tot + 25) >(r.freq + 25) * 0.9)
			{
				//cerr << utf16_to_utf8(r.form) << endl;
				continue;
			}
		}

		if (trieNodes.capacity() < trieNodes.size() + r.form.size())
		{
			trieNodes.reserve(max(trieNodes.capacity() * 2, trieNodes.size() + r.form.size()));
		}
		trieNodes[0].build(r.form.data(), r.form.size(), ret.size() + 1, addNode);

		if (trieBackNodes.capacity() < trieBackNodes.size() + r.form.size())
		{
			trieBackNodes.reserve(max(trieBackNodes.capacity() * 2, trieBackNodes.size() + r.form.size()));
		}
		vector<char16_t> revForm{ r.form.rbegin(), r.form.rend() };
		trieBackNodes[0].build(revForm.data(), r.form.size(), ret.size() + 1, addBackNode);
		ret.emplace_back(move(r));
	}

	return ret;
}
