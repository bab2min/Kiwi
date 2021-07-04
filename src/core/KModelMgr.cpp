#include <fstream>
#include <iostream>

#include <kiwi/Types.h>
#include "KTrie.h"
#include "Utils.h"
#include "serializer.hpp"
#include "KModelMgr.h"
#include "FrozenTrie.hpp"
#include "count.hpp"
#include "Knlm.hpp"

template class kiwi::utils::FrozenTrie<kiwi::kchar_t, const kiwi::Form*>;

constexpr uint32_t KIWI_MAGICID = 0x4B495749;

namespace std
{
	template <>
	class hash<pair<kiwi::KString, kiwi::POSTag>> {
	public:
		size_t operator() (const pair<kiwi::KString, kiwi::POSTag>& o) const
		{
			return hash<kiwi::KString>{}(o.first) ^ (size_t)o.second;
		};
	};
}

using namespace std;
using namespace kiwi;

#ifdef LOAD_TXT
void KModelMgr::loadMMFromTxt(std::istream& is, MorphemeMap& morphMap, std::unordered_map<POSTag, float>* posWeightSum, const function<bool(float, POSTag)>& selector)
{
	string line;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 8) continue;

		auto form = normalizeHangul({ fields[0].begin(), fields[0].end() });
		auto tag = toPOSTag({ fields[1].begin(), fields[1].end() });

		float morphWeight = stof(fields[2].begin(), fields[2].end());
		if (!selector(morphWeight, tag)) continue;
		if (posWeightSum) (*posWeightSum)[tag] += morphWeight;
		float vowel = stof(fields[4].begin(), fields[4].end());
		float vocalic = stof(fields[5].begin(), fields[5].end());
		float vocalic_h = stof(fields[6].begin(), fields[6].end());
		float positive = stof(fields[7].begin(), fields[7].end());

		CondVowel cvowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;
		if (tag >= POSTag::jks && tag <= POSTag::jc)
		{
			std::array<float, 6> t = { vowel, vocalic, vocalic_h, 1 - vowel, 1 - vocalic, 1 - vocalic_h };
			size_t pmIdx = max_element(t.begin(), t.end()) - t.begin();
			if (t[pmIdx] >= 0.85f)
			{
				cvowel = (CondVowel)(pmIdx + 2);
			}
			else
			{
				cvowel = CondVowel::any;
			}
		}

		/*if (tag >= POSTag::ep && tag <= POSTag::etm)
		{
			std::array<float, 2> u = { positive, 1 - positive };
			size_t pmIdx = max_element(u.begin(), u.end()) - u.begin();
			if (u[pmIdx] >= 0.825f)
			{
				polar = (CondPolarity)(pmIdx + 1);
			}
		}*/
		auto& fm = formMapper(form, cvowel, polar);
		bool unified = false;
		if (tag >= POSTag::ep && tag <= POSTag::etm && form[0] == u'아')
		{
			form[0] = u'어';
			unified = true;
		}
		auto it = morphMap.find(make_pair(form, tag));
		if (it != morphMap.end())
		{
			fm.candidate.emplace_back((Morpheme*)it->second);
			if(!unified) morphemes[it->second].kform = (const KString*)(&fm - &forms[0]);
		}
		else
		{
			size_t mid = morphemes.size();
			morphMap.emplace(make_pair(form, tag), mid);
			fm.candidate.emplace_back((Morpheme*)mid);
			morphemes.emplace_back(form, tag, cvowel, polar);
			morphemes.back().kform = (const KString*)(&fm - &forms[0]);
			morphemes.back().userScore = morphWeight;
		}
	}
}


void KModelMgr::loadCMFromTxt(std::istream& is, MorphemeMap& morphMap)
{
	static std::array<kchar_t*, 4> conds = { u"+", u"-Coda", u"+", u"+" };
	static std::array<kchar_t*, 3> conds2 = { u"+", u"+Positive", u"-Positive" };

	string line;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 2) continue;
		if (fields.size() == 2) fields.emplace_back();
		auto form = normalizeHangul({ fields[0].begin(), fields[0].end() });
		unique_ptr<vector<const Morpheme*>> chunkIds = make_unique<vector<const Morpheme*>>();
		float ps = 0;
		size_t bTag = 0;
		for (auto chunk : split(fields[1], u'+'))
		{
			auto c = split(chunk, u'/');
			if (c.size() < 2) continue;
			auto f = normalizeHangul({ c[0].begin(), c[0].end() });
			auto tag = toPOSTag({ c[1].begin(), c[1].end() });
			auto it = morphMap.find(make_pair(f, tag));
			if (it != morphMap.end())
			{
				chunkIds->emplace_back((Morpheme*)it->second);
			}
			else if (tag == POSTag::v)
			{
				size_t mid = morphemes.size();
				morphMap.emplace(make_pair(f, tag), mid);
				auto& fm = formMapper(f, CondVowel::none, CondPolarity::none);
				morphemes.emplace_back(f, tag);
				morphemes.back().kform = (const KString*)(&fm - &forms[0]);
				chunkIds->emplace_back((Morpheme*)mid);
			}
			else
			{
				goto continueFor;
			}
			bTag = (size_t)tag;
		}

		CondVowel vowel = morphemes[((size_t)chunkIds->at(0))].vowel;
		CondPolarity polar = morphemes[((size_t)chunkIds->at(0))].polar;
		{
			auto pm = find(conds.begin(), conds.end(), fields[2]);
			if (pm != conds.end())
			{
				vowel = (CondVowel)(pm - conds.begin() + 1);
			}
		}
		{
			auto pm = find(conds2.begin(), conds2.end(), fields[2]);
			if (pm != conds2.end())
			{
				polar = (CondPolarity)(pm - conds2.begin());
			}
		}
		uint8_t combineSocket = 0;
		if (fields.size() >= 4 && !fields[3].empty())
		{
			combineSocket = (size_t)stof(fields[3].begin(), fields[3].end());
		}

		size_t mid = morphemes.size();
		auto& fm = formMapper(form, vowel, polar);
		fm.candidate.emplace_back((Morpheme*)mid);
		morphemes.emplace_back(form, POSTag::unknown, vowel, polar, combineSocket);
		morphemes.back().kform = (const KString*)(&fm - &forms[0]);
		morphemes.back().chunks = move(chunkIds);
	continueFor:;
	}
}


void KModelMgr::loadPCMFromTxt(std::istream& is, MorphemeMap& morphMap)
{
	string line;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 4) continue;

		auto combs = split(fields[0], u'+');
		auto org = combs[0] + combs[1];
		auto form = normalizeHangul({ combs[0].begin(), combs[0].end() });
		auto orgform = normalizeHangul({ org.begin(), org.end() });
		auto tag = toPOSTag({ fields[1].begin(), fields[1].end() });
		KString suffixes = normalizeHangul({ fields[2].begin(), fields[2].end() });
		uint8_t socket = (size_t)stof(fields[3].begin(), fields[3].end());

		auto mit = morphMap.find(make_pair(orgform, tag));
		if (mit == morphMap.end()) continue;
		if (!form.empty())
		{
			size_t mid = morphemes.size();
			//morphMap.emplace(make_pair(form, tag), mid);
			auto& fm = formMapper(form, CondVowel::none, CondPolarity::none);
			fm.candidate.emplace_back((Morpheme*)mid);
			morphemes.emplace_back(form, tag, CondVowel::none, CondPolarity::none, socket);
			morphemes.back().kform = (const KString*)(&fm - &forms[0]);
			morphemes.back().combined = (int)mit->second - ((int)morphemes.size() - 1);
		}
	}
}

KNLangModel::AllomorphSet KModelMgr::loadAllomorphFromTxt(std::istream & is, const MorphemeMap& morphMap)
{
	KNLangModel::AllomorphSet ams;
	string line;
	vector<KNLangModel::WID> group;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (wstr.empty())
		{
			ams.addGroup(group.begin(), group.end());
			group.clear();
			continue;
		}
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 2) continue;
		auto form = normalizeHangul({ fields[0].begin(), fields[0].end() });
		auto tag = toPOSTag({ fields[1].begin(), fields[1].end() });
		auto it = morphMap.find(make_pair(form, tag));
		if (it != morphMap.end())
		{
			group.emplace_back(it->second);
		}
	}
	return ams;
}

void KModelMgr::loadCorpusFromTxt(std::istream & is, MorphemeMap& morphMap, const KNLangModel::AllomorphSet& ams)
{
	string line;
	vector<KNLangModel::WID> wids;
	wids.emplace_back(0);
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		if (wstr.empty() && wids.size() > 1)
		{
			wids.emplace_back(1);
			langMdl->trainSequence(&wids[0], wids.size());
			wids.erase(wids.begin() + 1, wids.end());
			continue;
		}
		auto fields = split(wstr, u'\t');
		if (fields.size() < 2) continue;

		for (size_t i = 1; i < fields.size(); i += 2)
		{
			auto f = normalizeHangul(fields[i]);
			if (f.empty()) continue;

			auto t = toPOSTag(fields[i + 1]);

			if ((f[0] == u'아' || f[0] == u'야') && fields[i + 1][0] == 'E')
			{
				if(f[0] == u'아') f[0] == u'어';
				else f[0] == u'여';
			}
			
			auto it = morphMap.find(make_pair(f, t));
			if (it == morphMap.end() || morphemes[it->second].chunks || morphemes[it->second].combineSocket)
			{
				if (t <= POSTag::sn && t != POSTag::unknown)
				{
					wids.emplace_back((size_t)t + 1);
				}
				else
				{
					wids.emplace_back((size_t)POSTag::nnp);
				}
			}
			else
			{
				auto& g = ams.getGroupByMorph(it->second);
				wids.emplace_back(g.empty() ? it->second : g[0]);
			}
		}
	}
}

void KModelMgr::addCorpusTo(Vector<Vector<uint16_t>>& out, std::istream& is, MorphemeMap& morphMap, const KNLangModel::AllomorphSet& ams)
{
	Vector<uint16_t> wids;
	wids.emplace_back(0);
	string line;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		if (wstr.empty() && wids.size() > 1)
		{
			wids.emplace_back(1);
			out.emplace_back(move(wids));
			wids.emplace_back(0);
			continue;
		}
		auto fields = split(wstr, u'\t');
		if (fields.size() < 2) continue;

		for (size_t i = 1; i < fields.size(); i += 2)
		{
			auto f = normalizeHangul(fields[i]);
			if (f.empty()) continue;

			auto t = toPOSTag(fields[i + 1]);

			if ((f[0] == u'아' || f[0] == u'야') && fields[i + 1][0] == 'E')
			{
				if (f[0] == u'아') f[0] == u'어';
				else f[0] == u'여';
			}

			auto it = morphMap.find(make_pair(f, t));
			if (it == morphMap.end() || morphemes[it->second].chunks || morphemes[it->second].combineSocket)
			{
				if (t <= POSTag::sn && t != POSTag::unknown)
				{
					wids.emplace_back((size_t)t + 1);
				}
				else
				{
					wids.emplace_back((size_t)POSTag::nnp);
				}
			}
			else
			{
				auto& g = ams.getGroupByMorph(it->second);
				wids.emplace_back(g.empty() ? it->second : g[0]);
			}
		}
	}
}

void kiwi::KModelMgr::updateForms()
{
	vector<pair<Form, size_t>> formOrder;
	vector<size_t> newIdcs(forms.size());

	for (size_t i = 0; i < forms.size(); ++i)
	{
		formOrder.emplace_back(move(forms[i]), i);
	}
	sort(formOrder.begin() + defaultTagSize, formOrder.end());

	forms.clear();
	for (size_t i = 0; i < formOrder.size(); ++i)
	{
		forms.emplace_back(move(formOrder[i].first));
		newIdcs[formOrder[i].second] = i;
	}

	for (auto& m : morphemes)
	{
		m.kform = (KString*)newIdcs[(size_t)m.kform];
	}
}

void KModelMgr::saveMorphBin(std::ostream& os) const
{
	serializer::writeToBinStream(os, KIWI_MAGICID);
	serializer::writeToBinStream<uint32_t>(os, forms.size());
	serializer::writeToBinStream<uint32_t>(os, morphemes.size());
	serializer::writeToBinStream<uint32_t>(os, baseTrieSize);

	auto mapper = [this](const Morpheme* p)->size_t
	{
		return (size_t)p;
	};

	for (const auto& form : forms)
	{
		form.writeToBin(os, mapper);
	}
	for (const auto& morph : morphemes)
	{
		morph.writeToBin(os, mapper);
	}
}

size_t KModelMgr::estimateTrieSize() const
{
	utils::ContinuousTrie<KTrie> trie{ 1 };
	for (auto& f : forms)
	{
		if (f.candidate.empty()) continue;
		trie.build(f.form.begin(), f.form.end(), &f);
	}
	return trie.size() + (size_t)POSTag::sn + 3;
}

#endif

template<class _Istream>
void KModelMgr::loadMorphBin(_Istream& is)
{
	if (serializer::readFromBinStream<uint32_t>(is) != KIWI_MAGICID) throw Exception("[loadMorphBin] Input file is corrupted.");
	size_t formSize = serializer::readFromBinStream<uint32_t>(is);
	size_t morphemeSize = serializer::readFromBinStream<uint32_t>(is);
	baseTrieSize = serializer::readFromBinStream<uint32_t>(is);

	forms.resize(formSize);
	morphemes.resize(morphemeSize);

	auto mapper = [this](size_t p)
	{
		return (const Morpheme*)p;
	};

	size_t cnt = 0;
	for (auto& form : forms)
	{
		form.readFromBin(is, mapper);
		formMap.emplace(FormCond{ form.form, form.vowel, form.polar }, cnt++);
	}
	for (auto& morph : morphemes)
	{
		morph.readFromBin(is, mapper);
	}
}

Form & KModelMgr::formMapper(KString form, CondVowel vowel, CondPolarity polar)
{
	FormCond fc{ form, vowel, polar };
	auto it = formMap.find(fc);
	if (it != formMap.end()) return forms[it->second];
	size_t id = forms.size();
	formMap.emplace(fc, id);
	forms.emplace_back(form, vowel, polar);
	return forms[id];
}


KModelMgr::KModelMgr(const char * modelPath)
{
	this->modelPath = modelPath;
#ifdef LOAD_TXT
	// reserve places for default tag forms & morphemes
	forms.resize(defaultTagSize);
	morphemes.resize(defaultTagSize + 2); // additional places for <s> & </s>
	for (size_t i = 0; i < defaultTagSize; ++i)
	{
		forms[i].candidate.emplace_back((Morpheme*)(i + 2));
		morphemes[i + 2].tag = (POSTag)(i + 1);
	}

	size_t morphIdToUpdate = morphemes.size();
	MorphemeMap morphMap;
	loadMMFromTxt(ifstream{ modelPath + string{ "fullmodelV2.txt" } }, morphMap, nullptr, [](float morphWeight, POSTag tag) {
		return morphWeight >= (tag < POSTag::jks ? 10 : 10);
	});
	for (size_t i = morphIdToUpdate; i < morphemes.size(); ++i)
	{
		morphemes[i].userScore = 0;
	}

	morphIdToUpdate = morphemes.size();
	auto realMorph = morphMap;
	unordered_map<POSTag, float> unknownWeight;
	loadMMFromTxt(ifstream{ modelPath + string{ "fullmodelV2.txt" } }, morphMap, &unknownWeight, [](float morphWeight, POSTag tag) {
		return tag < POSTag::jks && morphWeight < 10;
	});
	for (size_t i = morphIdToUpdate; i < morphemes.size(); ++i)
	{
		morphemes[i].userScore = log(morphemes[i].userScore / unknownWeight[morphemes[i].tag]);
	}

	loadCMFromTxt(ifstream{ modelPath + string{ "combinedV2.txt" } }, morphMap);
	loadPCMFromTxt(ifstream{ modelPath + string{ "precombinedV2.txt" } }, morphMap);
	updateForms();
	baseTrieSize = estimateTrieSize();
	saveMorphBin(ofstream{ modelPath + string{ "sj.morph" }, ios_base::binary });
	
	//auto ams = loadAllomorphFromTxt(ifstream{ modelPath + string{ "allomorphs.txt" } }, morphMap);
	KNLangModel::AllomorphSet ams;
	Vector<Vector<uint16_t>> sents;
	addCorpusTo(sents, ifstream{ modelPath + string{ "ML_lit.txt" } }, realMorph, ams);
	addCorpusTo(sents, ifstream{ modelPath + string{ "ML_spo.txt" } }, realMorph, ams);
	vector<pair<uint16_t, uint16_t>> bigramList;
	auto cntNodes = utils::count(sents.begin(), sents.end(), 1, 1, 3, nullptr, &bigramList);
	langMdl2 = lm::KNLangModelBase::create(lm::KNLangModelBase::build(cntNodes, 3, 1, 2, 0, 1, 1e-5, 0, &bigramList));

	langMdl = make_shared<KNLangModel>(3);
	loadCorpusFromTxt(ifstream{ modelPath + string{ "ML_lit.txt" } }, realMorph, ams);
	loadCorpusFromTxt(ifstream{ modelPath + string{ "ML_spo.txt" } }, realMorph, ams);
	langMdl->optimize(ams);
	langMdl->writeToStream(ofstream{ modelPath + string{ "sj.lang" }, ios_base::binary });
	
#else
	{
		ifstream ifs{ modelPath + string{ "sj.morph" }, ios_base::binary };
		if (ifs.fail()) throw Exception{ std::string{"[KModelMgr] Failed to find file '"} + modelPath + "sj.morph'." };
		ifs.seekg(0, ios_base::end);
		string buffer(ifs.tellg(), 0);
		ifs.seekg(0);
		ifs.read(&buffer[0], buffer.size());
		serializer::imstream iss{ buffer.data(), buffer.size() };
		loadMorphBin(iss);
	}
	{
		ifstream ifs{ modelPath + string{ "sj.lang" }, ios_base::binary };
		if (ifs.fail()) throw Exception{ std::string{"[KModelMgr] Failed to find file '"} + modelPath + "sj.lang'." };
		ifs.seekg(0, ios_base::end);
		string buffer(ifs.tellg(), 0);
		ifs.seekg(0);
		ifs.read(&buffer[0], buffer.size());
		serializer::imstream iss{ buffer.data(), buffer.size() };
		langMdl = make_shared<KNLangModel>(KNLangModel::readFromStream(move(iss)));
	}
#endif
}

void KModelMgr::addUserWord(const KString & form, POSTag tag, float userScore)
{
	if (ready()) throw Exception{ "Cannot addUserWord() after prepare()" };
	if (form.empty()) return;

	auto& f = formMapper(form, CondVowel::none, CondPolarity::none);
	if (f.candidate.empty())
	{
		extraTrieSize += form.size() - 1;
	}
	else
	{
		for (auto p : f.candidate)
		{
			// if `form` already has the same `tag`, skip adding
			if (morphemes[(size_t)p].tag == tag) return;
		}
	}

	f.candidate.emplace_back((const Morpheme*)morphemes.size());
	morphemes.emplace_back(form, tag);
	morphemes.back().kform = (const KString*)(&f - &forms[0]);
	morphemes.back().userScore = userScore;
}

void KModelMgr::solidify()
{
	if (ready()) throw Exception("[solidify] Cannot solidify twice.");
	formTrie = utils::ContinuousTrie<KTrie>{ defaultTagSize + 1, baseTrieSize + extraTrieSize };
	// reserve places for root node + default tag morphemes
	for (size_t i = 1; i <= defaultTagSize; ++i)
	{
		formTrie[i].val = &forms[i - 1];
	}

	forms.emplace_back(); // add sentry for last value

	for (size_t i = defaultTagSize; i < forms.size() - 1; ++i)
	{
		auto& f = forms[i];
		if (f.candidate.empty()) continue;
		formTrie.build(f.form.begin(), f.form.end(), &f);
	}
	fTrie = utils::FrozenTrie<kchar_t, const Form*>{ formTrie };
	formTrie = utils::ContinuousTrie<KTrie>{};

	for (auto& f : morphemes)
	{
		f.kform = &forms[(size_t)f.kform].form;
		if (f.chunks) for (auto& p : *f.chunks) p = &morphemes[(size_t)p];
	}

	for (auto& f : forms)
	{
		for (auto& p : f.candidate) p = &morphemes[(size_t)p];
	}
	formMap = {};
}

unordered_set<KString> KModelMgr::getAllForms() const
{
	unordered_set<KString> ret;
	for (auto& r : forms)
	{
		ret.emplace(r.form);
	}
	return ret;
}
