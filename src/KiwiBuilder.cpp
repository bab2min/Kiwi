#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>
#include "KTrie.h"
#include "FrozenTrie.hpp"
#include "Knlm.hpp"
#include "serializer.hpp"
#include "count.hpp"

using namespace std;

namespace std
{
	template <>
	class hash<pair<kiwi::KString, kiwi::POSTag>> 
	{
	public:
		size_t operator() (const pair<kiwi::KString, kiwi::POSTag>& o) const
		{
			return hash<kiwi::KString>{}(o.first) ^ (size_t)o.second;
		};
	};

	template <>
	class hash<kiwi::POSTag>
	{
	public:
		size_t operator() (const kiwi::POSTag& o) const
		{
			return (size_t)o;
		};
	};
}

namespace kiwi
{
	FormCond::FormCond() = default;

	FormCond::~FormCond() = default;

	FormCond::FormCond(const FormCond&) = default;

	FormCond::FormCond(FormCond&&) = default;

	FormCond& FormCond::operator=(const FormCond&) = default;

	FormCond& FormCond::operator=(FormCond&&) = default;

	FormCond::FormCond(const KString& _form, CondVowel _vowel, CondPolarity _polar)
		: form{ _form }, vowel{ _vowel }, polar{ _polar }
	{
	}

	bool FormCond::operator==(const FormCond& o) const
	{
		return form == o.form && vowel == o.vowel && polar == o.polar;
	}

	bool FormCond::operator!=(const FormCond& o) const
	{
		return !operator==(o);
	}

	KiwiBuilder::KiwiBuilder() = default;

	KiwiBuilder::~KiwiBuilder() = default;

	KiwiBuilder::KiwiBuilder(const KiwiBuilder&) = default;

	KiwiBuilder::KiwiBuilder(KiwiBuilder&&) = default;

	KiwiBuilder& KiwiBuilder::operator=(const KiwiBuilder&) = default;

	KiwiBuilder& KiwiBuilder::operator=(KiwiBuilder&&) = default;

	void KiwiBuilder::loadMMFromTxt(std::istream&& is, MorphemeMap& morphMap, std::unordered_map<POSTag, float>* posWeightSum, const function<bool(float, POSTag)>& selector)
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
			auto& fm = addForm(form, cvowel, polar);
			bool unified = false;
			if (tag >= POSTag::ep && tag <= POSTag::etm && form[0] == u'아')
			{
				form[0] = u'어';
				unified = true;
			}
			auto it = morphMap.find(make_pair(form, tag));
			if (it != morphMap.end())
			{
				fm.candidate.emplace_back(it->second);
				if (!unified) morphemes[it->second].kform = &fm - &forms[0];
			}
			else
			{
				size_t mid = morphemes.size();
				morphMap.emplace(make_pair(form, tag), mid);
				fm.candidate.emplace_back(mid);
				morphemes.emplace_back(tag, cvowel, polar);
				morphemes.back().kform = &fm - &forms[0];
				morphemes.back().userScore = morphWeight;
			}
		}
	}


	void KiwiBuilder::loadCMFromTxt(std::istream&& is, MorphemeMap& morphMap)
	{
		static std::array<const kchar_t*, 4> conds = { u"+", u"-Coda", u"+", u"+" };
		static std::array<const kchar_t*, 3> conds2 = { u"+", u"+Positive", u"-Positive" };

		string line;
		while (getline(is, line))
		{
			auto wstr = utf8To16(line);
			if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
			auto fields = split(wstr, u'\t');
			if (fields.size() < 2) continue;
			if (fields.size() == 2) fields.emplace_back();
			auto form = normalizeHangul({ fields[0].begin(), fields[0].end() });
			Vector<uint32_t> chunkIds;
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
					chunkIds.emplace_back(it->second);
				}
				else if (tag == POSTag::v)
				{
					size_t mid = morphemes.size();
					morphMap.emplace(make_pair(f, tag), mid);
					auto& fm = addForm(f, CondVowel::none, CondPolarity::none);
					morphemes.emplace_back(tag);
					morphemes.back().kform = &fm - &forms[0];
					chunkIds.emplace_back(mid);
				}
				else
				{
					goto continueFor;
				}
				bTag = (size_t)tag;
			}

			{
				CondVowel vowel = morphemes[chunkIds[0]].vowel;
				CondPolarity polar = morphemes[chunkIds[0]].polar;
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
				auto& fm = addForm(form, vowel, polar);
				fm.candidate.emplace_back(mid);
				morphemes.emplace_back(POSTag::unknown, vowel, polar, combineSocket);
				morphemes.back().kform = (&fm - &forms[0]);
				morphemes.back().chunks = move(chunkIds);
			}
		continueFor:;
		}
	}

	void KiwiBuilder::loadPCMFromTxt(std::istream&& is, MorphemeMap& morphMap)
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
				auto& fm = addForm(form, CondVowel::none, CondPolarity::none);
				fm.candidate.emplace_back(mid);
				morphemes.emplace_back(tag, CondVowel::none, CondPolarity::none, socket);
				morphemes.back().kform = &fm - &forms[0];
				morphemes.back().combined = (int)mit->second - ((int)morphemes.size() - 1);
			}
		}
	}

	void KiwiBuilder::addCorpusTo(Vector<Vector<uint16_t>>& out, std::istream&& is, MorphemeMap& morphMap)
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

				/*if ((f[0] == u'아' || f[0] == u'야') && fields[i + 1][0] == 'E')
				{
					if (f[0] == u'아') f[0] = u'어';
					else f[0] = u'여';
				}*/

				auto it = morphMap.find(make_pair(f, t));
				if (it == morphMap.end() || !morphemes[it->second].chunks.empty() || morphemes[it->second].combineSocket)
				{
					if (t <= POSTag::sn && t != POSTag::unknown)
					{
						wids.emplace_back(getDefaultMorphemeId(t));
					}
					else
					{
						wids.emplace_back((size_t)POSTag::nnp);
					}
				}
				else
				{
					wids.emplace_back(it->second);
				}
			}
		}
	}

	void KiwiBuilder::updateForms()
	{
		vector<pair<FormRaw, size_t>> formOrder;
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
			m.kform = newIdcs[m.kform];
		}
	}

	void KiwiBuilder::updateMorphemes()
	{
		for (auto& m : morphemes)
		{
			if (m.tag == POSTag::v || (&m - morphemes.data() + m.combined) < langMdl->getHeader().vocab_size)
			{
				m.lmMorphemeId = &m - morphemes.data();
			}
			else
			{
				m.lmMorphemeId = getDefaultMorphemeId(m.tag);
			}
		}
	}

	void KiwiBuilder::loadMorphBin(std::istream& is)
	{
		serializer::readMany(is, serializer::toKey("KIWI"), forms, morphemes);
		size_t cnt = 0;
		for (auto& form : forms)
		{
			formMap.emplace(FormCond{ form.form, form.vowel, form.polar }, cnt++);
		}
	}

	void KiwiBuilder::saveMorphBin(std::ostream& os) const
	{
		serializer::writeMany(os, serializer::toKey("KIWI"), forms, morphemes);
	}

	KiwiBuilder::KiwiBuilder(const string& modelPath, size_t _numThreads, BuildOption _options) 
		: detector{ modelPath, _numThreads }, options{ _options }, numThreads{ _numThreads ? _numThreads : thread::hardware_concurrency() }
	{
		{
			utils::MMap mm{ modelPath + "/sj.morph" };
			utils::imstream iss{ mm };
			loadMorphBin(iss);
		}
		langMdl = lm::KnLangModelBase::create(utils::MMap(modelPath + string{ "/sj.knlm" }));
		updateMorphemes();
		if (!!(options & BuildOption::loadDefaultDict))
		{
			loadDictionary(modelPath + "/default.dict");
		}
	}

	KiwiBuilder::KiwiBuilder(FromRawData, const std::string& rawDataPath, size_t _numThreads, BuildOption _options)
		: detector{ WordDetector::fromRawDataTag, rawDataPath, _numThreads }, options{ _options }, numThreads{ _numThreads ? _numThreads : thread::hardware_concurrency() }
	{
		forms.resize(defaultTagSize);
		morphemes.resize(defaultTagSize + 2); // additional places for <s> & </s>
		for (size_t i = 0; i < defaultTagSize; ++i)
		{
			forms[i].candidate.emplace_back(i + 2);
			morphemes[i + 2].tag = (POSTag)(i + 1);
		}

		size_t morphIdToUpdate = morphemes.size();
		MorphemeMap morphMap;
		loadMMFromTxt(ifstream{ rawDataPath + "/fullmodelV2.txt" }, morphMap, nullptr, [](float morphWeight, POSTag tag) {
			return morphWeight >= (tag < POSTag::jks ? 10 : 10);
		});
		for (size_t i = morphIdToUpdate; i < morphemes.size(); ++i)
		{
			morphemes[i].userScore = 0;
		}

		morphIdToUpdate = morphemes.size();
		auto realMorph = morphMap;
		unordered_map<POSTag, float> unknownWeight;
		loadMMFromTxt(ifstream{ rawDataPath + "/fullmodelV2.txt" }, morphMap, &unknownWeight, [](float morphWeight, POSTag tag) {
			return tag < POSTag::jks && morphWeight < 10;
		});
		for (size_t i = morphIdToUpdate; i < morphemes.size(); ++i)
		{
			morphemes[i].userScore = log(morphemes[i].userScore / unknownWeight[morphemes[i].tag]);
		}

		loadCMFromTxt(ifstream{ rawDataPath + "/combinedV2.txt" }, morphMap);
		loadPCMFromTxt(ifstream{ rawDataPath + "/precombinedV2.txt" }, morphMap);
		updateForms();
		//

		Vector<Vector<uint16_t>> sents;
		addCorpusTo(sents, ifstream{ rawDataPath + "/ML_lit.txt" }, realMorph);
		addCorpusTo(sents, ifstream{ rawDataPath + "/ML_spo.txt" }, realMorph);
		vector<pair<uint16_t, uint16_t>> bigramList;
		constexpr size_t order = 3, minCnt = 1, lastMinCnt = 1;
		auto cntNodes = utils::count(sents.begin(), sents.end(), minCnt, 1, order, nullptr, &bigramList);
		langMdl = lm::KnLangModelBase::create(lm::KnLangModelBase::build(cntNodes, order, minCnt, lastMinCnt, 2, 0, 1, 1e-5, 8, false, &bigramList));
		updateMorphemes();
		if (!!(options & BuildOption::loadDefaultDict))
		{
			loadDictionary(rawDataPath + "/default.dict");
		}
	}

	void KiwiBuilder::saveModel(const string& modelPath) const
	{
		{
			ofstream ofs{ modelPath + "/sj.morph", ios_base::binary };
			saveMorphBin(ofs);
		}
		{
			auto mem = langMdl->getMemory();
			ofstream ofs{ modelPath + "/sj.knlm", ios_base::binary };
			ofs.write((const char*)mem.get(), mem.size());
		}
	}

	FormRaw& KiwiBuilder::addForm(KString form, CondVowel vowel, CondPolarity polar)
	{
		FormCond fc{ form, vowel, polar };
		auto ret = formMap.emplace(fc, forms.size());
		if (ret.second)
		{
			forms.emplace_back(form, vowel, polar);
		}
		return forms[ret.first->second];
	}

	bool KiwiBuilder::addWord(const std::u16string& newForm, POSTag tag, float score, size_t origMorphemeId)
	{
		if (newForm.empty()) return false;

		auto normalizedForm = normalizeHangul({ newForm.begin(), newForm.end() });
		auto& f = addForm(normalizedForm, CondVowel::none, CondPolarity::none);
		if (f.candidate.empty())
		{
		}
		else
		{
			for (auto p : f.candidate)
			{
				// if `form` already has the same `tag`, skip adding
				if (morphemes[(size_t)p].tag == tag) return false;
			}
		}

		f.candidate.emplace_back(morphemes.size());
		morphemes.emplace_back(tag);
		morphemes.back().kform = &f - &forms[0];
		morphemes.back().userScore = score;
		morphemes.back().lmMorphemeId = origMorphemeId;
		return true;
	}

	bool KiwiBuilder::addWord(const std::u16string& form, POSTag tag, float score)
	{
		return addWord(form, tag, score, getDefaultMorphemeId(tag));
	}

	bool KiwiBuilder::addWord(const std::u16string& newForm, POSTag tag, float score, const std::u16string& origForm)
	{
		auto normalizedOrigForm = normalizeHangul({ origForm.begin(), origForm.end() });

		FormCond fc{ normalizedOrigForm, CondVowel::none, CondPolarity::none };
		auto origIt = formMap.find(fc);
		if (origIt == formMap.end())
		{
			throw UnknownMorphemeException{ "cannot find the original morpheme " + utf16To8(origForm) + "/" + tagToString(tag) };
		}

		size_t origMorphemeId = -1;
		for (auto p : forms[origIt->second].candidate)
		{
			if (morphemes[(size_t)p].tag == tag)
			{
				origMorphemeId = p;
				break;
			}
		}
		if (origMorphemeId == -1)
		{
			throw UnknownMorphemeException{ "cannot find the original morpheme " + utf16To8(origForm) + "/" + tagToString(tag) };
		}

		return addWord(newForm, tag, score, origMorphemeId);
	}

	size_t KiwiBuilder::loadDictionary(const std::string& dictPath)
	{
		size_t addedCnt = 0;
		ifstream ifs{ dictPath };
		if (!ifs) throw Exception("[loadUserDictionary] Failed to open '" + dictPath + "'");
		string line;
		while (getline(ifs, line))
		{
			auto wstr = utf8To16(line);
			if (wstr[0] == u'#') continue;
			auto fields = split(wstr, u'\t');
			if (fields.size() < 2) continue;

			while (!fields[1].empty() && 
				kiwi::identifySpecialChr(fields[1].back()) == POSTag::unknown
			) fields[1].pop_back();

			if (!fields[1].empty())
			{
				auto pos = toPOSTag(fields[1]);
				float score = 0.f;
				if (fields.size() > 2) score = stof(fields[2].begin(), fields[2].end());
				if (pos == POSTag::max)
				{
					throw Exception("[loadUserDictionary] Unknown Tag '" + utf16To8(fields[1]) + "'");
				}

				addedCnt += addWord(fields[0], pos, score) ? 1 : 0;
			}
		}
		return addedCnt;
	}

	Kiwi KiwiBuilder::build(ArchType arch) const
	{
		Kiwi ret{ arch, langMdl->getHeader().key_size };
		ret.forms.reserve(forms.size() + 1);
		ret.morphemes.reserve(morphemes.size());
		ret.langMdl = langMdl;
		ret.integrateAllomorph = !!(options & BuildOption::integrateAllomorph);
		if (numThreads > 1)
		{
			ret.pool = make_unique<utils::ThreadPool>(numThreads);
		}

		for (auto& f : forms)
		{
			ret.forms.emplace_back(bake(f, ret.morphemes.data()));
		}
		ret.forms.emplace_back();

		for (auto& m : morphemes)
		{
			ret.morphemes.emplace_back(bake(m, ret.morphemes.data(), ret.forms.data()));
		}

		utils::ContinuousTrie<KTrie> formTrie{ defaultTagSize + 1 };
		// reserve places for root node + default tag morphemes
		for (size_t i = 1; i <= defaultTagSize; ++i)
		{
			formTrie[i].val = &ret.forms[i - 1];
		}
		for (size_t i = defaultTagSize; i < ret.forms.size() - 1; ++i)
		{
			auto& f = ret.forms[i];
			if (f.candidate.empty()) continue;
			formTrie.build(f.form.begin(), f.form.end(), &f);
		}
		ret.formTrie = utils::FrozenTrie<kchar_t, const Form*>{ formTrie };
		return ret;
	}

	inline bool testSpeicalChr(const u16string& form)
	{
		POSTag pos;
		switch (pos = identifySpecialChr(form.back()))
		{
		case POSTag::sf:
		case POSTag::sp:
		case POSTag::ss:
		case POSTag::se:
		case POSTag::so:
		case POSTag::sw:
			return false;
		case POSTag::sl:
		case POSTag::sn:
		case POSTag::sh:
			if (all_of(form.begin(), form.end(), [&](char16_t c)
			{
				return pos == identifySpecialChr(c);
			}))
			{
				return false;
			}
		default:
			return true;
		}
	}

	vector<WordInfo> KiwiBuilder::extractWords(const U16MultipleReader& reader, size_t minCnt, size_t maxWordLen, float minScore, float posThreshold, bool lmFilter) const
	{
		vector<WordInfo> cands = detector.extractWords(reader, minCnt, maxWordLen, minScore);
		if (!lmFilter) return cands;

		vector<WordInfo> ret;
		Kiwi kiwiInst = build();

		unordered_set<KString> allForms;
		for (auto& f : forms)
		{
			allForms.emplace(f.form);
		}

		for (auto& r : cands)
		{
			if (r.posScore[POSTag::nnp] < posThreshold || !r.posScore[POSTag::nnp]) continue;
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
				if (!testSpeicalChr(r.form)) continue;
			}

			{
				auto normForm = normalizeHangul(r.form);
				if (allForms.count(normForm)) continue;

				TokenResult kr = kiwiInst.analyze(r.form, Match::none);
				if (any_of(kr.first.begin(), kr.first.end(), [](const TokenInfo& kp)
				{
					return POSTag::jks <= kp.tag && kp.tag <= POSTag::etm;
				}) && kr.second >= -35)
				{
					continue;
				}

				allForms.emplace(normForm);
				ret.emplace_back(r);
			}
		onlyBracket:;
			if (bracket)
			{
				auto subForm = r.form.substr(0, r.form.find(bracket));
				if (subForm.empty()) continue;
				if (!testSpeicalChr(subForm)) continue;
				auto subNormForm = normalizeHangul(subForm);
				if (allForms.count(subNormForm)) continue;

				TokenResult kr = kiwiInst.analyze(subForm, Match::none);
				if (any_of(kr.first.begin(), kr.first.end(), [](const TokenInfo& kp)
				{
					return POSTag::jks <= kp.tag && kp.tag <= POSTag::etm;
				}) && kr.second >= -35)
				{
					continue;
				}

				allForms.emplace(subNormForm);
				ret.emplace_back(r);
				ret.back().form = subForm;
			}
		}
		return ret;
	}

	vector<WordInfo> KiwiBuilder::extractAddWords(const U16MultipleReader& reader, size_t minCnt, size_t maxWordLen, float minScore, float posThreshold, bool lmFilter)
	{
		vector<WordInfo> words = extractWords(reader, minCnt, maxWordLen, minScore, posThreshold, lmFilter);
		for (auto& w : words)
		{
			addWord(w.form);
		}
		return words;
	}
}
