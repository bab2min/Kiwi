#include "stdafx.h"
#include "Kiwi.h"
#include "Utils.h"
#include "KFeatureTestor.h"

#define DIVIDE_BOUND 6


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
	return ret;
}

Kiwi::Kiwi(const char * modelPath, size_t _maxCache) : maxCache(_maxCache)
{
	mdl = make_shared<KModelMgr>(modelPath);
}

int Kiwi::addUserWord(const wstring & str, KPOSTag tag)
{
	if (!verifyHangul(str)) return -1;
	mdl->addUserWord(splitJamo(str), tag);
	return 0;
}

int Kiwi::addUserRule(const wstring & str, const vector<pair<wstring, KPOSTag>>& morph)
{
	if (!verifyHangul(str)) return -1;
	vector<pair<string, KPOSTag>> jmMorph;
	jmMorph.reserve(morph.size());
	for (auto& m : morph)
	{
		if (!verifyHangul(m.first)) return -1;
		jmMorph.emplace_back(splitJamo(m.first), m.second);
	}
	mdl->addUserRule(splitJamo(str), jmMorph);
	return 0;
}

int Kiwi::loadUserDictionary(const char * userDictPath)
{
	FILE* file = nullptr;
	if (fopen_s(&file, userDictPath, "r")) return -1;
	char buf[4096];
	wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
	while (fgets(buf, 4096, file))
	{
		if (buf[0] == '#') continue;
		auto wstr = converter.from_bytes(buf);
		auto chunks = split(wstr, '\t');
		if (chunks.size() < 2) continue;
		if (!chunks[1].empty()) 
		{
			auto pos = makePOSTag(chunks[1]);
			if (pos != KPOSTag::MAX)
			{
				addUserWord(chunks[0], pos);
				continue;
			}
		}
		
		vector<pair<wstring, KPOSTag>> morphs;
		for (size_t i = 1; i < chunks.size(); i++) 
		{
			auto cc = split(chunks[i], '/');
			if (cc.size() != 2) goto loopContinue;
			auto pos = makePOSTag(cc[1]);
			if (pos == KPOSTag::MAX) goto loopContinue;
			morphs.emplace_back(cc[0], pos);
		}
		addUserRule(chunks[0], morphs);
	loopContinue:;
	}
	fclose(file);
	return 0;
}

int Kiwi::prepare()
{
	mdl->solidify();
	kt = mdl->getTrie();
	return 0;
}

vector<char> getBacks(const vector<pair<vector<char>, float>>& cands)
{
	char buf[128] = { 0 };
	vector<char> ret;
	for (auto c : cands)
	{
		auto back = c.first.back();
		assert(back >= 0);
		if (!buf[back])
		{
			ret.emplace_back(back);
			buf[back] = 1;
		}
	}
	return ret;
}

KResult Kiwi::analyze(const wstring & str) const
{
	KResult ret;
	auto parts = splitPart(str);
	for (auto p : parts)
	{
		for (size_t cid = 0; cid < p.size(); cid++)
		{
			auto& c = p[cid];
			if (c.second != KPOSTag::MAX)
			{
				ret.first.emplace_back(c);
				continue;
			}
			auto jm = splitJamo(c.first);
			auto ar = analyzeJM(jm, 1, cid ? p[cid - 1].second : KPOSTag::UNKNOWN, cid+1 < p.size() ? p[cid + 1].second : KPOSTag::UNKNOWN);
			ret.first.insert(ret.first.end(), ar[0].first.begin(), ar[0].first.end());
			ret.second += ar[0].second;
		}
	}
	return ret;
}

vector<KResult> Kiwi::analyze(const wstring & str, size_t topN) const
{
	vector<KResult> cands(1);
	auto parts = splitPart(str);
	auto sortFunc = [](const auto& x, const auto& y)
	{
		return x.second > y.second;
	};
	for (auto p : parts)
	{
		for (size_t cid = 0; cid < p.size(); cid++)
		{
			auto& c = p[cid];
			if (c.second != KPOSTag::MAX)
			{
				for (auto& cand : cands)
				{
					cand.first.emplace_back(c);
				}
				continue;
			}
			auto jm = splitJamo(c.first);
			auto ar = analyzeJM(jm, topN, cid ? p[cid - 1].second : KPOSTag::UNKNOWN, cid + 1 < p.size() ? p[cid + 1].second : KPOSTag::UNKNOWN);
			vector<tuple<short, short, float>> probs;
			for (size_t i = 0; i < cands.size(); i++) for (size_t j = 0; j < ar.size(); j++)
			{
				probs.emplace_back((short)i, (short)j, cands[i].second + ar[j].second);
			}
			sort(probs.begin(), probs.end(), [](const auto& x, const auto& y)
			{
				return get<float>(x) > get<float>(y);
			});
			vector<KResult> newCands;
			newCands.reserve(min(topN, probs.size()));
			for_each(probs.begin(), probs.begin() + min(topN, probs.size()), [&newCands, &cands, &ar](auto p)
			{
				const auto& arp = ar[get<1>(p)];
				newCands.emplace_back(cands[get<0>(p)]);
				newCands.back().first.insert(newCands.back().first.end(), arp.first.begin(), arp.first.end());
				newCands.back().second += arp.second;
			});
			cands = newCands;
		}
	}
	return cands;
}

void Kiwi::clearCache()
{
	analyzedCache = {};
}

vector<const KChunk*> Kiwi::divideChunk(const vector<KChunk>& ch)
{
	vector<const KChunk*> ret;
	const KChunk* s = &ch[0];
	size_t n = 1;
	for (auto& c : ch)
	{
		if (n >= DIVIDE_BOUND && &c > s + 1)
		{
			ret.emplace_back(s);
			s = &c;
			n = 1;
		}
		if (c.isStr()) continue;
		n *= c.form->candidate.size();
	}
	if (s != &ch[0] + ch.size())
	{
		ret.emplace_back(s);
	}
	return ret;
}

vector<vector<pair<vector<char>, float>>> Kiwi::calcProbabilities(const KChunk * pre, const KChunk * ch, const KChunk * end, const char* ostr, size_t len) const
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

	vector<vector<pair<vector<char>, float>>> ret(pre->getCandSize());
	vector<char> idx(end - ch + 1);
	float minThreshold = P_MIN;
	while (1)
	{
		float ps = 0;
		KMorpheme tmpMorpheme;
		const KMorpheme* beforeMorpheme = pre->getMorpheme(idx[0], &tmpMorpheme, ostr);
		for (size_t i = 1; i < idx.size(); i++)
		{
			auto curMorpheme = ch[i - 1].getMorpheme(idx[i], &tmpMorpheme, ostr);
			if (!curMorpheme || ( KPOSTag::SF <= curMorpheme->tag && curMorpheme->tag <= KPOSTag::SN))
			{
				ps += mdl->getTransitionP(beforeMorpheme, curMorpheme);
				beforeMorpheme = curMorpheme;
				continue;
			}
			// if is unknown morpheme
			if (curMorpheme->chunks.empty() && !curMorpheme->form.empty() && curMorpheme->tag == KPOSTag::UNKNOWN)
			{
				tmpMorpheme.tag = mdl->findMaxiumTag(beforeMorpheme, i + 1 < idx.size() ? ch[i].getMorpheme(idx[i + 1], nullptr, nullptr) : nullptr);
			}

			if (beforeMorpheme->combineSocket && beforeMorpheme->tag != KPOSTag::UNKNOWN && beforeMorpheme->combineSocket != curMorpheme->combineSocket)
			{
				goto next;
			}
			if (curMorpheme->combineSocket && curMorpheme->tag == KPOSTag::UNKNOWN && beforeMorpheme->combineSocket != curMorpheme->combineSocket)
			{
				goto next;
			}

			if (beforeMorpheme->form.size())
			{
				const char* bBegin = &beforeMorpheme->form[0];
				const char* bEnd = bBegin + beforeMorpheme->form.size();
				if ((int)curMorpheme->vowel && !vowelFunc[(int)curMorpheme->vowel - 1](bBegin, bEnd)) goto next;
				if ((int)curMorpheme->polar && !polarFunc[(int)curMorpheme->polar - 1](bBegin, bEnd)) goto next;
			}
			ps += mdl->getTransitionP(beforeMorpheme, curMorpheme);
			ps += curMorpheme->p;
			beforeMorpheme = curMorpheme;
			if (ps <= minThreshold) goto next;
		}
		ret[idx[0]].emplace_back(vector<char>{ idx.begin() + 1, idx.end() }, ps);
	next:;

		idx[0]++;
		for (size_t i = 0; i < idx.size(); i++)
		{
			if (i ? (idx[i] >= ch[i - 1].getCandSize()) : (idx[i] >= pre->getCandSize()))
			{
				idx[i] = 0;
				if (i + 1 >= idx.size()) goto exit;
				idx[i + 1]++;
			}
		}
	}
exit:;
	return ret;
}

vector<KResult> Kiwi::analyzeJM(const string & jm, size_t topN, KPOSTag prefix, KPOSTag suffix) const
{
	string cfind = jm;
	cfind.push_back((char)prefix + 64);
	cfind.push_back((char)suffix + 64);
	auto cit = analyzedCache.find(cfind);
	if (cit != analyzedCache.end()) return cit->second;

	vector<KResult> ret;
	auto sortFunc = [](const auto& x, const auto& y)
	{
		return x.second > y.second;
	};
	topN = max((size_t)3, topN);
	vector<pair<vector<pair<string, KPOSTag>>, float>> cands;
	KMorpheme preMorpheme{ "", prefix }, sufMorpheme{ "", suffix };
	KForm preForm, sufForm;
	preForm.candidate.emplace_back(&preMorpheme);
	sufForm.candidate.emplace_back(&sufMorpheme);
	KChunk preTemp{ &preForm }, sufTemp{ &sufForm };

	auto chunks = kt->split(jm, prefix != KPOSTag::UNKNOWN);
	for (auto& s : chunks)
	{
#ifdef _DEBUG
		for (auto& t : s)
		{
			printJM(t, &jm[0]);
			printf(", ");
		}
		printf("\n");
#endif

		if (suffix != KPOSTag::UNKNOWN)
		{
			s.push_back(sufTemp);
		}
		else
		{
			s.emplace_back(0, 0);
		}
		auto chunks = divideChunk(s);
		chunks.emplace_back(&s[0] + s.size());
		const KChunk* pre = &preTemp;
		vector<pair<vector<char>, float>> pbFinal;

		for (size_t i = 1; i < chunks.size(); i++)
		{
			auto pb = calcProbabilities(pre, chunks[i - 1], chunks[i], &jm[0], jm.size());
			if (i == 1)
			{
				sort(pb[0].begin(), pb[0].end(), sortFunc);
				pbFinal.insert(pbFinal.end(), pb[0].begin(), pb[0].begin() + min(topN, pb[0].size()));
			}
			else
			{
				for (auto b : getBacks(pbFinal))
				{
					sort(pb[b].begin(), pb[b].end(), sortFunc);
				}
				vector<pair<vector<char>, float>> tmpFinal;
				for (auto cand : pbFinal)
				{
					auto& pbBack = pb[cand.first.back()];
					for (size_t n = 0; n < topN && n < pbBack.size(); n++)
					{
						auto newP = cand.second + pbBack[n].second;
						if (newP <= P_MIN) continue;
						tmpFinal.emplace_back(cand.first, newP);
						tmpFinal.back().first.insert(tmpFinal.back().first.end(), pbBack[n].first.begin(), pbBack[n].first.end());
					}
				}
				sort(tmpFinal.begin(), tmpFinal.end(), sortFunc);
				pbFinal = { tmpFinal.begin(), tmpFinal.begin() + min(topN, tmpFinal.size()) };
			}
			pre = chunks[i] - 1;
		}
		for (auto pb : pbFinal)
		{
			cands.emplace_back();
			cands.back().second = pb.second;
			size_t n = 0;
			pb.first.pop_back();
			for (auto pbc : pb.first)
			{
				auto morpheme = s[n++].getMorpheme(pbc, &preMorpheme, &jm[0]);
				//if (!morpheme) continue;
				// If is simple morpheme
				if (morpheme->chunks.empty())
				{
					cands.back().first.emplace_back(morpheme->form, morpheme->tag);
				}
				// complex morpheme
				else for (auto pbcc : morpheme->chunks)
				{
					// if post morpheme must be combined
					if (pbcc->tag == KPOSTag::V)
					{
						cands.back().first.back().first += pbcc->form;
					}
					else cands.back().first.emplace_back(pbcc->form, pbcc->tag);
				}
			}
		}
	}

	if (cands.empty())
	{
		ret.emplace_back();
		ret.back().first.emplace_back(joinJamo(jm), KPOSTag::UNKNOWN);
		ret.back().second = P_MIN;
	}
	else
	{
		sort(cands.begin(), cands.end(), sortFunc);
		transform(cands.begin(), cands.begin() + min(topN, cands.size()), back_inserter(ret), [](const pair<vector<pair<string, KPOSTag>>, float>& e)
		{
			KResult kr;
			for (auto& i : e.first)
			{
				kr.first.emplace_back(joinJamo(i.first), i.second);
			}
			kr.second = e.second;
			return kr;
		});
	}
	if(analyzedCache.size() < maxCache)	analyzedCache.emplace(cfind, ret);
	return ret;
}
