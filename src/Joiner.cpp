#include "Joiner.hpp"
#include "FrozenTrie.hpp"

using namespace std;

namespace kiwi
{
	namespace cmb
	{
		Joiner::Joiner(const CompiledRule& _cr) : cr{ &_cr } {}
		Joiner::~Joiner() = default;
		Joiner::Joiner(const Joiner&) = default;
		Joiner::Joiner(Joiner&&) noexcept = default;
		Joiner& Joiner::operator=(const Joiner&) = default;
		Joiner& Joiner::operator=(Joiner&&) = default;

		inline bool isSpaceInsertable(POSTag l, POSTag r, U16StringView rform)
		{
			if (l == r && (POSTag::sf <= l && l <= POSTag::sn)) return true;
			if (r == POSTag::vcp) return false;
			if (r == POSTag::xsa || r == POSTag::xsai || r == POSTag::xsv || r == POSTag::xsn) return isJClass(l);
			if (l == POSTag::xpn || l == POSTag::so || l == POSTag::ss || l == POSTag::sw) return false;
			if (l == POSTag::sn && r == POSTag::nnb) return false;
			if (!(l == POSTag::sn || l == POSTag::sl)
				&& (r == POSTag::sl || r == POSTag::sn)) return true;
			if (l == POSTag::sn && r == POSTag::nr) return false;
			if (l == POSTag::sso || l == POSTag::ssc) return false;
			if (r == POSTag::sso) return true;
			if ((isJClass(l) || isEClass(l)) && r == POSTag::ss) return true;

			if (r == POSTag::vx && rform.size() == 1 && (rform[0] == u'하' || rform[0] == u'지')) return false;

			switch (r)
			{
			case POSTag::nng:
			case POSTag::nnp:
			case POSTag::nnb:
			case POSTag::np:
			case POSTag::nr:
			case POSTag::mag:
			case POSTag::maj:
			case POSTag::mm:
			case POSTag::ic:
			case POSTag::vv:
			case POSTag::va:
			case POSTag::vx:
			case POSTag::vcn:
			case POSTag::xpn:
			case POSTag::xr:
			case POSTag::sw:
			case POSTag::sh:
			case POSTag::w_email:
			case POSTag::w_hashtag:
			case POSTag::w_url:
			case POSTag::w_mention:
				return true;
			default:
				return false;
			}
			return false;
		}

		inline char16_t getLastValidChr(const KString& str)
		{
			for (auto it = str.rbegin(); it != str.rend(); ++it)
			{
				switch (identifySpecialChr(*it))
				{
				case POSTag::sl:
				case POSTag::sn:
				case POSTag::sh:
				case POSTag::max:
					return *it;
				default:
					break;
				}
			}
			return 0;
		}

		void Joiner::add(U16StringView form, POSTag tag, Space space)
		{
			KString normForm = normalizeHangul(form);
			if (stack.size() == activeStart)
			{
				ranges.emplace_back(stack.size(), stack.size() + normForm.size());
				stack += normForm;
				lastTag = tag;
				return;
			}

			if (space == Space::insert_space || (space == Space::none && isSpaceInsertable(clearIrregular(lastTag), clearIrregular(tag), form)))
			{
				if (stack.empty() || !isSpace(stack.back())) stack.push_back(u' ');
				activeStart = stack.size();
				ranges.emplace_back(stack.size(), stack.size() + normForm.size());
				stack += normForm;
			}
			else
			{
				CondVowel cv = CondVowel::non_vowel;
				if (activeStart > 0)
				{
					cv = isHangulSyllable(stack[activeStart - 1]) ? CondVowel::vowel : CondVowel::non_vowel;
				}

				if (!stack.empty() && (isJClass(tag) || isEClass(tag)))
				{
					if (isEClass(tag) && normForm[0] == u'아') normForm[0] = u'어';
					char16_t c = getLastValidChr(stack);
					CondVowel lastCv = isHangulCoda(c) ? CondVowel::non_vowel : CondVowel::vowel;
					bool lastCvocalic = (lastCv == CondVowel::vowel || c == u'\u11AF');
					auto it = cr->allomorphPtrMap.find(make_pair(normForm, tag));
					if (it != cr->allomorphPtrMap.end())
					{
						size_t ptrBegin = it->second.first;
						size_t ptrEnd = it->second.second;
						CondVowel matchedCond = CondVowel::none;
						KString bestNormForm;
						for (size_t i = ptrBegin; i < ptrEnd; ++i)
						{
							auto& m = cr->allomorphData[i];
							if (matchedCond == CondVowel::none && ((m.cvowel == CondVowel::vocalic && lastCvocalic) || m.cvowel == lastCv))
							{
								bestNormForm = m.form;
								matchedCond = m.cvowel;
							}
							else if (matchedCond != CondVowel::none)
							{
								if (matchedCond == m.cvowel)
								{
									if (normForm == m.form) bestNormForm = m.form;
								}
								else
								{
									break;
								}
							}
						}
						normForm = bestNormForm;
					}
					// 어미 뒤에 보조사 '요'가 오는 경우, '요'->'이요'로 치환금지
					if (isEClass(lastTag) && tag == POSTag::jx && normForm[0] == u'이') normForm.erase(normForm.begin());
				}

				// 대명사가 아닌 다른 품사 뒤에 서술격 조사가 오는 경우 생략 금지
				if (anteLastTag != POSTag::np && lastTag == POSTag::vcp && isHangulCoda(normForm[0]))
				{
					cv = CondVowel::none;
				}
				auto r = cr->combineOneImpl({ stack.data() + activeStart, stack.size() - activeStart }, lastTag, normForm, tag, cv);
				stack.erase(stack.begin() + activeStart, stack.end());
				ranges.back().second = activeStart + get<1>(r);
				ranges.emplace_back(activeStart + get<2>(r), activeStart + get<0>(r).size());
				stack += get<0>(r);
				activeStart += get<2>(r);
			}
			anteLastTag = lastTag;
			lastTag = tag;
		}

		void Joiner::add(const u16string& form, POSTag tag, Space space)
		{
			return add(nonstd::to_string_view(form), tag, space);
		}

		void Joiner::add(const char16_t* form, POSTag tag, Space space)
		{
			return add(U16StringView{ form }, tag, space);
		}

		u16string Joiner::getU16(vector<pair<uint32_t, uint32_t>>* rangesOut) const
		{
			if (rangesOut)
			{
				rangesOut->clear();
				rangesOut->reserve(ranges.size());
				Vector<uint32_t> u16pos;
				auto ret = joinHangul(stack.begin(), stack.end(), u16pos);
				u16pos.emplace_back(ret.size());
				for (auto& r : ranges)
				{
					auto endOffset = u16pos[r.second] + (r.second > 0 && u16pos[r.second - 1] == u16pos[r.second] ? 1 : 0);
					rangesOut->emplace_back(u16pos[r.first], endOffset);
				}
				return ret;
			}
			else
			{
				return joinHangul(stack);
			}
		}

		string Joiner::getU8(vector<pair<uint32_t, uint32_t>>* rangesOut) const
		{
			auto u16 = getU16(rangesOut);
			if (rangesOut)
			{
				Vector<uint32_t> positions;
				auto ret = utf16To8(u16, positions);
				for (auto& r : *rangesOut)
				{
					r.first = positions[r.first];
					r.second = positions[r.second];
				}
				return ret;
			}
			else
			{
				return utf16To8(u16);
			}
		}

		AutoJoiner::~AutoJoiner()
		{
			reinterpret_cast<CandVector&>(candBuf).~CandVector();
		}

		AutoJoiner::AutoJoiner(const AutoJoiner& o)
			: kiwi{ o.kiwi }
		{
			new (&candBuf) CandVector{ reinterpret_cast<const CandVector&>(o.candBuf) };
		}

		AutoJoiner::AutoJoiner(AutoJoiner&& o)
			: kiwi{ o.kiwi }
		{
			new (&candBuf) CandVector{ reinterpret_cast<CandVector&&>(o.candBuf) };
		}

		AutoJoiner& AutoJoiner::operator=(const AutoJoiner& o)
		{
			kiwi = o.kiwi;
			reinterpret_cast<CandVector&>(candBuf) = reinterpret_cast<const CandVector&>(o.candBuf);
			return *this;
		}

		AutoJoiner& AutoJoiner::operator=(AutoJoiner&& o)
		{
			kiwi = o.kiwi;
			reinterpret_cast<CandVector&>(candBuf) = reinterpret_cast<CandVector&&>(o.candBuf);
			return *this;
		}

		template<class LmState>
		void AutoJoiner::add(size_t morphemeId, Space space, Vector<Candidate<LmState>>& candidates)
		{
			auto& morph = kiwi->morphemes[morphemeId];
			for (auto& cand : candidates)
			{
				cand.score += cand.lmState.next(kiwi->langMdl, morph.lmMorphemeId);
				cand.joiner.add(morph.getForm(), morph.tag, space);
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
		void AutoJoiner::add(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<LmState>>& candidates)
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
						cand.score += cand.lmState.next(kiwi->langMdl, lmId);
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
							n.score += n.lmState.next(kiwi->langMdl, cands[i]->lmMorphemeId);
							n.joiner.add(form, cands[i]->tag, space);
						}
					}
					for (size_t o = 0; o < oSize; ++o)
					{
						auto& n = candidates[o];
						n.score += n.lmState.next(kiwi->langMdl, cands[0]->lmMorphemeId);
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
					cand.score += cand.lmState.next(kiwi->langMdl, lmId);
					cand.joiner.add(form, tag, space);
				}
			}
			sort(candidates.begin(), candidates.end(), [](const cmb::Candidate<LmState>& a, const cmb::Candidate<LmState>& b)
			{
				return a.score > b.score;
			});
		}

		template<ArchType arch>
		void AutoJoiner::addWithoutSearch(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<VoidState<arch>>>& candidates)
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
		void AutoJoiner::addWithoutSearch(size_t morphemeId, Space space, Vector<Candidate<VoidState<arch>>>& candidates)
		{
			auto& morph = kiwi->morphemes[morphemeId];
			for (auto& cand : candidates)
			{
				cand.joiner.add(morph.getForm(), morph.tag, space);
			}
		}

		struct AutoJoiner::AddVisitor
		{
			AutoJoiner* joiner;
			U16StringView form;
			POSTag tag;
			bool inferRegularity;
			Space space;

			AddVisitor(AutoJoiner* _joiner, U16StringView _form, POSTag _tag, bool _inferRegularity, Space _space)
				: joiner{ _joiner }, form{ _form }, tag{ _tag }, inferRegularity{ _inferRegularity }, space{ _space }
			{
			}

			template<ArchType arch>
			void operator()(Vector<Candidate<VoidState<arch>>>& o) const
			{
				return joiner->addWithoutSearch(form, tag, inferRegularity, space, o);
			}

			template<class LmState>
			void operator()(Vector<Candidate<LmState>>& o) const
			{
				return joiner->add(form, tag, inferRegularity, space, o);
			}
		};

		struct AutoJoiner::AddVisitor2
		{
			AutoJoiner* joiner;
			size_t morphemeId;
			Space space;

			AddVisitor2(AutoJoiner* _joiner, size_t _morphemeId, Space _space)
				: joiner{ _joiner }, morphemeId{ _morphemeId }, space{ _space }
			{
			}

			template<ArchType arch>
			void operator()(Vector<Candidate<VoidState<arch>>>& o) const
			{
				return joiner->addWithoutSearch(morphemeId, space, o);
			}

			template<class LmState>
			void operator()(Vector<Candidate<LmState>>& o) const
			{
				return joiner->add(morphemeId, space, o);
			}
		};

		struct GetU16Visitor
		{
			vector<pair<uint32_t, uint32_t>>* rangesOut;

			GetU16Visitor(vector<pair<uint32_t, uint32_t>>* _rangesOut)
				: rangesOut{ _rangesOut }
			{
			}

			template<class LmState>
			u16string operator()(const Vector<Candidate<LmState>>& o) const
			{
				return o[0].joiner.getU16(rangesOut);
			}
		};

		struct GetU8Visitor
		{
			vector<pair<uint32_t, uint32_t>>* rangesOut;

			GetU8Visitor(vector<pair<uint32_t, uint32_t>>* _rangesOut)
				: rangesOut{ _rangesOut }
			{
			}

			template<class LmState>
			string operator()(const Vector<Candidate<LmState>>& o) const
			{
				return o[0].joiner.getU8(rangesOut);
			}
		};

		void AutoJoiner::add(size_t morphemeId, Space space)
		{
			return mapbox::util::apply_visitor(AddVisitor2{ this, morphemeId, space }, reinterpret_cast<CandVector&>(candBuf));
		}

		void AutoJoiner::add(const u16string& form, POSTag tag, bool inferRegularity, Space space)
		{
			return mapbox::util::apply_visitor(AddVisitor{ this, nonstd::to_string_view(form), tag, inferRegularity, space }, reinterpret_cast<CandVector&>(candBuf));
		}

		void AutoJoiner::add(const char16_t* form, POSTag tag, bool inferRegularity, Space space)
		{
			return mapbox::util::apply_visitor(AddVisitor{ this, U16StringView{ form }, tag, inferRegularity, space }, reinterpret_cast<CandVector&>(candBuf));
		}

		void AutoJoiner::add(U16StringView form, POSTag tag, Space space)
		{
			return mapbox::util::apply_visitor(AddVisitor{ this, form, tag, false, space }, reinterpret_cast<CandVector&>(candBuf));
		}

		u16string AutoJoiner::getU16(vector<pair<uint32_t, uint32_t>>* rangesOut) const
		{
			return mapbox::util::apply_visitor(GetU16Visitor{ rangesOut }, reinterpret_cast<const CandVector&>(candBuf));
		}

		string AutoJoiner::getU8(vector<pair<uint32_t, uint32_t>>* rangesOut) const
		{
			return mapbox::util::apply_visitor(GetU8Visitor{ rangesOut }, reinterpret_cast<const CandVector&>(candBuf));
		}
	}
}
