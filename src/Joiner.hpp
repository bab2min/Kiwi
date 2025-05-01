#pragma once
#include <kiwi/Joiner.h>
#include <kiwi/Kiwi.h>
#include <kiwi/LangModel.h>
#include "Combiner.h"
#include "StrUtils.h"

using namespace std;

namespace kiwi
{
	namespace cmb
	{
		inline const char16_t* reprFormForTag(POSTag tag)
		{
			switch (tag)
			{
			case POSTag::sf:
				return u".";
			case POSTag::sp:
				return u",";
			case POSTag::ss:
				return u"'";
			case POSTag::sso:
				return u"(";
			case POSTag::ssc:
				return u")";
			case POSTag::se:
				return u"…";
			case POSTag::so:
				return u"-";
			case POSTag::sw:
				return u"^";
			case POSTag::sb:
				return u"(1)";
			case POSTag::sl:
				return u"A";
			case POSTag::sh:
				return u"漢";
			case POSTag::sn:
				return u"1";
			case POSTag::w_url:
				return u"http://ex.org";
			case POSTag::w_email:
				return u"ex@ex.org";
			case POSTag::w_mention:
				return u"@ex";
			case POSTag::w_hashtag:
				return u"#ex";
			case POSTag::w_serial:
				return u"1:2";
			case POSTag::w_emoji:
				return u"\U0001F600";
			}
			return u"";
		}

		template<class LmState>
		void AutoJoiner::addImpl(size_t morphemeId, Space space, Vector<Candidate<LmState>>& candidates)
		{
			auto& morph = kiwi->morphemes[morphemeId];
			for (auto& cand : candidates)
			{
				cand.score += cand.lmState.next(kiwi->langMdl.get(), morph.lmMorphemeId);
				if (morph.getForm().empty())
				{
					cand.joiner.add(reprFormForTag(morph.tag), morph.tag, space);
				}
				else
				{
					cand.joiner.add(morph.getForm(), morph.tag, space);
				}
			}

			sort(candidates.begin(), candidates.end(), [](const cmb::Candidate<LmState>& a, const cmb::Candidate<LmState>& b)
			{
				return a.score > b.score;
			});
		}

		template<class Func>
		void AutoJoiner::foreachMorpheme(const Form* formHead, Func&& func) const
		{
			if (kiwi->isTypoTolerant())
			{
				auto tformHead = reinterpret_cast<const TypoForm*>(formHead);
				do
				{
					if (tformHead->score() == 0)
					{
						for (auto m : tformHead->form(kiwi->forms.data()).candidate)
						{
							func(m);
						}
					}
					++tformHead;
				} while (tformHead[-1].hash() == tformHead[0].hash());
			}
			else
			{
				do
				{
					for (auto m : formHead->candidate)
					{
						func(m);
					}
					++formHead;
				} while (formHead[-1].form == formHead[0].form);
			}
		}

		template<class LmState>
		void AutoJoiner::addImpl2(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<LmState>>& candidates)
		{
			const Form* formHead;
			auto node = kiwi->formTrie.root();
			for (auto c : normalizeHangul(form))
			{
				node = node->template nextOpt<LmState::arch>(kiwi->formTrie, c);
				if (!node) break;
			}

			// prevent unknown or partial tag
			POSTag fixedTag = tag;
			if (tag == POSTag::unknown || tag == POSTag::p)
			{
				fixedTag = POSTag::nnp;
			}

			if (node && kiwi->formTrie.hasMatch(formHead = node->val(kiwi->formTrie)))
			{
				Vector<const Morpheme*> cands;
				foreachMorpheme(formHead, [&](const Morpheme* m)
				{
					if (areTagsEqual(m->tag, fixedTag, inferRegularity))
					{
						cands.emplace_back(m);
					}
				});

				if (cands.size() <= 1)
				{
					auto lmId = cands.empty() ? getDefaultMorphemeId(clearIrregular(fixedTag)) : cands[0]->lmMorphemeId;
					if (!cands.empty()) tag = cands[0]->tag;
					for (auto& cand : candidates)
					{
						cand.score += cand.lmState.next(kiwi->langMdl.get(), lmId);
						cand.joiner.add(form, tag, space);
					}
				}
				else
				{
					size_t oSize = candidates.size();
					for (size_t i = 1; i < cands.size(); ++i)
					{
						for (size_t o = 0; o < oSize; ++o)
						{
							candidates.emplace_back(candidates[o]);
							auto& n = candidates.back();
							n.score += n.lmState.next(kiwi->langMdl.get(), cands[i]->lmMorphemeId);
							n.joiner.add(form, cands[i]->tag, space);
						}
					}
					for (size_t o = 0; o < oSize; ++o)
					{
						auto& n = candidates[o];
						n.score += n.lmState.next(kiwi->langMdl.get(), cands[0]->lmMorphemeId);
						n.joiner.add(form, cands[0]->tag, space);
					}

					UnorderedMap<LmState, pair<float, uint32_t>> bestScoreByState;
					for (size_t i = 0; i < candidates.size(); ++i)
					{
						auto& c = candidates[i];
						auto inserted = bestScoreByState.emplace(c.lmState, make_pair(c.score, (uint32_t)i));
						if (!inserted.second)
						{
							if (inserted.first->second.first < c.score)
							{
								inserted.first->second = make_pair(c.score, i);
							}
						}
					}

					if (bestScoreByState.size() < candidates.size())
					{
						Vector<Candidate<LmState>> newCandidates;
						newCandidates.reserve(bestScoreByState.size());
						for (auto& p : bestScoreByState)
						{
							newCandidates.emplace_back(std::move(candidates[p.second.second]));
						}
						candidates = std::move(newCandidates);
					}
				}
			}
			else
			{
				auto lmId = getDefaultMorphemeId(clearIrregular(fixedTag));
				for (auto& cand : candidates)
				{
					cand.score += cand.lmState.next(kiwi->langMdl.get(), lmId);
					cand.joiner.add(form, tag, space);
				}
			}
			sort(candidates.begin(), candidates.end(), [](const cmb::Candidate<LmState>& a, const cmb::Candidate<LmState>& b)
			{
				return a.score > b.score;
			});
		}

		template<ArchType arch>
		void AutoJoiner::addWithoutSearchImpl2(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates)
		{
			if (inferRegularity)
			{
				auto node = kiwi->formTrie.root();
				for (auto c : normalizeHangul(form))
				{
					node = node->template nextOpt<arch>(kiwi->formTrie, c);
					if (!node) break;
				}

				if (node)
				{
					if (const Form* formHead = node->val(kiwi->formTrie))
					{
						Vector<const Morpheme*> cands;
						foreachMorpheme(formHead, [&](const Morpheme* m)
						{
							if (areTagsEqual(m->tag, tag, true))
							{
								cands.emplace_back(m);
							}
						});

						if (!cands.empty())
						{
							tag = cands[0]->tag;
						}
					}
				}
			}
			candidates[0].joiner.add(form, tag, space);
		}

		template<ArchType arch>
		void AutoJoiner::addWithoutSearchImpl(size_t morphemeId, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates)
		{
			auto& morph = kiwi->morphemes[morphemeId];
			for (auto& cand : candidates)
			{
				if (morph.getForm().empty())
				{
					cand.joiner.add(reprFormForTag(morph.tag), morph.tag, space);
				}
				else
				{
					cand.joiner.add(morph.getForm(), morph.tag, space);
				}
			}
		}

		template<class LmState>
		struct AutoJoiner::Dispatcher
		{
			static void add(AutoJoiner* joiner, size_t morphemeId, Space space, Vector<Candidate<LmState>>& candidates)
			{
				return joiner->addImpl(morphemeId, space, candidates);
			}

			static void add2(AutoJoiner* joiner, U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<LmState>>& candidates)
			{
				return joiner->addImpl2(form, tag, inferRegularity, space, candidates);
			}
		};

		template<ArchType arch>
		struct AutoJoiner::Dispatcher<lm::VoidState<arch>>
		{
			static void add(AutoJoiner* joiner, size_t morphemeId, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates)
			{
				return joiner->addWithoutSearchImpl(morphemeId, space, candidates);
			}

			static void add2(AutoJoiner* joiner, U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates)
			{
				return joiner->addWithoutSearchImpl2(form, tag, inferRegularity, space, candidates);
			}
		};

		template<class LmState>
		AutoJoiner::AutoJoiner(const Kiwi& _kiwi, Candidate<LmState>&& state)
			: kiwi{ &_kiwi }, candBuf{ Vector<Candidate<LmState>>{ { move(state) } } }
		{
			using Dp = Dispatcher<LmState>;
			dfAdd = reinterpret_cast<FnAdd>(&Dp::add);
			dfAdd2 = reinterpret_cast<FnAdd2>(&Dp::add2);
		}

	}
}
