#include "stdafx.h"
#include "Kiwi.h"
#include "Utils.h"
#include "KFeatureTestor.h"

KPOSTag Kiwi::identifySpecialChr(wchar_t chr)
{
	if (isspace(chr)) return KPOSTag::UNKNOWN;
	if (isdigit(chr)) return KPOSTag::SN;
	if (('A' <= chr && chr <= 'Z') ||
		('a' <= chr && chr <= 'z'))  return KPOSTag::SL;
	if (0xac00 <= chr && chr < 0xd7a4) return KPOSTag::MAX;
	switch (chr)
	{
	case '.':
	case '!':
	case '?':
		return KPOSTag::SF;
	case '-':
	case '~':
	case 0x223c:
		return KPOSTag::SO;
	case 0x2026:
		return KPOSTag::SE;
	case ',':
	case ';':
	case ':':
	case '/':
	case 0xb7:
		return KPOSTag::SP;
	case '"':
	case '\'':
	case '(':
	case ')':
	case '<':
	case '>':
	case '[':
	case ']':
	case '{':
	case '}':
	case 0xad:
	case 0x2015:
	case 0x2018:
	case 0x2019:
	case 0x201c:
	case 0x201d:
	case 0x226a:
	case 0x226b:
	case 0x2500:
	case 0x3008:
	case 0x3009:
	case 0x300a:
	case 0x300b:
	case 0x300c:
	case 0x300d:
	case 0x300e:
	case 0x300f:
	case 0x3010:
	case 0x3011:
	case 0x3014:
	case 0x3015:
	case 0xff0d:
		return KPOSTag::SS;
	}
	if ((0x2e80 <= chr && chr <= 0x2e99) ||
		(0x2e9b <= chr && chr <= 0x2ef3) ||
		(0x2f00 <= chr && chr <= 0x2fd5) ||
		(0x3005 <= chr && chr <= 0x3007) ||
		(0x3021 <= chr && chr <= 0x3029) ||
		(0x3038 <= chr && chr <= 0x303b) ||
		(0x3400 <= chr && chr <= 0x4db5) ||
		(0x4e00 <= chr && chr <= 0x9fcc) ||
		(0xf900 <= chr && chr <= 0xfa6d) ||
		(0xfa70 <= chr && chr <= 0xfad9)) return KPOSTag::SH;
	if (0xd800 <= chr && chr <= 0xdfff) return KPOSTag::SH;
	return KPOSTag::SW;
}

vector<vector<KWordPair>> Kiwi::splitPart(const wstring & str)
{
	vector<vector<KWordPair>> ret;
	ret.emplace_back();
	for (auto c : str)
	{
		auto tag = identifySpecialChr(c);
		if (tag == KPOSTag::UNKNOWN)
		{
			if(!ret.back().empty()) ret.emplace_back();
			continue;
		}
		if (ret.back().empty())
		{
			ret.back().emplace_back(wstring{ &c, 1 }, tag);
			continue;
		}
		if ((ret.back().back().second == KPOSTag::SN && c == '.') ||
			ret.back().back().second == tag)
		{
			ret.back().back().first.push_back(c);
			continue;
		}
		if (ret.back().back().second == KPOSTag::SF) ret.emplace_back();
		ret.back().emplace_back(wstring{ &c , 1 }, tag);
	}
	return move(ret);
}

Kiwi::Kiwi(const char * modelPath)
{
	mdl = make_shared<KModelMgr>("../ModelGenerator/pos.txt", "../ModelGenerator/fullmodel.txt", "../ModelGenerator/combined.txt", "../ModelGenerator/precombined.txt");
}

int Kiwi::loadUserDictionary(const char * userDictPath)
{
	return 0;
}

int Kiwi::prepare()
{
	mdl->solidify();
	kt = mdl->makeTrie();
	return 0;
}


KResult Kiwi::analyze(const wstring & str) const
{
	KResult ret;
	auto parts = splitPart(str);
	for (auto p : parts)
	{
		int cid = 0;
		for (auto c : p)
		{
			if (c.second != KPOSTag::MAX)
			{
				ret.first.emplace_back(c);
				cid++;
				continue;
			}
			auto jm = splitJamo(c.first);
			auto chunks = kt->split(jm, !!cid);
			vector<pair<vector<pair<string, KPOSTag>>, float>> cands;
			for (auto s : chunks)
			{
#ifdef _DEBUG
				for (auto t : s)
				{
					printJM(t, &jm[0]);
					printf(", ");
				}
				printf("\n");
#endif
				enumPossible(cid ? p[cid-1].second : KPOSTag::UNKNOWN, s, &jm[0], jm.size(), cands);
			}

#ifdef _DEBUG
			sort(cands.begin(), cands.end(), [](const pair<vector<pair<string, KPOSTag>>, float>& a, const pair<vector<pair<string, KPOSTag>>, float>& b)
			{
				return a.second > b.second;
			});
			int n = 0;
			for (auto ic : cands)
			{
				printf("%03d\t%g\t", n++, ic.second);
				for (auto d : ic.first)
				{
					printJM(d.first);
					printf("/");
					printf(tagToString(d.second));
					printf(" + ");
				}
				printf("\n");
			}
			printf("\n\n");
#endif // DEBUG
			if (cands.empty())
			{
				ret.first.emplace_back(c.first, KPOSTag::UNKNOWN);
			}
			else
			{
				auto m = max_element(cands.begin(), cands.end(), [](const pair<vector<pair<string, KPOSTag>>, float>& a, const pair<vector<pair<string, KPOSTag>>, float>& b)
				{
					return a.second < b.second;
				});
				ret.second += m->second;
				for (auto r : m->first)
				{
					ret.first.emplace_back(joinJamo(r.first), r.second);
				}
			}
			cid++;
		}
	}
	return ret;
}

void Kiwi::enumPossible(KPOSTag prefixTag, const vector<KChunk>& ch, const char* ostr, size_t len, vector<pair<vector<pair<string, KPOSTag>>, float>>& ret) const
{
	static bool(*vowelFunc[])(const char*, const char*) = {
		KFeatureTestor::isPostposition,
		KFeatureTestor::isVowel,
		KFeatureTestor::isVocalic,
		KFeatureTestor::isVocalicH,
		KFeatureTestor::notVowel,
		KFeatureTestor::notVocalic,
		KFeatureTestor::notVocalicH,
	};
	static bool(*polarFunc[])(const char*, const char*) = {
		KFeatureTestor::isPositive,
		KFeatureTestor::notPositive
	};

	vector<size_t> idx(ch.size());
	string tmpChr;
	float minThreshold = len * -1.5f - 15.f;
	while (1)
	{
		const KMorpheme* before = nullptr;
		KPOSTag bfTag = prefixTag;
		float ps = 0;
		vector<pair<string, KPOSTag>> mj;
		for (size_t i = 0; i < idx.size(); i++)
		{
			auto chi = ch[i];
			if (chi.isStr())
			{
				auto curTag = mdl->findMaxiumTag(before, i + 1 < idx.size() && !ch[i + 1].isStr() ? ch[i + 1].form->candidate[idx[i + 1]] : nullptr);
				assert(curTag < KPOSTag::MAX);
				ps += powf(chi.end - chi.begin, 1.f) * -1.5f - 6.f;
				if (curTag == KPOSTag::VV || curTag == KPOSTag::VA) ps += -5.f;
				ps += mdl->getTransitionP(bfTag, curTag);
				bfTag = curTag;
				if (ps < minThreshold) goto next;
				mj.emplace_back(string(ostr + chi.begin, ostr + chi.end), bfTag);
				before = nullptr;
				continue;
			}
			auto& c = chi.form->candidate[idx[i]];
			ps += c->p;
			const char* bBegin = nullptr;
			const char* bEnd = nullptr;
			if (!mj.empty())
			{
				bBegin = &mj.back().first[0];
				bEnd = bBegin + mj.back().first.size();
			}
			if (bEnd == 0 && prefixTag == KPOSTag::UNKNOWN)
			{
				if ((int)c->vowel && !vowelFunc[(int)c->vowel - 1](bBegin, bEnd)) goto next;
				if ((int)c->polar && !polarFunc[(int)c->polar - 1](bBegin, bEnd)) goto next;
			}
			if (c->chunks.empty())
			{
				if (!KFeatureTestor::isCorrectEnd(bBegin, bEnd)) goto next;
				if (before && before->tag != KPOSTag::UNKNOWN && before->combineSocket) goto next;
				mj.emplace_back(c->form, c->tag);
				ps += mdl->getTransitionP(bfTag, c->tag);
				bfTag = c->tag;
			}
			else
			{
				size_t x = 0;
				if (!mj.empty() && c->chunks[0]->tag == KPOSTag::V)
				{
					if (before && before->combineSocket != c->combineSocket) goto next;
					mj.back().first += c->chunks[0]->form;
					x++;
				}

				if (!mj.empty() && !KFeatureTestor::isCorrectEnd(&mj.back().first[0], &mj.back().first[0] + mj.back().first.size())) goto next;
				for (; x < c->chunks.size(); x++)
				{
					auto& ch = c->chunks[x];
					mj.emplace_back(ch->form, ch->tag);
				}
				ps += mdl->getTransitionP(bfTag, c->chunks[c->chunks[0]->tag == KPOSTag::V ? 1 : 0]->tag);
				bfTag = c->chunks.back()->tag;
			}
			if (ps < minThreshold) goto next;
			before = c;
		}
		if (before && before->tag != KPOSTag::UNKNOWN && before->combineSocket) goto next;
		ps += mdl->getTransitionP(bfTag, KPOSTag::UNKNOWN);
		if (ps < minThreshold) goto next;
		ret.emplace_back(move(mj), ps);
	next:;

		idx[0]++;
		for (size_t i = 0; i < idx.size(); i++)
		{
			if (idx[i] >= (ch[i].isStr() ? 1 : ch[i].form->candidate.size()))
			{
				idx[i] = 0;
				if (i + 1 >= idx.size()) goto exit;
				idx[i + 1]++;
			}
		}
	}
exit:;
}