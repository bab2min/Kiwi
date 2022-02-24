#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>
#include "KTrie.h"
#include "FrozenTrie.hpp"
#include "Knlm.hpp"
#include "serializer.hpp"
#include "count.hpp"
#include "FeatureTestor.h"
#include "Combiner.h"

using namespace std;
using namespace kiwi;

KiwiBuilder::KiwiBuilder() = default;

KiwiBuilder::~KiwiBuilder() = default;

KiwiBuilder::KiwiBuilder(const KiwiBuilder&) = default;

KiwiBuilder::KiwiBuilder(KiwiBuilder&&) noexcept = default;

KiwiBuilder& KiwiBuilder::operator=(const KiwiBuilder&) = default;

KiwiBuilder& KiwiBuilder::operator=(KiwiBuilder&&) noexcept = default;

void KiwiBuilder::loadMMFromTxt(std::istream&& is, MorphemeMap& morphMap, std::unordered_map<POSTag, float>* posWeightSum, const function<bool(float, POSTag)>& selector)
{
	string line;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		auto fields = split(wstr, u'\t');
		if (fields.size() < 8) continue;

		auto form = normalizeHangul(fields[0]);
		auto tag = toPOSTag(fields[1]);

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
		if (m.tag == POSTag::p || (&m - morphemes.data() + m.combined) < langMdl->getHeader().vocab_size)
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
		formMap.emplace(form.form, cnt++);
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
	
	if (!!(options & BuildOption::loadDefaultDict))
	{
		loadDictionary(modelPath + "/default.dict");
	}

	{
		ifstream ifs{ modelPath + string{ "/combiningRule.txt" } };
		if (!ifs) throw Exception("Cannot open '" + modelPath + "/combiningRule.txt'");
		combiningRule = make_shared<cmb::CompiledRule>(cmb::RuleSet{ ifs }.compile());
	}
	updateCombiningCands();
	updateMorphemes();
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

	updateForms();

	Vector<Vector<uint16_t>> sents;
	addCorpusTo(sents, ifstream{ rawDataPath + "/ML_lit.txt" }, realMorph);
	addCorpusTo(sents, ifstream{ rawDataPath + "/ML_spo.txt" }, realMorph);
	vector<pair<uint16_t, uint16_t>> bigramList;
	constexpr size_t order = 3, minCnt = 1, lastMinCnt = 1;
	auto cntNodes = utils::count(sents.begin(), sents.end(), minCnt, 1, order, nullptr, &bigramList);
	langMdl = lm::KnLangModelBase::create(lm::KnLangModelBase::build(cntNodes, order, minCnt, lastMinCnt, 2, 0, 1, 1e-5, 8, false, &bigramList));
	
	if (!!(options & BuildOption::loadDefaultDict))
	{
		loadDictionary(rawDataPath + "/default.dict");
	}

	{
		ifstream ifs{ rawDataPath + string{ "/combiningRule.txt" } };
		if (!ifs) throw Exception("Cannot open '" + rawDataPath + "/combiningRule.txt'");
		combiningRule = make_shared<cmb::CompiledRule>(cmb::RuleSet{ ifs }.compile());
	}
	//updateCombiningCands();
	updateMorphemes();
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
	auto ret = formMap.emplace(form, forms.size());
	if (ret.second)
	{
		forms.emplace_back(form);
	}
	return forms[ret.first->second];
}

bool KiwiBuilder::addWord(const u16string& newForm, POSTag tag, float score, size_t origMorphemeId)
{
	if (newForm.empty()) return false;

	auto normalizedForm = normalizeHangul(newForm);
	auto& f = addForm(normalizedForm);
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
	auto& newMorph = morphemes.back();
	newMorph.kform = &f - &forms[0];
	newMorph.userScore = score;
	newMorph.lmMorphemeId = origMorphemeId;
	return true;
}

void KiwiBuilder::addCombinedMorphemes(size_t leftId, size_t rightId, size_t ruleId)
{
	auto res = combiningRule->combine(forms[morphemes[leftId].kform].form, forms[morphemes[rightId].kform].form, ruleId);
	for (auto& r : res)
	{
		size_t newId = morphemes.size();
		morphemes.emplace_back(POSTag::unknown);
		auto& newMorph = morphemes.back();
		newMorph.userScore = 0;
		newMorph.lmMorphemeId = newId;
		if (morphemes[leftId].chunks.empty())
		{
			newMorph.chunks.emplace_back(leftId);
			newMorph.chunkPositions.emplace_back(0, r.leftEnd);
		}
		else
		{
			newMorph.chunks = morphemes[leftId].chunks;
			newMorph.chunkPositions = morphemes[leftId].chunkPositions;
			newMorph.chunkPositions.back().second = r.leftEnd;
			if (r.vowel == CondVowel::none) r.vowel = morphemes[leftId].vowel;
			if (r.polar == CondPolarity::none) r.polar = morphemes[leftId].polar;
		}
		newMorph.chunks.emplace_back(rightId);
		newMorph.chunkPositions.emplace_back(r.rightBegin, r.str.size());
		if (morphemes[newMorph.chunks[0]].combineSocket)
		{
			newMorph.combineSocket = morphemes[newMorph.chunks[0]].combineSocket;
		}
		newMorph.vowel = r.vowel;
		newMorph.polar = r.polar;
		auto& f = addForm(r.str);
		f.candidate.emplace_back(newId);
		newMorph.kform = &f - &forms[0];
	}
}

void KiwiBuilder::updateCombiningCands()
{
	auto ruleLeftIds = combiningRule->getRuleIdsByLeftTag();
	auto ruleRightIds = combiningRule->getRuleIdsByRightTag();
	while (combiningUpdateIdx < morphemes.size())
	{
		// 새 형태소들 중에서 결합이 가능한 형태소 후보 추출
		for (size_t i = combiningUpdateIdx; i < morphemes.size(); ++i)
		{
			auto tag = morphemes[i].tag;
			auto& form = forms[morphemes[i].kform].form;

			if (tag > POSTag::pa) continue;
			if (morphemes[i].combined) continue;

			if (tag == POSTag::unknown)
			{
				if (morphemes[i].chunks.empty()) continue;
				tag = morphemes[morphemes[i].chunks.back()].tag;
			}

			for (size_t feat = 0; feat < 4; ++feat)
			{
				for (auto id : ruleLeftIds[make_tuple(tag, feat)])
				{
					auto res = combiningRule->testLeftPattern(form, id);
					if (res.empty()) continue;

					if (combiningLeftCands.size() <= id) combiningLeftCands.resize(id + 1);
					if (combiningLeftCands[id].empty() || combiningLeftCands[id].back() < i) combiningLeftCands[id].emplace_back(i);
				}
			}

			if (morphemes[i].tag == POSTag::unknown) continue;
			for (auto id : ruleRightIds[tag])
			{
				auto res = combiningRule->testRightPattern(form, id);
				if (res.empty()) continue;

				if (combiningRightCands.size() <= id) combiningRightCands.resize(id + 1);
				combiningRightCands[id].emplace_back(i);
			}

			if (tag == POSTag::vv || tag == POSTag::va)
			{
				CondVowel vowel = CondVowel::none;
				CondPolarity polar = FeatureTestor::isMatched(&form, CondPolarity::positive) ? CondPolarity::positive : CondPolarity::negative;

				auto& ids = ruleLeftIds[make_tuple(
					tag == POSTag::vv ? POSTag::p : POSTag::pa,
					cmb::CompiledRule::toFeature(vowel, polar)
				)];
				Vector<uint8_t> partialFormInserted(form.size());
				auto cform = form;

				for (auto id : ids)
				{
					auto res = combiningRule->testLeftPattern(form, id);
					if (res.empty()) continue;

					auto& startPos = get<1>(res[0]);
					auto& condPolar = get<2>(res[0]);

					if (startPos == 0)
					{
						if (combiningLeftCands.size() <= id) combiningLeftCands.resize(id + 1);
						combiningLeftCands[id].emplace_back(i);
					}
					else
					{
						auto inserted = combiningSuffices.emplace(
							make_tuple(cform.substr(startPos), tag, condPolar),
							0
						);
						if (inserted.second)
						{
							auto& f = addForm(get<0>(inserted.first->first));
							inserted.first->second = morphemes.size();
							morphemes.emplace_back(tag == POSTag::vv ? POSTag::p : POSTag::pa);
							auto& newMorph = morphemes.back();
							newMorph.kform = &f - &forms[0];
							newMorph.combineSocket = combiningSuffices.size();
						}

						if (!partialFormInserted[startPos])
						{
							auto& f = addForm(cform.substr(0, startPos));
							ptrdiff_t newId = (ptrdiff_t)morphemes.size();
							morphemes.emplace_back(POSTag::p);
							auto& newMorph = morphemes.back();
							newMorph.kform = &f - &forms[0];
							newMorph.combineSocket = morphemes[inserted.first->second].combineSocket;
							newMorph.combined = (ptrdiff_t)i - newId;
							f.candidate.emplace_back(newId);
							partialFormInserted[startPos] = 1;
						}
					}
				}
			}
		}
		for (auto& p : combiningSuffices)
		{
			if (get<1>(p.first) != POSTag::va) continue;
			morphemes[p.second].tag = POSTag::p;
		}
		size_t updated = morphemes.size();

		// 규칙에 의한 결합 연산 수행 후 형태소 목록에 삽입
		for (size_t ruleId = 0; ruleId < min(combiningLeftCands.size(), combiningRightCands.size()); ++ruleId)
		{
			auto& ls = combiningLeftCands[ruleId];
			auto lmid = lower_bound(ls.begin(), ls.end(), combiningUpdateIdx);
			auto& rs = combiningRightCands[ruleId];
			auto rmid = lower_bound(rs.begin(), rs.end(), combiningUpdateIdx);
			for (auto lit = lmid; lit != ls.end(); ++lit)
			{
				for (auto rit = rs.begin(); rit != rmid; ++rit)
				{
					addCombinedMorphemes(*lit, *rit, ruleId);
				}
			}

			for (auto lit = ls.begin(); lit != ls.end(); ++lit)
			{
				for (auto rit = rmid; rit != rs.end(); ++rit)
				{
					addCombinedMorphemes(*lit, *rit, ruleId);
				}
			}
		}
		combiningUpdateIdx = updated;
	}
}

bool KiwiBuilder::addWord(const u16string& form, POSTag tag, float score)
{
	return addWord(form, tag, score, getDefaultMorphemeId(tag));
}
	
size_t KiwiBuilder::findMorpheme(const u16string& form, POSTag tag) const
{
	auto normalized = normalizeHangul(form);
	auto it = formMap.find(normalized);
	if (it == formMap.end()) return -1;

	for (auto p : forms[it->second].candidate)
	{
		if (morphemes[(size_t)p].tag == tag)
		{
			return p;
		}
	}
	return -1;
}

bool KiwiBuilder::addWord(const u16string& newForm, POSTag tag, float score, const u16string& origForm)
{
	size_t origMorphemeId = findMorpheme(origForm, tag);

	if (origMorphemeId == -1)
	{
		throw UnknownMorphemeException{ "cannot find the original morpheme " + utf16To8(origForm) + "/" + tagToString(tag) };
	}

	return addWord(newForm, tag, score, origMorphemeId);
}

bool KiwiBuilder::addPreAnalyzedWord(const u16string& form, const vector<pair<u16string, POSTag>>& analyzed, vector<pair<size_t, size_t>> positions, float score)
{
	if (form.empty()) return false;

	Vector<uint32_t> analyzedIds;
	for (auto& p : analyzed)
	{
		size_t morphemeId = findMorpheme(p.first, p.second);
		if (morphemeId == -1)
		{
			throw UnknownMorphemeException{ "cannot find the original morpheme " + utf16To8(p.first) + "/" + tagToString(p.second) };
		}
		analyzedIds.emplace_back(morphemeId);
	}

	while (positions.size() < analyzed.size())
	{
		positions.emplace_back(0, form.size());
	}

	auto normalized = normalizeHangulWithPosition(form);

	for (auto& p : positions)
	{
		p.first = normalized.second[p.first];
		p.second = normalized.second[p.second];
	}

	auto& f = addForm(normalized.first);
	if (f.candidate.empty())
	{
	}
	else
	{
		for (auto p : f.candidate)
		{
			auto& mchunks = morphemes[(size_t)p].chunks;
			if (mchunks == analyzedIds) return false;
		}
	}

	f.candidate.emplace_back(morphemes.size());
	morphemes.emplace_back(POSTag::unknown);
	auto& newMorph = morphemes.back();
	newMorph.kform = &f - &forms[0];
	newMorph.userScore = score;
	newMorph.lmMorphemeId = morphemes.size() - 1;
	newMorph.chunks = analyzedIds;
	newMorph.chunkPositions.insert(newMorph.chunkPositions.end(), positions.begin(), positions.end());
	return true;
}

size_t KiwiBuilder::loadDictionary(const string& dictPath)
{
	size_t addedCnt = 0;
	ifstream ifs{ dictPath };
	if (!ifs) throw Exception("[loadUserDictionary] Failed to open '" + dictPath + "'");
	string line;
	for (size_t lineNo = 1; getline(ifs, line); ++lineNo)
	{
		auto wstr = utf8To16(line);
		while (!wstr.empty() && kiwi::identifySpecialChr(wstr.back()) == POSTag::unknown) wstr.pop_back();
		if (wstr.empty()) continue;
		if (wstr[0] == u'#') continue;
		auto fields = split(wstr, u'\t');
		if (fields.size() < 2)
		{
			throw Exception("[loadUserDictionary] Wrong dictionary format at line " + to_string(lineNo) + " : " + line);
		}

		if (fields[0].find(' ') != fields[0].npos)
		{
			throw Exception("[loadUserDictionary] Form should not contain space. at line " + to_string(lineNo) + " : " + line);
		}

		float score = 0.f;
		if (fields.size() > 2) score = stof(fields[2].begin(), fields[2].end());

		if (fields[1].find(u'/') != fields[1].npos)
		{
			vector<pair<u16string, POSTag>> morphemes;

			for (auto& m : split(fields[1], u'+'))
			{
				while (!m.empty() && m.back() == ' ') m.pop_back();
				while (!m.empty() && m.front() == ' ') m.erase(m.begin());

				size_t p = m.rfind(u'/');
				if (p == m.npos)
				{
					throw Exception("[loadUserDictionary] Wrong dictionary format at line " + to_string(lineNo) + " : " + line);
				}
				auto pos = toPOSTag(m.substr(p + 1));
				if (pos == POSTag::max)
				{
					throw Exception("[loadUserDictionary] Unknown Tag '" + utf16To8(fields[1]) + "' at line " + to_string(lineNo));
				}
				morphemes.emplace_back(m.substr(0, p), pos);
			}

			if (morphemes.size() > 1)
			{
				addedCnt += addPreAnalyzedWord(fields[0], morphemes, {}, score) ? 1 : 0;
			}
			else
			{
				addedCnt += addWord(fields[0], morphemes[0].second, score, morphemes[0].first);
			}
		}
		else
		{
			auto pos = toPOSTag(fields[1]);
			if (pos == POSTag::max)
			{
				throw Exception("[loadUserDictionary] Unknown Tag '" + utf16To8(fields[1]) + "' at line " + to_string(lineNo));
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

		if (f.candidate[0]->vowel != CondVowel::none && 
			all_of(f.candidate.begin(), f.candidate.end(), [&](const Morpheme* m)
		{
			return m->vowel == f.candidate[0]->vowel;
		}))
		{
			f.vowel = f.candidate[0]->vowel;
		}

		if (f.candidate[0]->polar != CondPolarity::none && 
			all_of(f.candidate.begin(), f.candidate.end(), [&](const Morpheme* m)
		{
			return m->polar == f.candidate[0]->polar;
		}))
		{
			f.polar = f.candidate[0]->polar;
		}

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