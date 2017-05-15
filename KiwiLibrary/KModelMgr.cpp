#include "stdafx.h"
#include "KForm.h"
#include "KTrie.h"
#include "Utils.h"
#include "KModelMgr.h"

namespace std
{
	template <>
	class hash<pair<k_string, KPOSTag>> {
	public:
		size_t operator() (const pair<k_string, KPOSTag>& o) const
		{
			return hash<k_string>{}(o.first) ^ (size_t)o.second;
		};
	};
}

void KModelMgr::loadPOSFromTxt(const char * filename)
{
#ifdef LOAD_TXT
	for (auto& f : posTransition) for (auto& g : f)
	{
		g = P_MIN;
	}
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw ios_base::failure{ string("Cannot open ") + filename };
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 3) continue;
		auto tagA = makePOSTag(fields[0]);
		if (tagA == KPOSTag::MAX) continue;
		if (fields[1].empty())
		{
			//posTransition[(int)tagA][0] = _wtof(fields[2].c_str());
		}
		else
		{
			auto tagB = makePOSTag(fields[1]);
			if (tagB == KPOSTag::MAX) continue;
			auto p = stof(fields[2].c_str());
			if (p < 0.00007) continue;
			posTransition[(int)tagA][(int)tagB] = logf(p);
		}
	}
	fclose(file);

	/*for (size_t i = 0; i < (size_t)KPOSTag::MAX; i++)
	{
		size_t maxPos = 0;
		for (size_t j = 1; j <= (size_t)KPOSTag::IC; j++)
		{
			if (posTransition[j][i] > posTransition[maxPos][i]) maxPos = j;
		}
		maxiumBf[i] = (KPOSTag)maxPos;
	}*/

	/* 시작부분 조건 완화 */
	for (size_t i = 0; i < (size_t)KPOSTag::MAX; i++)
	{
		if (posTransition[0][i] > P_MIN) posTransition[0][i] = 0;
	}

	for (size_t i = 0; i < (size_t)KPOSTag::MAX; i++) for (size_t j = 0; j < (size_t)KPOSTag::MAX; j++)
	{
		static KPOSTag vOnly[] = {
			KPOSTag::VV, KPOSTag::VA
		};
		size_t maxPos = 1;
		float maxValue = posTransition[i][1] + posTransition[1][j];
		for (size_t n = 1; n < (size_t)KPOSTag::NR; n++)
		{
			if (n == (size_t)KPOSTag::NNB || n == (size_t)KPOSTag::VV || n == (size_t)KPOSTag::VA || n == (size_t)KPOSTag::MAG) continue;
			if (posTransition[i][n] + posTransition[n][j] > maxValue)
			{
				maxPos = n;
				maxValue = posTransition[i][n] + posTransition[n][j];
			}
		}
		maxiumBtwn[i][j] = (KPOSTag)maxPos;

		maxPos = (size_t)vOnly[0];
		maxValue = posTransition[i][(size_t)vOnly[0]] + posTransition[(size_t)vOnly[0]][j];
		for (size_t n = 1; n < LEN_ARRAY(vOnly); n++)
		{
			if (posTransition[i][(size_t)vOnly[n]] + posTransition[(size_t)vOnly[n]][j] > maxValue)
			{
				maxPos = (size_t)vOnly[n];
				maxValue = posTransition[i][(size_t)vOnly[n]] + posTransition[(size_t)vOnly[n]][j];
			}
		}
		maxiumVBtwn[i][j] = (KPOSTag)maxPos;
	}
#endif
}

void KModelMgr::savePOSBin(const char * filename) const
{
	FILE* f;
	if (fopen_s(&f, filename, "wb")) throw ios_base::failure{ string("Cannot open ") + filename };
	fwrite(posTransition, 1, sizeof(posTransition), f);
	fwrite(maxiumBtwn, 1, sizeof(maxiumBtwn), f);
	fwrite(maxiumVBtwn, 1, sizeof(maxiumVBtwn), f);
	fclose(f);
}

void KModelMgr::loadPOSBin(const char * filename)
{
	FILE* f;
	if (fopen_s(&f, filename, "rb")) throw ios_base::failure{string("Cannot open ") + filename};
	fread(posTransition, 1, sizeof(posTransition), f);
	fread(maxiumBtwn, 1, sizeof(maxiumBtwn), f);
	fread(maxiumVBtwn, 1, sizeof(maxiumVBtwn), f);
	fclose(f);
}

void KModelMgr::loadMMFromTxt(const char * filename, unordered_map<pair<k_string, KPOSTag>, size_t>& morphMap)
{
#ifdef LOAD_TXT
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw ios_base::failure{ string("Cannot open ") + filename };
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 9) continue;

		auto form = encodeJamo(fields[0].cbegin(), fields[0].cend());
		auto tag = makePOSTag(fields[1]);
		float morphWeight = stof(fields[2]);
		if (morphWeight < 10 && tag >= KPOSTag::JKS)
		{
			continue;
		}
		float tagWeight = stof(fields[3]);
		float vowel = stof(fields[5]);
		float vocalic = stof(fields[6]);
		float vocalicH = stof(fields[7]);
		float positive = stof(fields[8]);

		KCondVowel cvowel = KCondVowel::none;
		KCondPolarity polar = KCondPolarity::none;
		if (tag >= KPOSTag::JKS && tag <= KPOSTag::ETM)
		{
			float t[] = { vowel, vocalic, vocalicH, 1 - vowel, 1 - vocalic, 1 - vocalicH };
			size_t pmIdx = max_element(t, t + LEN_ARRAY(t)) - t;
			if (t[pmIdx] >= 0.825f)
			{
				cvowel = (KCondVowel)(pmIdx + 2);
			}
			else
			{
				cvowel = KCondVowel::any;
			}

			float u[] = { positive, 1 - positive };
			pmIdx = max_element(u, u + 2) - u;
			if (u[pmIdx] >= 0.825f)
			{
				polar = (KCondPolarity)(pmIdx + 1);
			}
		}
		size_t mid = morphemes.size();
		morphMap.emplace(make_pair(form, tag), mid);
		auto& fm = formMapper(form);
		fm.candidate.emplace_back((KMorpheme*)mid);
		fm.suffix.insert(0);
		morphemes.emplace_back(form, tag, cvowel, polar, logf(tagWeight));
		morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
	}
	fclose(file);
#endif
}


void KModelMgr::loadCMFromTxt(const char * filename, unordered_map<pair<k_string, KPOSTag>, size_t>& morphMap)
{
#ifdef LOAD_TXT
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw ios_base::failure{ string("Cannot open ") + filename };
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 2) continue;
		if (fields.size() == 2) fields.emplace_back();
		auto form = encodeJamo(fields[0].cbegin(), fields[0].cend());
		vector<const KMorpheme*>* chunkIds = new vector<const KMorpheme*>;
		float ps = 0;
		size_t bTag = 0;
		for (auto chunk : split(fields[1], '+'))
		{
			auto c = split(chunk, '/');
			if (c.size() < 2) continue;
			auto f = encodeJamo(c[0].cbegin(), c[0].cend());
			auto tag = makePOSTag(c[1]);
			auto it = morphMap.find(make_pair(f, tag));
			if (it != morphMap.end())
			{
				chunkIds->emplace_back((KMorpheme*)it->second);
			}
			else
			{
				size_t mid = morphemes.size();
				morphMap.emplace(make_pair(f, tag), mid);
				auto& fm = formMapper(f);
				morphemes.emplace_back(f, tag);
				morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
				chunkIds->emplace_back((KMorpheme*)mid);
			}
			if (bTag == (size_t)KPOSTag::V)
			{
			}
			else if (bTag)
			{
				ps += posTransition[bTag][(size_t)tag];
			}
			ps += morphemes[(size_t)chunkIds->back()].p;
			bTag = (size_t)tag;
		}

		KCondVowel vowel = morphemes[((size_t)chunkIds->at(0))].vowel;
		KCondPolarity polar = morphemes[((size_t)chunkIds->at(0))].polar;
		k_wstring conds[] = { KSTR("+"), KSTR("+Vowel"), KSTR("+Vocalic"), KSTR("+VocalicH") };
		k_wstring conds2[] = { KSTR("+"), KSTR("+Positive"), KSTR("-Positive")};
		auto pm = find(conds, conds + LEN_ARRAY(conds), fields[2]);
		if (pm < conds + LEN_ARRAY(conds))
		{
			vowel = (KCondVowel)(pm - conds + 1);
		}
		pm = find(conds2, conds2 + LEN_ARRAY(conds2), fields[2]);
		if (pm < conds2 + LEN_ARRAY(conds2))
		{
			polar = (KCondPolarity)(pm - conds2);
		}
		char combineSocket = 0;
		if (fields.size() >= 4 && !fields[3].empty())
		{
			combineSocket = stoi(fields[3]);
		}

		size_t mid = morphemes.size();
		auto& fm = formMapper(form);
		fm.candidate.emplace_back((KMorpheme*)mid);
		fm.suffix.insert(0);
		morphemes.emplace_back(form, KPOSTag::UNKNOWN, vowel, polar, ps, combineSocket);
		morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
		morphemes.back().chunks = chunkIds;
	}
	fclose(file);
#endif
}


void KModelMgr::loadPCMFromTxt(const char * filename, unordered_map<pair<k_string, KPOSTag>, size_t>& morphMap)
{
#ifdef LOAD_TXT
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw ios_base::failure{ string("Cannot open ") + filename };
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 5) continue;

		auto combs = split(fields[0], '+');
		auto org = combs[0] + combs[1];
		auto form = encodeJamo(combs[0].cbegin(), combs[0].cend());
		auto orgform = encodeJamo(org.cbegin(), org.cend());
		auto tag = makePOSTag(fields[1]);
		float tagWeight = stof(fields[2]);
		k_string suffixes = encodeJamo(fields[3].cbegin(), fields[3].cend());
		char socket = stoi(fields[4]);

		auto mit = morphMap.find(make_pair(orgform, tag));
		assert(mit != morphMap.end());
		size_t mid = morphemes.size();
		morphMap.emplace(make_pair(form, tag), mid);
		auto& fm = formMapper(form);
		fm.candidate.emplace_back((KMorpheme*)mid);
		fm.suffix.insert(suffixes.begin(), suffixes.end());
		morphemes.emplace_back(form, tag, KCondVowel::none, KCondPolarity::none, logf(tagWeight), socket);
		morphemes.back().kform = (const k_string*)(&fm - &forms[0]);
		morphemes.back().combined = (int)mit->second - ((int)morphemes.size() - 1);
	}
	fclose(file);
#endif
}

void KModelMgr::saveMorphBin(const char * filename) const
{
	FILE* f;
	if (fopen_s(&f, filename, "wb")) throw ios_base::failure{ string("Cannot open ") + filename };
	fwrite("KIWI", 1, 4, f);
	size_t s = forms.size();
	fwrite(&s, 1, 4, f);
	s = morphemes.size();
	fwrite(&s, 1, 4, f);

	auto mapper = [this](const KMorpheme* p)->size_t
	{
		return (size_t)p;
	};

	for (const auto& form : forms)
	{
		form.writeToBin(f, mapper);
	}
	for (const auto& morph : morphemes)
	{
		morph.writeToBin(f, mapper);
	}
	fclose(f);
}

void KModelMgr::loadMorphBin(const char * filename)
{
	FILE* f;
	if (fopen_s(&f, filename, "rb")) throw ios_base::failure{ string("Cannot open ") + filename };
	size_t formSize = 0, morphemeSize = 0;
	fread(&formSize, 1, 4, f);
	fread(&formSize, 1, 4, f);
	fread(&morphemeSize, 1, 4, f);
	
	forms.resize(formSize);
	morphemes.resize(morphemeSize);

	auto mapper = [this](size_t p)->const KMorpheme*
	{
		return (const KMorpheme*)p;
	};

	for (auto& form : forms)
	{
		form.readFromBin(f, mapper);
		formMap.emplace(form.form, formMap.size());
	}
	for (auto& morph : morphemes)
	{
		morph.readFromBin(f, mapper);
	}
	fclose(f);
}

#if defined(USE_DIST_MAP) && defined(LOAD_TXT)
void KModelMgr::loadDMFromTxt(const char * filename)
{
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw ios_base::failure{ string("Cannot open ") + filename };
	char buf[65536*4];
	wstring_convert<codecvt_utf8_utf16<k_wchar>, k_wchar> converter;

	auto parseFormTag = [](const k_wstring& f) -> pair<k_string, KPOSTag>
	{
		auto c = split(f, '/');
		if (c.size() < 2) return {};
		k_string str = encodeJamo(c[0].begin(), c[0].end());
		KPOSTag tag = makePOSTag(c[1]);
		return { str, tag };
	};

	auto findMorpheme = [this](const pair<k_string, KPOSTag>& m) -> KMorpheme*
	{
		auto form = getTrie()->search(&m.first[0], &m.first[0] + m.first.size());
		if (!form || form == (void*)-1) return nullptr;
		for (const auto& cand : form->candidate)
		{
			if (cand->tag == m.second) return (KMorpheme*)cand;
		}
		return nullptr;
	};

	while (fgets(buf, 65536*4, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 4) continue;
		auto tarMorpheme = findMorpheme(parseFormTag(fields[0]));
		if (!tarMorpheme) continue;
		for (size_t i = 2; i < fields.size(); i += 2)
		{
			auto m = findMorpheme(parseFormTag(fields[i]));
			if (!m) continue;
			float pmi = stof(fields[i + 1]);
			if (abs(pmi) < 3.f) continue;
			//pmi *= PMI_WEIGHT;
			tarMorpheme->addToDistMap(m, pmi);
			m->addToDistMap(tarMorpheme, pmi);
		}
	}
	fclose(file);
}
#endif

#ifdef USE_DIST_MAP
void KModelMgr::saveDMBin(const char * filename) const
{
	FILE* f;
	if (fopen_s(&f, filename, "wb")) throw ios_base::failure{ string("Cannot open ") + filename };
	fwrite("KIWI", 1, 4, f);
	size_t s = morphemes.size();
	fwrite(&s, 1, 4, f);

	for (const auto& morph : morphemes)
	{
		morph.writeDistMapToBin(f);
	}
	fclose(f);
}

void KModelMgr::loadDMBin(const char * filename)
{
	FILE* f;
	if (fopen_s(&f, filename, "rb")) throw ios_base::failure{ string("Cannot open ") + filename };
	size_t morphemeSize = 0;
	fread(&morphemeSize, 1, 4, f);
	fread(&morphemeSize, 1, 4, f);

	for (auto& morph : morphemes)
	{
		morph.readDistMapFromBin(f);
	}
	fclose(f);
}

#endif

KModelMgr::KModelMgr(const char * modelPath)
{
	this->modelPath = modelPath;
#ifdef LOAD_TXT
	unordered_map<pair<k_string, KPOSTag>, size_t> morphMap;
	loadPOSFromTxt((modelPath + k_string("pos.txt")).c_str());
	loadMMFromTxt((modelPath + k_string("fullmodel.txt")).c_str(), morphMap);
	loadCMFromTxt((modelPath + k_string("combined.txt")).c_str(), morphMap);
	loadPCMFromTxt((modelPath + k_string("precombined.txt")).c_str(), morphMap);
	savePOSBin((modelPath + k_string("pos.bin")).c_str());
	saveMorphBin((modelPath + k_string("fullmodel.bin")).c_str());
#else
	loadPOSBin((modelPath + k_string("pos.bin")).c_str());
	loadMorphBin((modelPath + k_string("fullmodel.bin")).c_str());
#endif
}

void KModelMgr::addUserWord(const k_string & form, KPOSTag tag)
{
#ifdef TRIE_ALLOC_ARRAY
	if (!form.empty()) extraTrieSize += form.size() - 1;
#else
#endif

	auto& f = formMapper(form);
	f.candidate.emplace_back((const KMorpheme*)morphemes.size());
	morphemes.emplace_back(form, tag);
}

void KModelMgr::addUserRule(const k_string & form, const vector<pair<k_string, KPOSTag>>& morphs)
{
#ifdef TRIE_ALLOC_ARRAY
	if (!form.empty()) extraTrieSize += form.size() - 1;
#else
#endif

	auto& f = formMapper(form);
	f.candidate.emplace_back((const KMorpheme*)morphemes.size());
	morphemes.emplace_back(form, KPOSTag::UNKNOWN);
	morphemes.back().chunks = new vector<const KMorpheme*>(morphs.size());
	iota(morphemes.back().chunks->begin(), morphemes.back().chunks->end(), (const KMorpheme*)morphemes.size());
	for (auto& m : morphs)
	{
		morphemes.emplace_back(m.first, m.second);
	}
}

void KModelMgr::solidify()
{
#ifdef TRIE_ALLOC_ARRAY
	trieRoot.reserve(152000 + extraTrieSize);
	trieRoot.emplace_back();
	for (auto& f : forms)
	{
		if (f.candidate.empty()) continue;
		trieRoot[0].build(f.form.c_str(), &f, [this]()
		{
			trieRoot.emplace_back();
			return &trieRoot.back();
		});
	}
	trieRoot[0].fillFail();
#else
	trieRoot = make_shared<KTrie>();
	for (auto& f : forms)
	{
		trieRoot->build(f.form.c_str(), &f);
	}
	trieRoot->fillFail();
#endif

	for (auto& f : morphemes)
	{
		f.wform = &forms[(size_t)f.kform].wform;
		f.kform = &forms[(size_t)f.kform].form;
		if (f.chunks) for (auto& p : *f.chunks) p = &morphemes[(size_t)p];
	}

	for (auto& f : forms)
	{
		for (auto& p : f.candidate) p = &morphemes[(size_t)p];
		f.updateCond();
	}
	formMap = {};

#if defined(USE_DIST_MAP) && defined(LOAD_TXT)
	loadDMFromTxt((modelPath + k_string("distModel.txt")).c_str());
	saveDMBin((modelPath + k_string("distModel.bin")).c_str());
#endif
#if defined(USE_DIST_MAP) && !defined(LOAD_TXT)
	loadDMBin((modelPath + k_string("distModel.bin")).c_str());
#endif
	for (auto& m : morphemes)
	{
		if (m.distMap) for (auto& p : *m.distMap) p.second *= PMI_WEIGHT;
	}
	/*FILE* out;
	fopen_s(&out, "dmTestBin.txt", "w");
	size_t n = 0;
	for (auto& m : morphemes)
	{
		n++;
		if (!m.distMap) continue;
		fprintf(out, "\n%zd\n", n);
		for (auto& i : *m.distMap)
		{
			fprintf(out, "%d\t%g\n", i.first, i.second);
		}
	}
	fclose(out);*/
}

float KModelMgr::getTransitionP(const KMorpheme * a, const KMorpheme * b) const
{
	size_t tagA = a ? (size_t)(!a->chunks ? a : a->chunks->back())->tag : 0;
	size_t tagB = b ? (size_t)(!b->chunks ? b : b->chunks->at(b->chunks->front()->tag == KPOSTag::V ? 1 : 0))->tag : 0;
	return posTransition[tagA][tagB];
}

float KModelMgr::getTransitionP(KPOSTag a, KPOSTag b) const
{
	return posTransition[(size_t)a][(size_t)b];
}

KPOSTag KModelMgr::findMaxiumTag(const KMorpheme * a, const KMorpheme * b) const
{
	KPOSTag tagA = KPOSTag::UNKNOWN;
	if (a && !a->chunks) tagA = a->tag;
	else if (a) tagA = a->chunks->back()->tag;

	if (!b) return maxiumBtwn[(size_t)tagA][0];
	if (!b->chunks) return maxiumBtwn[(size_t)tagA][(size_t)b->tag];
	//if (b->chunks.front()->tag == KPOSTag::V) return KPOSTag::UNKNOWN;
	return (b->chunks->front()->tag == KPOSTag::V ? maxiumVBtwn : maxiumBtwn)[(size_t)tagA][(size_t)b->chunks->at(b->chunks->front()->tag == KPOSTag::V ? 1 : 0)->tag];
}
