#include "stdafx.h"
#include "KForm.h"
#include "KModelMgr.h"
#include "KTrie.h"
#include "Utils.h"


template <>
class hash<pair<string, KPOSTag>> {
public:
	size_t operator() (const pair<string, KPOSTag>& o) const
	{
		return hash_value(o.first) ^ (size_t)o.second;
	};
};

void KModelMgr::loadPOSFromTxt(const char * filename)
{
	for (auto& f : posTransition) for (auto& g : f)
	{
		g = P_MIN;
	}
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
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
			auto p = _wtof(fields[2].c_str());
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
		for (size_t n = 1; n <= (size_t)KPOSTag::NR; n++)
		{
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
}

void KModelMgr::savePOSBin(const char * filename) const
{
	FILE* f;
	if (fopen_s(&f, filename, "wb")) throw exception();
	fwrite(posTransition, 1, sizeof(posTransition), f);
	fwrite(maxiumBtwn, 1, sizeof(maxiumBtwn), f);
	fwrite(maxiumVBtwn, 1, sizeof(maxiumVBtwn), f);
	fclose(f);
}

void KModelMgr::loadPOSBin(const char * filename)
{
	FILE* f;
	if (fopen_s(&f, filename, "rb")) throw exception();
	fread(posTransition, 1, sizeof(posTransition), f);
	fread(maxiumBtwn, 1, sizeof(maxiumBtwn), f);
	fread(maxiumVBtwn, 1, sizeof(maxiumVBtwn), f);
	fclose(f);
}

void KModelMgr::loadMMFromTxt(const char * filename, unordered_map<pair<string, KPOSTag>, size_t>& morphMap)
{
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 9) continue;

		auto form = encodeJamo(fields[0].cbegin(), fields[0].cend());
		auto tag = makePOSTag(fields[1]);
		float morphWeight = _wtof(fields[2].c_str());
		if (morphWeight < 10 && tag >= KPOSTag::JKS)
		{
			continue;
		}
		float tagWeight = _wtof(fields[3].c_str());
		float vowel = _wtof(fields[5].c_str());
		float vocalic = _wtof(fields[6].c_str());
		float vocalicH = _wtof(fields[7].c_str());
		float positive = _wtof(fields[8].c_str());

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
	}
	fclose(file);
}


void KModelMgr::loadCMFromTxt(const char * filename, unordered_map<pair<string, KPOSTag>, size_t>& morphMap)
{
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 2) continue;
		if (fields.size() == 2) fields.emplace_back();
		auto form = encodeJamo(fields[0].cbegin(), fields[0].cend());
		vector<const KMorpheme*> chunkIds;
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
				chunkIds.emplace_back((KMorpheme*)it->second);
			}
			else
			{
				size_t mid = morphemes.size();
				morphMap.emplace(make_pair(f, tag), mid);
				morphemes.emplace_back(f, tag);
				chunkIds.emplace_back((KMorpheme*)mid);
			}
			if (bTag == (size_t)KPOSTag::V)
			{
			}
			else if (bTag)
			{
				ps += posTransition[bTag][(size_t)tag];
			}
			ps += morphemes[(size_t)chunkIds.back()].p;
			bTag = (size_t)tag;
		}

		KCondVowel vowel = morphemes[((size_t)chunkIds[0])].vowel;
		KCondPolarity polar = morphemes[((size_t)chunkIds[0])].polar;
		wstring conds[] = { L"+", L"+Vowel", L"+Vocalic", L"+VocalicH" };
		wstring conds2[] = { L"+", L"+Positive", L"-Positive"};
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
		if (fields.size() >= 4)
		{
			combineSocket = _wtoi(fields[3].c_str());
		}

		size_t mid = morphemes.size();
		auto& fm = formMapper(form);
		fm.candidate.emplace_back((KMorpheme*)mid);
		fm.suffix.insert(0);
		morphemes.emplace_back(form, KPOSTag::UNKNOWN, vowel, polar, ps, combineSocket);
		morphemes.back().chunks = move(chunkIds);
	}
	fclose(file);
}


void KModelMgr::loadPCMFromTxt(const char * filename, unordered_map<pair<string, KPOSTag>, size_t>& morphMap)
{
	FILE* file;
	if (fopen_s(&file, filename, "r")) throw exception();
	char buf[2048];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	while (fgets(buf, 2048, file))
	{
		auto wstr = converter.from_bytes(buf);
		if (wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, '\t');
		if (fields.size() < 5) continue;

		auto combs = split(fields[0], '+');
		auto form = encodeJamo(combs[0].cbegin(), combs[0].cend());
		auto tag = makePOSTag(fields[1]);
		float tagWeight = _wtof(fields[2].c_str());
		string suffixes = encodeJamo(fields[3].cbegin(), fields[3].cend());
		char socket = _wtoi(fields[4].c_str());

		size_t mid = morphemes.size();
		morphMap.emplace(make_pair(form, tag), mid);
		auto& fm = formMapper(form);
		fm.candidate.emplace_back((KMorpheme*)mid);
		fm.suffix.insert(suffixes.begin(), suffixes.end());
		morphemes.emplace_back(form, tag, KCondVowel::none, KCondPolarity::none, logf(tagWeight), socket);
	}
	fclose(file);
}

void KModelMgr::saveMorphBin(const char * filename) const
{
	FILE* f;
	if (fopen_s(&f, filename, "wb")) throw exception();
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
	if (fopen_s(&f, filename, "rb")) throw exception();
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

KModelMgr::KModelMgr(const char * posFile, const char * morphemeFile, const char * combinedFile, const char* precombinedFile)
{
	/*
	unordered_map<pair<string, KPOSTag>, size_t> morphMap;
	if (posFile) loadPOSFromTxt(posFile);
	if (morphemeFile) loadMMFromTxt(morphemeFile, morphMap);
	if (combinedFile) loadCMFromTxt(combinedFile, morphMap);
	if (precombinedFile) loadPCMFromTxt(precombinedFile, morphMap);
	savePOSBin((posFile + string(".bin")).c_str());
	saveMorphBin((morphemeFile + string(".bin")).c_str());
	/*/
	loadPOSBin((posFile + string(".bin")).c_str());
	loadMorphBin((morphemeFile + string(".bin")).c_str());
	//*/
}

void KModelMgr::addUserWord(const string & form, KPOSTag tag)
{
#ifdef TRIE_ALLOC_ARRAY
	if (!form.empty()) extraTrieSize += form.size() - 1;
#else
#endif

	auto& f = formMapper(form);
	f.candidate.emplace_back((const KMorpheme*)morphemes.size());
	morphemes.emplace_back(form, tag);
}

void KModelMgr::addUserRule(const string & form, const vector<pair<string, KPOSTag>>& morphs)
{
#ifdef TRIE_ALLOC_ARRAY
	if (!form.empty()) extraTrieSize += form.size() - 1;
#else
#endif
}

void KModelMgr::solidify()
{
#ifdef TRIE_ALLOC_ARRAY
	trieRoot.reserve(150000 + extraTrieSize);
	trieRoot.emplace_back();
	for (auto& f : forms)
	{
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
		for (auto& p : f.chunks) p = &morphemes[(size_t)p];
	}

	for (auto& f : forms)
	{
		for (auto& p : f.candidate) p = &morphemes[(size_t)p];
		f.updateCond();
	}
	formMap = {};
}

float KModelMgr::getTransitionP(const KMorpheme * a, const KMorpheme * b) const
{
	size_t tagA = a ? (size_t)(a->chunks.empty() ? a : a->chunks.back())->tag : 0;
	size_t tagB = b ? (size_t)(b->chunks.empty() ? b : b->chunks[b->chunks[0]->tag == KPOSTag::V ? 1 : 0])->tag : 0;
	return posTransition[tagA][tagB];
}

float KModelMgr::getTransitionP(KPOSTag a, KPOSTag b) const
{
	return posTransition[(size_t)a][(size_t)b];
}

KPOSTag KModelMgr::findMaxiumTag(const KMorpheme * a, const KMorpheme * b) const
{
	KPOSTag tagA = KPOSTag::UNKNOWN;
	if (a && a->chunks.empty()) tagA = a->tag;
	else if (a) tagA = a->chunks.back()->tag;

	if (!b) return maxiumBtwn[(size_t)tagA][0];
	if (b->chunks.empty()) return maxiumBtwn[(size_t)tagA][(size_t)b->tag];
	//if (b->chunks.front()->tag == KPOSTag::V) return KPOSTag::UNKNOWN;
	return (b->chunks[0]->tag == KPOSTag::V ? maxiumVBtwn : maxiumBtwn)[(size_t)tagA][(size_t)b->chunks[b->chunks[0]->tag == KPOSTag::V ? 1 : 0]->tag];
}
