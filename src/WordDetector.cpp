#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <mutex>
#include <unordered_set>
#include <algorithm>

#include <kiwi/Types.h>
#include <kiwi/WordDetector.h>
#include <kiwi/Trie.hpp>
#include <kiwi/ThreadPool.h>
#include <kiwi/Utils.h>
#include "StrUtils.h"
#include "serializer.hpp"

using namespace std;
using namespace kiwi;

namespace kiwi
{
	struct puhash
	{
		size_t operator()(const pair<uint16_t, uint16_t>& o) const
		{
			return hash<uint16_t>{}(o.first) | (hash<uint16_t>{}(o.second) << 16);
		}
	};

	template<class ChrTy>
	bool startsWith(const std::basic_string<ChrTy>& s, const std::basic_string<ChrTy>& pf)
	{
		if (s.size() < pf.size()) return false;
		return std::equal(pf.begin(), pf.end(), s.begin());
	}

	template<class KeyType = char16_t, class ValueType = uint32_t>
	class WordDictionary
	{
	protected:
		std::map<KeyType, ValueType> word2id;
		std::vector<KeyType> id2word;
		std::mutex mtx;
	public:

		WordDictionary() {}
		WordDictionary(const WordDictionary& o) : word2id(o.word2id), id2word(o.id2word) {}
		WordDictionary(WordDictionary&& o)
		{
			std::swap(word2id, o.word2id);
			std::swap(id2word, o.id2word);
		}

		WordDictionary& operator=(WordDictionary&& o)
		{
			std::swap(word2id, o.word2id);
			std::swap(id2word, o.id2word);
			return *this;
		}

		enum { npos = (ValueType)-1 };

		ValueType add(const KeyType& str)
		{
			if (word2id.emplace(str, word2id.size()).second) id2word.emplace_back(str);
			return word2id.size() - 1;
		}

		ValueType getOrAdd(const KeyType& str)
		{
			std::lock_guard<std::mutex> lg(mtx);
			auto it = word2id.find(str);
			if (it != word2id.end()) return it->second;
			return add(str);
		}

		template<class Iter>
		std::vector<ValueType> getOrAdds(Iter begin, Iter end)
		{
			std::lock_guard<std::mutex> lg(mtx);
			return getOrAddsWithoutLock(begin, end);
		}

		template<class Iter>
		std::vector<ValueType> getOrAddsWithoutLock(Iter begin, Iter end)
		{
			std::vector<ValueType> ret;
			for (; begin != end; ++begin)
			{
				auto it = word2id.find(*begin);
				if (it != word2id.end()) ret.emplace_back(it->second);
				else ret.emplace_back(add(*begin));
			}
			return ret;
		}

		template<class Func>
		void withLock(const Func& f)
		{
			std::lock_guard<std::mutex> lg(mtx);
			f();
		}

		ValueType get(const KeyType& str) const
		{
			auto it = word2id.find(str);
			if (it != word2id.end()) return it->second;
			return npos;
		}

		const KeyType& getStr(ValueType id) const
		{
			return id2word[id];
		}

		size_t size() const { return id2word.size(); }
	};

	inline bool isalnum16(char16_t c)
	{
		if (c < 0 || c > 255) return false;
		return std::isalnum(c);
	}

	template<class LocalData, class FuncReader, class FuncProc>
	std::vector<LocalData> readProc(size_t numWorkers, const FuncReader& reader, const FuncProc& processor, LocalData&& ld = {})
	{
		utils::ThreadPool workers{ numWorkers, numWorkers * 2 };
		std::vector<LocalData> ldByTid(workers.size(), ld);
		while (1)
		{
			auto ustr = reader();
			if (ustr.empty()) break;
			workers.enqueue([&, ustr](size_t tid)
			{
				auto& ld = ldByTid[tid];
			processor(ustr, ld);
			});
		}
		workers.joinAll();
		return ldByTid;
	}
}

struct WordDetector::Counter
{
	WordDictionary<char16_t, uint16_t> chrDict;
	std::vector<uint32_t> cntUnigram;
	std::unordered_set<std::pair<uint16_t, uint16_t>, puhash> candBigram;
	std::map<std::u16string, uint32_t> forwardCnt, backwardCnt;
};

WordDetector::WordDetector(const std::string& modelPath, size_t _numThreads)
	: numThreads{ _numThreads ? _numThreads : std::thread::hardware_concurrency() }
{
	{
		ifstream ifs;
		serializer::readMany(openFile(ifs, modelPath + "/extract.mdl", ios_base::binary), posScore, nounTailScore);
	}
}

WordDetector::WordDetector(FromRawData, const std::string& modelPath, size_t _numThreads)
	: numThreads{ _numThreads ? _numThreads : std::thread::hardware_concurrency() }
{
	{
		ifstream ifs;
		loadPOSModelFromTxt(openFile(ifs, modelPath + "/RPosModel.txt"));
	}
	{
		ifstream ifs;
		loadNounTailModelFromTxt(openFile(ifs, modelPath + "/NounTailList.txt"));
	}
}

void WordDetector::saveModel(const std::string& modelPath) const
{
	{
		ofstream ofs{ modelPath + "/extract.mdl", ios_base::binary };
		if (!ofs) throw Exception{ "Failed to open model file '" + modelPath + "extract.mdl'." };
		serializer::writeMany(ofs, posScore, nounTailScore);
	}
}

void WordDetector::countUnigram(Counter& cdata, const U16Reader& reader, size_t minCnt) const
{
	auto ldUnigram = readProc<vector<uint32_t>>(numThreads, reader, [this, &cdata](u16string ustr, vector<uint32_t>& ld)
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

void WordDetector::countBigram(Counter& cdata, const U16Reader& reader, size_t minCnt) const
{
	auto ldBigram = readProc<vector<uint32_t>>(numThreads, reader, [this, &cdata](u16string ustr, vector<uint32_t>& ld)
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

void WordDetector::countNgram(Counter& cdata, const U16Reader& reader, size_t minCnt, size_t maxWordLen) const
{
	{
		utils::ThreadPool rtForward{ 1, 2 }, rtBackward{ 1, 2 };
		vector<future<void>> futures;
		while (1)
		{
			auto ustr = reader();
			if (ustr.empty()) break;
			SpaceSplitIterator begin{ ustr.begin(), ustr.end() }, end;
			auto idss = make_shared<vector<vector<int16_t>>>();
			for (; begin != end; ++begin)
			{
				vector<int16_t> ids;
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
	
	u16string prefixToErase = {};
	for (auto it  = cdata.forwardCnt.cbegin(); it != cdata.forwardCnt.cend();)
	{
		auto& p = *it;
		if (prefixToErase.empty() || !startsWith(p.first, prefixToErase))
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
		if (prefixToErase.empty() || !startsWith(p.first, prefixToErase))
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

float WordDetector::branchingEntropy(const map<u16string, uint32_t>& cnt, map<u16string, uint32_t>::iterator it, size_t minCnt, float defaultPerp) const
{
	u16string endKey = it->first;
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
			entropy -= p * std::log(p / defaultPerp);
		}
		else
		{
			entropy -= p * std::log(p);
		}
	}

	if (sum < tot)
	{
		float p = (tot - sum) / tot;
		entropy -= p * std::log(p / ((tot - sum) / minCnt));
	}

	return entropy;
}

map<POSTag, float> WordDetector::getPosScore(Counter & cdata, 
	const std::map<u16string, uint32_t>& cnt, 
	std::map<u16string, uint32_t>::iterator it, 
	bool coda, 
	const u16string& realForm
) const
{
	map<POSTag, float> ret;
	u16string endKey = it->first;
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
			ret[POSTag::nnp] += nount.second * .25f;
			break;
		}
	}
	return ret;
}

void WordDetector::loadPOSModelFromTxt(std::istream & is)
{
	string line;
	while (getline(is, line))
	{
		auto fields = split(utf8To16(line), u'\t');
		if (fields.size() < 4) continue;
		POSTag pos = toPOSTag(fields[0]);
		bool coda = !!stof(fields[1].begin(), fields[1].end());
		char16_t chr = fields[2][0];
		float p = stof(fields[3].begin(), fields[3].end());
		posScore[make_pair(pos, coda)][chr] = p;
	}
}

void WordDetector::loadNounTailModelFromTxt(std::istream & is)
{
	string line;
	while (getline(is, line))
	{
		auto fields = split(utf8To16(line), u'\t');
		if (fields.size() < 4) continue;
		float p = stof(fields[1].begin(), fields[1].end());
		nounTailScore[fields[0].to_string()] = p;
	}
}

vector<WordInfo> WordDetector::extractWords(const U16MultipleReader& reader, size_t minCnt, size_t maxWordLen, float minScore) const
{
	Counter cdata;
	countUnigram(cdata, reader(), minCnt);
	countBigram(cdata, reader(), minCnt);
	countNgram(cdata, reader(), minCnt, maxWordLen);

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
			forwardCohesion = std::pow(forwardCohesion, 1.f / (p.first.size() * 2 - 3));
			backwardCohesion = std::pow(backwardCohesion, 1.f / (p.first.size() * 2 - 3));
		}
		else if (p.first.size() > 3)
		{
			forwardCohesion *= p.second / (float)cdata.forwardCnt.find({ p.first.begin(), p.first.begin() + 2 })->second;
			backwardCohesion *= p.second / (float)cdata.backwardCnt.find({ p.first.rbegin(), p.first.rbegin() + 2 })->second;
			forwardCohesion *= p.second / (float)cdata.forwardCnt.find({ p.first.begin(), p.first.begin() + 3 })->second;
			backwardCohesion *= p.second / (float)cdata.backwardCnt.find({ p.first.rbegin(), p.first.rbegin() + 3 })->second;
			forwardCohesion = std::pow(forwardCohesion, 1.f / (p.first.size() * 3 - 6));
			backwardCohesion = std::pow(backwardCohesion, 1.f / (p.first.size() * 3 - 6));
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
		float r = branchingEntropy(cdata.backwardCnt, bit, minCnt);
		if (r >= 1)
		{
			u16string form;
			form.reserve(p.first.size());
			transform(p.first.begin(), p.first.end(), back_inserter(form), [this, &cdata](char16_t c) { return cdata.chrDict.getStr(c); });
			if (any_of(form.begin(), form.end(), isalnum16)) continue;
			rPartEntropy[form] = r;
		}
	}

	sort(cands.begin(), cands.end(), [](const WordInfo& a, const WordInfo& b)
	{
		return a.score > b.score;
	});

	vector<utils::TrieNode<char16_t, uint32_t, utils::ConstAccess<map<char16_t, int32_t>>>> trieNodes(1), trieBackNodes(1);
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
		if ((r.form.front() & 0xFC00) == 0xDC00)
		{
			r.form.erase(r.form.begin());
		}
		if ((r.form.back() & 0xFC00) == 0xD800)
		{
			r.form.pop_back();
		}

		/*
		removing super-matched forms
		ex) correct form: ABC, matched forms: ABC, ABC_D, ABC_E ...
		remove ABC_D, ABC_E ... if branching entropy of D, E ... is higher
		*/
		auto pm = trieNodes[0].findMaximumMatch(r.form.begin(), r.form.end());
		if (pm.second)
		{
			auto& pr = ret[pm.first->val - 1];
			if (r.form.size() <= pr.form.size() + 3)
			{
				float rEntropy = 1;
				auto rpit = rPartEntropy.find({ r.form.rbegin(), r.form.rbegin() + r.form.size() - pr.form.size() });
				if (rpit != rPartEntropy.end()) rEntropy = max(rpit->second, rEntropy);
				if (rEntropy > 2.5f && r.freq < ((rEntropy - 1) / rEntropy) * pr.freq)
				{
					//cerr << utf16To8(r.form) << '\t' << r.freq << '\t' << utf16To8(pr.form) << '\t' << pr.freq << '\t' << rEntropy << endl;
					continue;
				}
			}
		}

		/*
		removing sub-matched forms
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
				entropy -= p * std::log(p);
			}
			if (entropy < 1.5 && (tot + 25) > (r.freq + 25) * 0.9)
			{
				//cerr << utf16To8(r.form) << endl;
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
				entropy -= p * std::log(p);
			}
			if (entropy < 1.5 && (tot + 25) >(r.freq + 25) * 0.9)
			{
				//cerr << utf16To8(r.form) << endl;
				continue;
			}
		}

		if (trieNodes.capacity() < trieNodes.size() + r.form.size())
		{
			trieNodes.reserve(max(trieNodes.capacity() * 2, trieNodes.size() + r.form.size()));
		}
		trieNodes[0].build(r.form.begin(), r.form.end(), ret.size() + 1, addNode);

		if (trieBackNodes.capacity() < trieBackNodes.size() + r.form.size())
		{
			trieBackNodes.reserve(max(trieBackNodes.capacity() * 2, trieBackNodes.size() + r.form.size()));
		}
		trieBackNodes[0].build(r.form.rbegin(), r.form.rend(), ret.size() + 1, addBackNode);
		ret.emplace_back(move(r));
	}

	return ret;
}
