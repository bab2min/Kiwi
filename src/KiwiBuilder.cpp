#include <fstream>

#include <kiwi/Kiwi.h>
#include <kiwi/Utils.h>
#include "ArchAvailable.h"
#include "KTrie.h"
#include "StrUtils.h"
#include "FileUtils.h"
#include "FrozenTrie.hpp"
#include "Knlm.hpp"
#include "serializer.hpp"
#include "count.hpp"
#include "FeatureTestor.h"
#include "Combiner.h"
#include "RaggedVector.hpp"

using namespace std;
using namespace kiwi;

KiwiBuilder::KiwiBuilder() = default;

KiwiBuilder::~KiwiBuilder() = default;

KiwiBuilder::KiwiBuilder(const KiwiBuilder&) = default;

KiwiBuilder::KiwiBuilder(KiwiBuilder&&) noexcept = default;

KiwiBuilder& KiwiBuilder::operator=(const KiwiBuilder&) = default;

KiwiBuilder& KiwiBuilder::operator=(KiwiBuilder&&) noexcept = default;

template<class Fn>
auto KiwiBuilder::loadMorphemesFromTxt(std::istream& is, Fn&& filter) -> MorphemeMap
{
	Vector<tuple<KString, float, POSTag, CondVowel, KString, int>> longTails;
	UnorderedMap<POSTag, float> longTailWeights;
	MorphemeMap morphMap;

	const auto& insertMorph = [&](KString&& form, float score, POSTag tag, CondVowel cvowel, size_t origMorphemeId = 0)
	{
		auto& fm = addForm(form);
		bool unified = false;
		if (isEClass(tag) && form[0] == u'아')
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
			morphMap.emplace(make_pair(form, tag), origMorphemeId ? origMorphemeId : mid);
			fm.candidate.emplace_back(mid);
			morphemes.emplace_back(tag, cvowel, CondPolarity::none);
			morphemes.back().kform = &fm - &forms[0];
			morphemes.back().userScore = score;
			morphemes.back().lmMorphemeId = origMorphemeId;
		}
	};

	string line;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		if (wstr.empty()) continue;
		auto fields = split(wstr, u'\t');
		if (fields.size() < 3)
		{
			throw Exception{ "wrong line: " + line };
		}

		auto form = normalizeHangul(fields[0]);
		auto tag = toPOSTag(fields[1]);
		if (tag >= POSTag::p || tag == POSTag::unknown)
		{
			cerr << "Wrong tag at line: " + line << endl;
			continue;
		}

		float morphWeight = stof(fields[2].begin(), fields[2].end());

		CondVowel cvowel = CondVowel::none;
		KString origMorphemeOfAlias;
		int addAlias = 0;
		if (fields.size() > 3)
		{
			for (size_t i = 3; i < fields.size(); ++i)
			{
				auto& f = fields[i];
				if (f == u"vowel")
				{
					if (cvowel != CondVowel::none) throw Exception{ "wrong line: " + line };
					cvowel = CondVowel::vowel;
					if (i + 1 < fields.size())
					{
						if (stof(fields[i + 1].begin(), fields[i + 1].end())) ++i;
					}
				}
				else if (f == u"non_vowel")
				{
					if (cvowel != CondVowel::none) throw Exception{ "wrong line: " + line };
					cvowel = CondVowel::non_vowel;
					if (i + 1 < fields.size())
					{
						if (stof(fields[i + 1].begin(), fields[i + 1].end())) ++i;
					}
				}
				else if (f == u"vocalic")
				{
					if (cvowel != CondVowel::none) throw Exception{ "wrong line: " + line };
					cvowel = CondVowel::vocalic;
					if (i + 1 < fields.size())
					{
						if (stof(fields[i + 1].begin(), fields[i + 1].end())) ++i;
					}
				}
				else if (f[0] == u'=')
				{
					if (!origMorphemeOfAlias.empty()) throw Exception{ "wrong line: " + line };
					if (f[1] == u'=')
					{
						origMorphemeOfAlias = normalizeHangul(f.substr(2));
						addAlias = 2;
					}
					else
					{
						origMorphemeOfAlias = normalizeHangul(f.substr(1));
						addAlias = 1;
					}
					
				}
				else if (f[0] == u'~')
				{
					if (!origMorphemeOfAlias.empty()) throw Exception{ "wrong line: " + line };
					origMorphemeOfAlias = normalizeHangul(f.substr(1));
					addAlias = 0;
				}
				else
				{
					throw Exception{ "wrong line: " + line };
				}
			}
		}

		if (filter(tag, morphWeight) && origMorphemeOfAlias.empty())
		{
			insertMorph(move(form), morphWeight, tag, cvowel);
		}
		else
		{
			longTails.emplace_back(form, morphWeight, tag, cvowel, origMorphemeOfAlias, addAlias);
			longTailWeights[tag] += morphWeight;
		}
		
	}

	for (auto& p : longTails)
	{
		auto morphWeight = get<1>(p);
		auto tag = get<2>(p);
		auto cvowel = get<3>(p);
		auto& origMorphemeOfAlias = get<4>(p);
		auto addAlias = get<5>(p);
		if (origMorphemeOfAlias.empty()) continue;
		auto it = morphMap.find(make_pair(origMorphemeOfAlias, tag));
		if (it == morphMap.end())
		{
			throw Exception{ "cannot find base morpheme: " + utf16To8(origMorphemeOfAlias) + "/" + tagToString(tag) };
		}
		if (!addAlias) continue;
		morphemes[it->second].userScore += morphWeight;
	}

	for (auto& p : longTails)
	{
		auto morphWeight = get<1>(p);
		auto tag = get<2>(p);
		auto cvowel = get<3>(p);
		auto& origMorphemeOfAlias = get<4>(p);
		auto addAlias = get<5>(p);
		if (origMorphemeOfAlias.empty())
		{
			insertMorph(move(get<0>(p)), log(morphWeight / longTailWeights[tag]), tag, cvowel, getDefaultMorphemeId(tag));
		}
		else
		{
			auto it = morphMap.find(make_pair(origMorphemeOfAlias, tag));
			if (addAlias)
			{
				float normalized = morphWeight / morphemes[it->second].userScore;
				float score = log(normalized);
				if (addAlias > 1) score = 0;
				insertMorph(move(get<0>(p)), score, tag, cvowel, it->second);
			}
			else
			{
				morphMap.emplace(make_pair(move(get<0>(p)), tag), it->second);
			}
		}
	}
	for (auto& m : morphemes)
	{
		if (m.userScore <= 0) continue;
		m.userScore = 0;
	}
	return morphMap;
}

void KiwiBuilder::addCorpusTo(RaggedVector<uint16_t>& out, std::istream& is, KiwiBuilder::MorphemeMap& morphMap)
{
	Vector<uint16_t> wids;
	string line;
	while (getline(is, line))
	{
		auto wstr = utf8To16(line);
		if (!wstr.empty() && wstr.back() == '\n') wstr.pop_back();
		if (wstr.empty() && wids.size() > 1)
		{
			out.emplace_back();
			out.add_data(0);
			out.insert_data(wids.begin(), wids.end());
			out.add_data(1);
			wids.clear();
			continue;
		}
		auto fields = split(wstr, u'\t');
		if (fields.size() < 2) continue;

		for (size_t i = 1; i < fields.size(); i += 2)
		{
			auto f = normalizeHangul(fields[i]);
			if (f.empty()) continue;

			auto t = toPOSTag(fields[i + 1]);

			if (f[0] == u'아' && fields[i + 1][0] == 'E')
			{
				f[0] = u'어';
			}

			auto it = morphMap.find(make_pair(f, t));
			if (it == morphMap.end() || !morphemes[it->second].chunks.empty() || morphemes[it->second].combineSocket)
			{
				if (t < POSTag::p && t != POSTag::unknown)
				{
					wids.emplace_back(getDefaultMorphemeId(t));
				}
				else
				{
					//wids.emplace_back(getDefaultMorphemeId(POSTag::nnp));
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
		if (m.lmMorphemeId > 0) continue;
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
	archType = getSelectedArch(ArchType::default_);

	{
		utils::MMap mm{ modelPath + "/sj.morph" };
		utils::imstream iss{ mm };
		loadMorphBin(iss);
	}
	langMdl = lm::KnLangModelBase::create(utils::MMap(modelPath + string{ "/sj.knlm" }), archType);

	if (!!(options & BuildOption::loadDefaultDict))
	{
		loadDictionary(modelPath + "/default.dict");
	}

	{
		ifstream ifs;
		combiningRule = make_shared<cmb::CompiledRule>(cmb::RuleSet{ openFile(ifs, modelPath + string{ "/combiningRule.txt" }) }.compile());
	}
}

KiwiBuilder::KiwiBuilder(const ModelBuildArgs& args)
{
	archType = getSelectedArch(ArchType::default_);

	forms.resize(defaultTagSize);
	morphemes.resize(defaultTagSize + 2); // additional places for <s> & </s>
	for (size_t i = 0; i < defaultTagSize; ++i)
	{
		forms[i].candidate.emplace_back(i + 2);
		morphemes[i + 2].tag = (POSTag)(i + 1);
	}

	ifstream ifs;
	auto realMorph = loadMorphemesFromTxt(openFile(ifs, args.morphemeDef), [&](POSTag tag, float cnt)
	{
		return cnt >= args.minMorphCnt;
	});
	updateForms();

	RaggedVector<uint16_t> sents;
	for (auto& path : args.corpora)
	{
		ifstream ifs;
		addCorpusTo(sents, openFile(ifs, path), realMorph);
	}

	size_t lmVocabSize = 0;
	for (auto& p : realMorph) lmVocabSize = max(p.second, lmVocabSize);
	lmVocabSize += 1;

	Vector<utils::Vid> historyTx(lmVocabSize);

	if (args.useLmTagHistory)
	{
		for (size_t i = 0; i < lmVocabSize; ++i)
		{
			historyTx[i] = (size_t)morphemes[i].tag + lmVocabSize;
		}
	}

	vector<pair<uint16_t, uint16_t>> bigramList;
	utils::ThreadPool pool;
	if (args.numWorkers > 1)
	{
		pool.~ThreadPool();
		new (&pool) utils::ThreadPool{ args.numWorkers };
	}
	auto cntNodes = utils::count(sents.begin(), sents.end(), args.lmMinCnt, 1, args.lmOrder, (args.numWorkers > 1 ? &pool : nullptr), &bigramList, args.useLmTagHistory ? &historyTx : nullptr);
	cntNodes.root().getNext(lmVocabSize)->val /= 2;
	langMdl = lm::KnLangModelBase::create(lm::KnLangModelBase::build(
		cntNodes, 
		args.lmOrder, args.lmMinCnt, args.lmLastOrderMinCnt, 
		2, 0, 1, 1e-5, 
		args.quantizeLm ? 8 : 0,
		args.compressLm,
		&bigramList, 
		args.useLmTagHistory ? &historyTx : nullptr
	), archType);

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

FormRaw& KiwiBuilder::addForm(const KString& form)
{
	auto ret = formMap.emplace(form, forms.size());
	if (ret.second)
	{
		forms.emplace_back(form);
	}
	return forms[ret.first->second];
}

size_t KiwiBuilder::addForm(Vector<FormRaw>& newForms, UnorderedMap<KString, size_t>& newFormMap, KString form) const
{
	auto it = formMap.find(form);
	if (it != formMap.end())
	{
		return it->second;
	}
	auto ret = newFormMap.emplace(form, forms.size() + newForms.size());
	if (ret.second)
	{
		newForms.emplace_back(form);
	}
	return ret.first->second;
}

bool KiwiBuilder::addWord(nonstd::u16string_view newForm, POSTag tag, float score, size_t origMorphemeId)
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
			if (morphemes[p].tag == tag && morphemes[p].lmMorphemeId == origMorphemeId) return false;
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

bool KiwiBuilder::addWord(const u16string& newForm, POSTag tag, float score, size_t origMorphemeId)
{
	return addWord(nonstd::to_string_view(newForm), tag, score, origMorphemeId);
}

void KiwiBuilder::addCombinedMorphemes(
	Vector<FormRaw>& newForms, 
	UnorderedMap<KString, size_t>& newFormMap, 
	Vector<MorphemeRaw>& newMorphemes, 
	UnorderedMap<size_t, Vector<uint32_t>>& newFormCands,
	size_t leftId, 
	size_t rightId, 
	size_t ruleId
) const
{
	const auto& getMorph = [&](size_t id) -> const MorphemeRaw&
	{
		if (id < morphemes.size()) return morphemes[id];
		else return newMorphemes[id - morphemes.size()];
	};

	const auto& getForm = [&](size_t id) -> const FormRaw&
	{
		if (id < forms.size()) return forms[id];
		else return newForms[id - forms.size()];
	};

	auto res = combiningRule->combine(getForm(getMorph(leftId).kform).form, getForm(getMorph(rightId).kform).form, ruleId);
	for (auto& r : res)
	{
		size_t newId = morphemes.size() + newMorphemes.size();
		newMorphemes.emplace_back(POSTag::unknown);
		auto& newMorph = newMorphemes.back();
		newMorph.lmMorphemeId = newId;
		if (getMorph(leftId).chunks.empty())
		{
			newMorph.chunks.emplace_back(leftId);
			newMorph.chunkPositions.emplace_back(0, r.leftEnd);
			newMorph.userScore = getMorph(leftId).userScore + r.score;
		}
		else
		{
			auto& leftMorph = getMorph(leftId);
			newMorph.chunks = leftMorph.chunks;
			newMorph.chunkPositions = leftMorph.chunkPositions;
			newMorph.chunkPositions.back().second = r.leftEnd;
			if (r.vowel == CondVowel::none) r.vowel = leftMorph.vowel;
			if (r.polar == CondPolarity::none) r.polar = leftMorph.polar;
			newMorph.userScore = leftMorph.userScore + r.score;
		}
		newMorph.chunks.emplace_back(rightId);
		newMorph.chunkPositions.emplace_back(r.rightBegin, r.str.size());
		if (getMorph(newMorph.chunks[0]).combineSocket)
		{
			newMorph.combineSocket = getMorph(newMorph.chunks[0]).combineSocket;
		}
		newMorph.userScore += getMorph(rightId).userScore;
		newMorph.vowel = r.vowel;
		// 양/음성 조건은 부분결합된 형태소에서만 유효
		if (getMorph(leftId).tag == POSTag::p)
		{
			newMorph.polar = r.polar;
		}
		size_t fid = addForm(newForms, newFormMap, r.str);
		newFormCands[fid].emplace_back(newId);
		newMorph.kform = fid;
	}
}

void KiwiBuilder::buildCombinedMorphemes(
	Vector<FormRaw>& newForms, 
	Vector<MorphemeRaw>& newMorphemes, 
	UnorderedMap<size_t, Vector<uint32_t>>& newFormCands
) const
{
	const auto& getMorph = [&](size_t id) -> const MorphemeRaw&
	{
		if (id < morphemes.size()) return morphemes[id];
		else return newMorphemes[id - morphemes.size()];
	};

	const auto& getForm = [&](size_t id) -> const FormRaw&
	{
		if (id < forms.size()) return forms[id];
		else return newForms[id - forms.size()];
	};

	UnorderedMap<KString, size_t> newFormMap;
	Vector<Vector<size_t>> combiningLeftCands, combiningRightCands;
	UnorderedMap<std::tuple<KString, POSTag, CondPolarity>, size_t> combiningSuffices;
	size_t combiningUpdateIdx = defaultTagSize + 2;

	auto ruleLeftIds = combiningRule->getRuleIdsByLeftTag();
	auto ruleRightIds = combiningRule->getRuleIdsByRightTag();
	while (combiningUpdateIdx < morphemes.size() + newMorphemes.size())
	{
		// 새 형태소들 중에서 결합이 가능한 형태소 후보 추출
		for (size_t i = combiningUpdateIdx; i < morphemes.size() + newMorphemes.size(); ++i)
		{
			auto& morph = getMorph(i);
			auto tag = morph.tag;
			auto& form = getForm(morph.kform).form;

			if (tag > POSTag::pa) continue;
			if (morph.combined) continue;

			if (tag == POSTag::unknown)
			{
				if (morph.chunks.empty()) continue;
				tag = getMorph(morph.chunks.back()).tag;
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

			if (morph.tag == POSTag::unknown) continue;
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
							size_t fid = addForm(newForms, newFormMap, get<0>(inserted.first->first));
							size_t newId = morphemes.size() + newMorphemes.size();
							inserted.first->second = newId;
							newMorphemes.emplace_back(tag == POSTag::vv ? POSTag::p : POSTag::pa);
							auto& newMorph = newMorphemes.back();
							newMorph.kform = fid;
							newMorph.lmMorphemeId = newId;
							newMorph.combineSocket = combiningSuffices.size();
						}

						if (!partialFormInserted[startPos])
						{
							size_t fid = addForm(newForms, newFormMap, cform.substr(0, startPos));
							ptrdiff_t newId = (ptrdiff_t)(morphemes.size() + newMorphemes.size());
							newMorphemes.emplace_back(POSTag::p);
							auto& newMorph = newMorphemes.back();
							newMorph.kform = fid;
							newMorph.lmMorphemeId = newId;
							newMorph.combineSocket = getMorph(inserted.first->second).combineSocket;
							newMorph.combined = (ptrdiff_t)i - newId;
							newFormCands[fid].emplace_back(newId);
							partialFormInserted[startPos] = 1;
						}
					}
				}
			}
		}
		for (auto& p : combiningSuffices)
		{
			if (get<1>(p.first) != POSTag::va) continue;
			newMorphemes[p.second - morphemes.size()].tag = POSTag::p;
		}

		size_t updated = morphemes.size() + newMorphemes.size();

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
					addCombinedMorphemes(newForms, newFormMap, newMorphemes, newFormCands, *lit, *rit, ruleId);
				}
			}

			for (auto lit = ls.begin(); lit != ls.end(); ++lit)
			{
				for (auto rit = rmid; rit != rs.end(); ++rit)
				{
					addCombinedMorphemes(newForms, newFormMap, newMorphemes, newFormCands, *lit, *rit, ruleId);
				}
			}
		}
		combiningUpdateIdx = updated;
	}
}

bool KiwiBuilder::addWord(nonstd::u16string_view form, POSTag tag, float score)
{
	return addWord(form, tag, score, getDefaultMorphemeId(tag));
}

bool KiwiBuilder::addWord(const u16string& form, POSTag tag, float score)
{
	return addWord(nonstd::to_string_view(form), tag, score);
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

bool KiwiBuilder::addWord(nonstd::u16string_view newForm, POSTag tag, float score, const u16string& origForm)
{
	size_t origMorphemeId = findMorpheme(origForm, tag);

	if (origMorphemeId == -1)
	{
		throw UnknownMorphemeException{ "cannot find the original morpheme " + utf16To8(origForm) + "/" + tagToString(tag) };
	}

	return addWord(newForm, tag, score, origMorphemeId);
}

bool KiwiBuilder::addWord(const u16string& newForm, POSTag tag, float score, const u16string& origForm)
{
	return addWord(nonstd::to_string_view(newForm), tag, score, origForm);
}

bool KiwiBuilder::addPreAnalyzedWord(nonstd::u16string_view form, const vector<pair<u16string, POSTag>>& analyzed, vector<pair<size_t, size_t>> positions, float score)
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
			auto& mchunks = morphemes[p].chunks;
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

bool KiwiBuilder::addPreAnalyzedWord(const u16string& form, const vector<pair<u16string, POSTag>>& analyzed, vector<pair<size_t, size_t>> positions, float score)
{
	return addPreAnalyzedWord(nonstd::to_string_view(form), analyzed, positions, score);
}

size_t KiwiBuilder::loadDictionary(const string& dictPath)
{
	size_t addedCnt = 0;
	ifstream ifs;
	openFile(ifs, dictPath);
	string line;
	array<nonstd::u16string_view, 3> fields;
	u16string wstr;
	for (size_t lineNo = 1; getline(ifs, line); ++lineNo)
	{
		utf8To16(nonstd::to_string_view(line), wstr);
		while (!wstr.empty() && kiwi::identifySpecialChr(wstr.back()) == POSTag::unknown) wstr.pop_back();
		if (wstr.empty()) continue;
		if (wstr[0] == u'#') continue;
		size_t fieldSize = split(wstr, u'\t', fields.begin(), 2) - fields.begin();
		if (fieldSize < 2)
		{
			throw Exception("[loadUserDictionary] Wrong dictionary format at line " + to_string(lineNo) + " : " + line);
		}

		if (fields[0].find(' ') != fields[0].npos)
		{
			throw Exception("[loadUserDictionary] Form should not contain space. at line " + to_string(lineNo) + " : " + line);
		}

		float score = 0.f;
		if (fieldSize > 2) score = stof(fields[2].begin(), fields[2].end());

		if (fields[1].find(u'/') != fields[1].npos)
		{
			vector<pair<u16string, POSTag>> morphemes;

			for (auto& m : split(fields[1], u'+'))
			{
				size_t b = 0, e = m.size();
				while (b < e && m[e - 1] == ' ') --e;
				while (b < e && m[b] == ' ') ++b;
				m = m.substr(b, e - b);

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
				morphemes.emplace_back(m.substr(0, p).to_string(), pos);
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

template<ArchType archType>
utils::FrozenTrie<kchar_t, const Form*> freezeTrie(utils::ContinuousTrie<KTrie>&& trie)
{
	return { trie, ArchTypeHolder<archType>{} };
}

using FnFreezeTrie = decltype(&freezeTrie<ArchType::none>);

struct FreezeTrieGetter
{
	template<std::ptrdiff_t i>
	struct Wrapper
	{
		static constexpr FnFreezeTrie value = &freezeTrie<static_cast<ArchType>(i)>;
	};
};

Kiwi KiwiBuilder::build() const
{
	Kiwi ret{ archType, langMdl->getHeader().key_size };

	Vector<FormRaw> combinedForms;
	Vector<MorphemeRaw> combinedMorphemes;
	UnorderedMap<size_t, Vector<uint32_t>> newFormCands;

	buildCombinedMorphemes(combinedForms, combinedMorphemes, newFormCands);

	ret.forms.reserve(forms.size() + combinedForms.size() + 1);
	ret.morphemes.reserve(morphemes.size() + combinedMorphemes.size());
	ret.langMdl = langMdl;
	ret.integrateAllomorph = !!(options & BuildOption::integrateAllomorph);
	if (numThreads >= 1)
	{
		ret.pool = make_unique<utils::ThreadPool>(numThreads);
	}

	for (auto& f : forms)
	{
		auto it = newFormCands.find(ret.forms.size());
		if (it == newFormCands.end())
		{
			ret.forms.emplace_back(bake(f, ret.morphemes.data()));
		}
		else
		{
			ret.forms.emplace_back(bake(f, ret.morphemes.data(), it->second));
		}
		
	}
	for (auto& f : combinedForms)
	{
		ret.forms.emplace_back(bake(f, ret.morphemes.data(), newFormCands[ret.forms.size()]));
	}

	ret.forms.emplace_back();

	for (auto& m : morphemes)
	{
		ret.morphemes.emplace_back(bake(m, ret.morphemes.data(), ret.forms.data()));
	}
	for (auto& m : combinedMorphemes)
	{
		ret.morphemes.emplace_back(bake(m, ret.morphemes.data(), ret.forms.data()));
	}

	utils::ContinuousTrie<KTrie> formTrie{ defaultTagSize + 1 };
	// reserve places for root node + default tag morphemes
	for (size_t i = 1; i <= defaultTagSize; ++i)
	{
		formTrie[i].val = &ret.forms[i - 1];
	}

	Vector<const Form*> sortedForms;
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

		sortedForms.emplace_back(&f);
	}

	sort(sortedForms.begin(), sortedForms.end(), [](const Form* a, const Form* b)
	{
		return a->form < b->form;
	});

	size_t estimatedNodeSize = 0;
	const KString* prevForm = nullptr;
	for (auto f : sortedForms)
	{
		if (!prevForm)
		{
			estimatedNodeSize += f->form.size();
			prevForm = &f->form;
			continue;
		}
		size_t commonPrefix = 0;
		while (commonPrefix < std::min(prevForm->size(), f->form.size())
			&& (*prevForm)[commonPrefix] == f->form[commonPrefix]) ++commonPrefix;
		estimatedNodeSize += f->form.size() - commonPrefix;
		prevForm = &f->form;
	}
	formTrie.reserveMore(estimatedNodeSize);

	decltype(formTrie)::CacheStore<const KString> cache;
	for (auto f : sortedForms)
	{
		formTrie.buildWithCaching(f->form, f, cache);
	}

	static tp::Table<FnFreezeTrie, AvailableArch> table{ FreezeTrieGetter{} };
	auto* fn = table[static_cast<std::ptrdiff_t>(archType)];
	if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType)};
	ret.formTrie = (*fn)(move(formTrie));
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