#include "stdafx.h"
#include "KWordDetector.h"
#include "Utils.h"

using namespace std;

void KWordDetector::countUnigram(Counter& cdata, const function<u16string(size_t)>& reader) const
{
	auto ldUnigram = readProc<vector<uint32_t>>(reader, [this, &cdata](u16string ustr, size_t id, vector<uint32_t>& ld)
	{
		basic_stringstream<char16_t> ss{ ustr };
		istream_iterator<u16string, char16_t> begin{ ss }, end{};
		vector<int16_t> ids;
		vector<size_t> chk;
		chk.emplace_back(0);
		cdata.chrDict.withLock([&]()
		{
			for (; begin != end; ++begin)
			{
				auto r = cdata.chrDict.getOrAddsWithoutLock(begin->begin(), begin->end());
				ids.insert(ids.end(), r.begin(), r.end());
				chk.emplace_back(ids.size());
			}
		});
		if (ids.empty()) return;
		int16_t maxId = *max_element(ids.begin(), ids.end());
		if (ld.size() <= maxId) ld.resize(maxId + 1);
		for (auto id : ids) ld[id]++;
	});

	int16_t maxId = 0;
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

	WordDictionary<char16_t, int16_t> chrDictShrink;
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
		basic_stringstream<char16_t> ss{ ustr };
		istream_iterator<u16string, char16_t> begin{ ss }, end{};
		for (; begin != end; ++begin)
		{
			int16_t a = 1;
			for (auto c : *begin)
			{
				int16_t id = max(cdata.chrDict.get(c), (int16_t)0);
				if (a && id) ++ld[a * cdata.chrDict.size() + id];
				a = id;
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
	vector<ReusableThread> rt(2);
	vector<future<void>> futures(2);
	for (size_t id = 0; ; ++id)
	{
		auto ustr = reader(id);
		if (ustr.empty()) break;
		basic_stringstream<char16_t> ss{ ustr };
		istream_iterator<u16string, char16_t> begin{ ss }, end{};
		auto idss = make_shared<vector<vector<int16_t, pool_allocator<void*>>, pool_allocator<void*>>>();
		for (; begin != end; ++begin)
		{
			vector<int16_t, pool_allocator<void*>> ids;
			ids.reserve(begin->size() + 2);
			ids.emplace_back(1);
			for (auto c : *begin)
			{
				int16_t id = max(cdata.chrDict.get(c), (int16_t)0);
				ids.emplace_back(id);
			}
			ids.emplace_back(2);
			idss->emplace_back(move(ids));
		}

		if (futures[0].valid()) futures[0].get();
		futures[0] = rt[0].setWork([&, idss]()
		{
			for (auto& ids : *idss)
			for (size_t i = 1; i < ids.size(); ++i)
			{
				if (!ids[i]) continue;
				for (size_t j = i + 2; j < min(i + 1 + maxWordLen, ids.size() + 1); ++j)
				{
					if (!ids[j - 1]) break;
					++cdata.forwardCnt[{ids.begin() + i, ids.begin() + j}];
					if (!cdata.candBigram.count(make_pair(ids[j - 2], ids[j - 1]))) break;
				}
			}
		});
			
		if (futures[1].valid()) futures[1].get();
		futures[1] = rt[1].setWork([&, idss]()
		{
			for (auto& ids : *idss)
			for (size_t i = 1; i < ids.size(); ++i)
			{
				if (!ids.rbegin()[i]) continue;
				for (size_t j = i + 2; j < min(i + 1 + maxWordLen, ids.size() + 1); ++j)
				{
					if (!ids.rbegin()[j - 1]) break;
					++cdata.backwardCnt[{ids.rbegin() + i, ids.rbegin() + j}];
					if (!cdata.candBigram.count(make_pair(ids.rbegin()[j - 1], ids.rbegin()[j - 2]))) break;
				}
			}
		});
	}

	for (auto& f : futures)
	{
		if (f.valid()) f.get();
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

float KWordDetector::branchingEntropy(const map<u16light, uint32_t>& cnt, map<u16light, uint32_t>::iterator it) const
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
		if (it->first.back() < 3)
		{
			entropy -= p * log(p / 4);
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

map<KPOSTag, float> KWordDetector::inverseKLDivergenceFromPOS(Counter& cdata, const map<u16light, uint32_t>& cnt, map<u16light, uint32_t>::iterator it, bool coda) const
{
	map<KPOSTag, float> ret;
	u16light endKey = it->first;
	float tot = it->second;
	size_t len = endKey.size();
	endKey.back()++;
	++it;
	auto eit = cnt.lower_bound(endKey);
	map<char16_t, float> rParts;
	for (; it != eit; ++it)
	{
		if (it->first.size() != len + 1) continue;
		rParts[cdata.chrDict.getStr(it->first.back())] = it->second;
	}

	for (auto& posd : posDistribution)
	{
		if (posd.first.second != coda) continue;
		float smoothing = 10.f / posd.second.size();
		for (auto& dist : posd.second)
		{
			auto qit = rParts.find(dist.first);
			float q = (qit != rParts.end() ? qit->second + smoothing : smoothing) / (tot + 10);
			ret[posd.first.first] += dist.second * log(dist.second / q);
		}
	}
	
	for (auto& p : ret)
	{
		p.second = 1 / p.second;
	}
	return ret;
}

map<KPOSTag, float> KWordDetector::getPosScore(Counter & cdata, const std::map<u16light, uint32_t>& cnt, std::map<u16light, uint32_t>::iterator it, bool coda) const
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
	return ret;
}

void KWordDetector::loadPOSModelFromTxt(std::istream & is)
{
	/*
	string line;
	while (getline(is, line))
	{
		auto fields = split(utf8_to_utf16(line), u'\t');
		if (fields.size() < 4) continue;
		KPOSTag pos = makePOSTag(fields[0]);
		bool coda = !!stof(fields[1].begin(), fields[1].end());
		char16_t chr = fields[2][0];
		float p = stof(fields[3].begin(), fields[3].end());
		posDistribution[make_pair(pos, coda)][chr] = p;
	}
	*/
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

vector<KWordDetector::WordInfo> KWordDetector::extractWords(const std::function<std::u16string(size_t)>& reader) const
{
	Counter cdata;
	countUnigram(cdata, reader);
	countBigram(cdata, reader);
	countNgram(cdata, reader);

	vector<WordInfo> ret;
	for (auto it = cdata.forwardCnt.begin(); it != cdata.forwardCnt.end(); ++it)
	{
		auto& p = *it;
		if (p.second < minCnt) continue;
		auto bit = cdata.backwardCnt.find({ p.first.rbegin(), p.first.rend() });
		if (bit == cdata.backwardCnt.end()) continue;

		float forwardCohesion = pow(p.second / (float)cdata.cntUnigram[p.first.front()], 1 / (p.first.size() - 1.f));
		float backwardCohesion = pow(p.second / (float)cdata.cntUnigram[p.first.back()], 1 / (p.first.size() - 1.f));

		float forwardBranch = branchingEntropy(cdata.forwardCnt, it);
		float backwardBranch = branchingEntropy(cdata.backwardCnt, bit);

		float score = forwardCohesion * backwardCohesion;
		score *= forwardBranch * backwardBranch;
		if (score < minScore) continue;
		u16string form;
		form.reserve(p.first.size());
		transform(p.first.begin(), p.first.end(), back_inserter(form), [this, &cdata](char16_t c) { return cdata.chrDict.getStr(c); });

		bool hasCoda = 0xAC00 <= p.first.back() && p.first.back() <= 0xD7A4 && (p.first.back() - 0xAC00) % 28;
		ret.emplace_back(form, score, backwardBranch, forwardBranch, backwardCohesion, forwardCohesion,
			p.second, getPosScore(cdata, cdata.forwardCnt, it, hasCoda));
	}

	sort(ret.begin(), ret.end(), [](const auto& a, const auto& b)
	{
		return a.score > b.score;
	});
	return ret;
}