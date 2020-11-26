#include "KiwiHeader.h"
#include "KTrie.h"
#include "Utils.h"
#include "serializer.hpp"
#include "KModelMgr.h"

constexpr uint32_t KIWI_MAGICID = 0x4B495749;

namespace std
{
	template <>
	class hash<pair<kiwi::k_string, kiwi::KPOSTag>> {
	public:
		size_t operator() (const pair<kiwi::k_string, kiwi::KPOSTag>& o) const
		{
			return hash<kiwi::k_string>{}(o.first) ^ (size_t)o.second;
		};
	};
}

using namespace std;
using namespace kiwi;

#ifdef LOAD_TXT
void KModelMgr::loadMMFromTxt(std::istream& is, MorphemeMap& morphMap, std::unordered_map<KPOSTag, float>* posWeightSum, const function<bool(float, KPOSTag)>& selector)
{
	string line;
	while (getline(is, line))
	{
		auto wstr = utf8_to_utf16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 8) continue;

		auto form = normalizeHangul({ fields[0].begin(), fields[0].end() });
		auto tag = makePOSTag({ fields[1].begin(), fields[1].end() });

		float morphWeight = stof(fields[2].begin(), fields[2].end());
		if (!selector(morphWeight, tag)) continue;
		if (posWeightSum) (*posWeightSum)[tag] += morphWeight;
		float vowel = stof(fields[4].begin(), fields[4].end());
		float vocalic = stof(fields[5].begin(), fields[5].end());
		float vocalicH = stof(fields[6].begin(), fields[6].end());
		float positive = stof(fields[7].begin(), fields[7].end());

		KCondVowel cvowel = KCondVowel::none;
		KCondPolarity polar = KCondPolarity::none;
		if (tag >= KPOSTag::JKS && tag <= KPOSTag::JC)
		{
			std::array<float, 6> t = { vowel, vocalic, vocalicH, 1 - vowel, 1 - vocalic, 1 - vocalicH };
			size_t pmIdx = max_element(t.begin(), t.end()) - t.begin();
			if (t[pmIdx] >= 0.85f)
			{
				cvowel = (KCondVowel)(pmIdx + 2);
			}
			else
			{
				cvowel = KCondVowel::any;
			}
		}

		/*if (tag >= KPOSTag::EP && tag <= KPOSTag::ETM)
		{
			std::array<float, 2> u = { positive, 1 - positive };
			size_t pmIdx = max_element(u.begin(), u.end()) - u.begin();
			if (u[pmIdx] >= 0.825f)
			{
				polar = (KCondPolarity)(pmIdx + 1);
			}
		}*/
		auto& fm = formMapper(form, cvowel, polar);
		bool unified = false;
		if (tag >= KPOSTag::EP && tag <= KPOSTag::ETM && form[0] == u'아')
		{
			form[0] = u'어';
			unified = true;
		}
		auto it = morphMap.find(make_pair(form, tag));
		if (it != morphMap.end())
		{
			fm.candidate.emplace_back((KMorpheme*)it->second);
			if(!unified) morphemes[it->second].kform = (const k_string*)(&fm - &forms[0]);
		}
		else
		{
			size_t mid = morphemes.size();
			morphMap.emplace(make_pair(form, tag), mid);
			fm.candidate.emplace_back((KMorpheme*)mid);
			morphemes.emplace_back(form, tag, cvowel, polar);
			morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
			morphemes.back().userScore = morphWeight;
		}
	}
}


void KModelMgr::loadCMFromTxt(std::istream& is, MorphemeMap& morphMap)
{
	static std::array<k_char*, 4> conds = { KSTR("+"), KSTR("-Coda"), KSTR("+"), KSTR("+") };
	static std::array<k_char*, 3> conds2 = { KSTR("+"), KSTR("+Positive"), KSTR("-Positive") };

	string line;
	while (getline(is, line))
	{
		auto wstr = utf8_to_utf16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 2) continue;
		if (fields.size() == 2) fields.emplace_back();
		auto form = normalizeHangul({ fields[0].begin(), fields[0].end() });
		unique_ptr<vector<const KMorpheme*>> chunkIds = make_unique<vector<const KMorpheme*>>();
		float ps = 0;
		size_t bTag = 0;
		for (auto chunk : split(fields[1], u'+'))
		{
			auto c = split(chunk, u'/');
			if (c.size() < 2) continue;
			auto f = normalizeHangul({ c[0].begin(), c[0].end() });
			auto tag = makePOSTag({ c[1].begin(), c[1].end() });
			auto it = morphMap.find(make_pair(f, tag));
			if (it != morphMap.end())
			{
				chunkIds->emplace_back((KMorpheme*)it->second);
			}
			else if (tag == KPOSTag::V)
			{
				size_t mid = morphemes.size();
				morphMap.emplace(make_pair(f, tag), mid);
				auto& fm = formMapper(f, KCondVowel::none, KCondPolarity::none);
				morphemes.emplace_back(f, tag);
				morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
				chunkIds->emplace_back((KMorpheme*)mid);
			}
			else
			{
				goto continueFor;
			}
			bTag = (size_t)tag;
		}

		KCondVowel vowel = morphemes[((size_t)chunkIds->at(0))].vowel;
		KCondPolarity polar = morphemes[((size_t)chunkIds->at(0))].polar;
		{
			auto pm = find(conds.begin(), conds.end(), fields[2]);
			if (pm != conds.end())
			{
				vowel = (KCondVowel)(pm - conds.begin() + 1);
			}
		}
		{
			auto pm = find(conds2.begin(), conds2.end(), fields[2]);
			if (pm != conds2.end())
			{
				polar = (KCondPolarity)(pm - conds2.begin());
			}
		}
		uint8_t combineSocket = 0;
		if (fields.size() >= 4 && !fields[3].empty())
		{
			combineSocket = (size_t)stof(fields[3].begin(), fields[3].end());
		}

		size_t mid = morphemes.size();
		auto& fm = formMapper(form, vowel, polar);
		fm.candidate.emplace_back((KMorpheme*)mid);
		morphemes.emplace_back(form, KPOSTag::UNKNOWN, vowel, polar, combineSocket);
		morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
		morphemes.back().chunks = move(chunkIds);
	continueFor:;
	}
}


void KModelMgr::loadPCMFromTxt(std::istream& is, MorphemeMap& morphMap)
{
	string line;
	while (getline(is, line))
	{
		auto wstr = utf8_to_utf16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 4) continue;

		auto combs = split(fields[0], u'+');
		auto org = combs[0] + combs[1];
		auto form = normalizeHangul({ combs[0].begin(), combs[0].end() });
		auto orgform = normalizeHangul({ org.begin(), org.end() });
		auto tag = makePOSTag({ fields[1].begin(), fields[1].end() });
		k_string suffixes = normalizeHangul({ fields[2].begin(), fields[2].end() });
		uint8_t socket = (size_t)stof(fields[3].begin(), fields[3].end());

		auto mit = morphMap.find(make_pair(orgform, tag));
		if (mit == morphMap.end()) continue;
		if (!form.empty())
		{
			size_t mid = morphemes.size();
			//morphMap.emplace(make_pair(form, tag), mid);
			auto& fm = formMapper(form, KCondVowel::none, KCondPolarity::none);
			fm.candidate.emplace_back((KMorpheme*)mid);
			morphemes.emplace_back(form, tag, KCondVowel::none, KCondPolarity::none, socket);
			morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
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
		auto wstr = utf8_to_utf16(line);
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
		auto tag = makePOSTag({ fields[1].begin(), fields[1].end() });
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
		auto wstr = utf8_to_utf16(line);
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

			auto t = makePOSTag(fields[i + 1]);

			if ((f[0] == u'아' || f[0] == u'야') && fields[i + 1][0] == 'E')
			{
				if(f[0] == u'아') f[0] == u'어';
				else f[0] == u'여';
			}
			
			auto it = morphMap.find(make_pair(f, t));
			if (it == morphMap.end() || morphemes[it->second].chunks || morphemes[it->second].combineSocket)
			{
				if (t <= KPOSTag::SN && t != KPOSTag::UNKNOWN)
				{
					wids.emplace_back((size_t)t + 1);
				}
				else
				{
					wids.emplace_back((size_t)KPOSTag::NNP);
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
	vector<pair<KForm, size_t>> formOrder;
	vector<size_t> newIdcs(forms.size());

	for (size_t i = 0; i < forms.size(); ++i)
	{
		formOrder.emplace_back(move(forms[i]), i);
	}
	sort(formOrder.begin() + (size_t)KPOSTag::DEFAULT_TAG_SIZE, formOrder.end());

	forms.clear();
	for (size_t i = 0; i < formOrder.size(); ++i)
	{
		forms.emplace_back(move(formOrder[i].first));
		newIdcs[formOrder[i].second] = i;
	}

	for (auto& m : morphemes)
	{
		m.kform = (k_string*)newIdcs[(size_t)m.kform];
	}
}

void KModelMgr::saveMorphBin(std::ostream& os) const
{
	serializer::writeToBinStream(os, KIWI_MAGICID);
	serializer::writeToBinStream<uint32_t>(os, forms.size());
	serializer::writeToBinStream<uint32_t>(os, morphemes.size());
	serializer::writeToBinStream<uint32_t>(os, baseTrieSize);

	auto mapper = [this](const KMorpheme* p)->size_t
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
	vector<KTrie> tries;
	size_t est = 0;
	for (auto& f : forms)
	{
		est += f.form.size();
	}
	tries.reserve(est);
	tries.emplace_back();
	for (auto& f : forms)
	{
		if (f.candidate.empty()) continue;
		tries[0].build(&f.form[0], f.form.size(), &f, [&]()
		{
			tries.emplace_back();
			return &tries.back();
		});
	}
	return tries.size() + (size_t)KPOSTag::SN + 3;
}

#endif

template<class _Istream>
void KModelMgr::loadMorphBin(_Istream& is)
{
	if (serializer::readFromBinStream<uint32_t>(is) != KIWI_MAGICID) throw KiwiException("[loadMorphBin] Input file is corrupted.");
	size_t formSize = serializer::readFromBinStream<uint32_t>(is);
	size_t morphemeSize = serializer::readFromBinStream<uint32_t>(is);
	baseTrieSize = serializer::readFromBinStream<uint32_t>(is);

	forms.resize(formSize);
	morphemes.resize(morphemeSize);

	auto mapper = [this](size_t p)->const KMorpheme*
	{
		return (const KMorpheme*)p;
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

KForm & KModelMgr::formMapper(k_string form, KCondVowel vowel, KCondPolarity polar)
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
	forms.resize((size_t)KPOSTag::DEFAULT_TAG_SIZE);
	morphemes.resize((size_t)KPOSTag::DEFAULT_TAG_SIZE + 2); // additional places for <s> & </s>
	for (size_t i = 0; i < (size_t)KPOSTag::DEFAULT_TAG_SIZE; ++i)
	{
		forms[i].candidate.emplace_back((KMorpheme*)(i + 2));
		morphemes[i + 2].tag = (KPOSTag)(i + 1);
	}

	size_t morphIdToUpdate = morphemes.size();
	MorphemeMap morphMap;
	loadMMFromTxt(ifstream{ modelPath + string{ "fullmodelV2.txt" } }, morphMap, nullptr, [](float morphWeight, KPOSTag tag) {
		return morphWeight >= (tag < KPOSTag::JKS ? 10 : 10);
	});
	for (size_t i = morphIdToUpdate; i < morphemes.size(); ++i)
	{
		morphemes[i].userScore = 0;
	}

	morphIdToUpdate = morphemes.size();
	auto realMorph = morphMap;
	unordered_map<KPOSTag, float> unknownWeight;
	loadMMFromTxt(ifstream{ modelPath + string{ "fullmodelV2.txt" } }, morphMap, &unknownWeight, [](float morphWeight, KPOSTag tag) {
		return tag < KPOSTag::JKS && morphWeight < 10;
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
	langMdl = make_shared<KNLangModel>(3);
	loadCorpusFromTxt(ifstream{ modelPath + string{ "ML_lit.txt" } }, realMorph, ams);
	loadCorpusFromTxt(ifstream{ modelPath + string{ "ML_spo.txt" } }, realMorph, ams);
	langMdl->optimize(ams);
	langMdl->writeToStream(ofstream{ modelPath + string{ "sj.lang" }, ios_base::binary });
	
#else
	{
		ifstream ifs{ modelPath + string{ "sj.morph" }, ios_base::binary };
		if (ifs.fail()) throw KiwiException{ std::string{"[KModelMgr] Failed to find file '"} + modelPath + "sj.morph'." };
		ifs.seekg(0, ios_base::end);
		string buffer(ifs.tellg(), 0);
		ifs.seekg(0);
		ifs.read(&buffer[0], buffer.size());
		serializer::imstream iss{ buffer.data(), buffer.size() };
		loadMorphBin(iss);
	}
	{
		ifstream ifs{ modelPath + string{ "sj.lang" }, ios_base::binary };
		if (ifs.fail()) throw KiwiException{ std::string{"[KModelMgr] Failed to find file '"} + modelPath + "sj.lang'." };
		ifs.seekg(0, ios_base::end);
		string buffer(ifs.tellg(), 0);
		ifs.seekg(0);
		ifs.read(&buffer[0], buffer.size());
		serializer::imstream iss{ buffer.data(), buffer.size() };
		langMdl = make_shared<KNLangModel>(KNLangModel::readFromStream(move(iss)));
	}
#endif
}

void KModelMgr::addUserWord(const k_string & form, KPOSTag tag, float userScore)
{
	if (!trieRoot.empty()) throw KiwiException{ "Cannot addUserWord() after prepare()" };
	if (form.empty()) return;

	auto& f = formMapper(form, KCondVowel::none, KCondPolarity::none);
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

	f.candidate.emplace_back((const KMorpheme*)morphemes.size());
	morphemes.emplace_back(form, tag);
	morphemes.back().kform = (const k_string*)(&f - &forms[0]);
	morphemes.back().userScore = userScore;
}

void KModelMgr::solidify()
{
	if (!trieRoot.empty()) throw KiwiException("[solidify] Cannot solidify twice.");
	trieRoot.reserve(baseTrieSize + extraTrieSize);
	trieRoot.resize((size_t)KPOSTag::DEFAULT_TAG_SIZE + 1); // preserve places for root node + default tag morphemes
	for (size_t i = 1; i <= (size_t)KPOSTag::DEFAULT_TAG_SIZE; ++i)
	{
		trieRoot[i].val = &forms[i - 1];
	}

	for (size_t i = (size_t)KPOSTag::DEFAULT_TAG_SIZE; i < forms.size(); ++i)
	{
		auto& f = forms[i];
		if (f.candidate.empty()) continue;
		size_t realSize = f.form.size();
		if (trieRoot.capacity() < trieRoot.size() + realSize)
		{
			trieRoot.reserve(max(trieRoot.size() + realSize, trieRoot.capacity() + trieRoot.capacity() / 4));
		}

		trieRoot[0].build(&f.form[0], realSize, &f, [this]()
		{
			trieRoot.emplace_back();
			return &trieRoot.back();
		});
	}
	trieRoot[0].fillFail();

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

unordered_set<k_string> KModelMgr::getAllForms() const
{
	unordered_set<k_string> ret;
	for (auto& r : forms)
	{
		ret.emplace(r.form);
	}
	return ret;
}
