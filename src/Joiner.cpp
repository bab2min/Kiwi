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
		Joiner::Joiner(Joiner&&) = default;
		Joiner& Joiner::operator=(const Joiner&) = default;
		Joiner& Joiner::operator=(Joiner&&) = default;

		inline bool isSpaceInsertable(POSTag l, POSTag r)
		{
			if (r == POSTag::vcp || r == POSTag::xsa || r == POSTag::xsai || r == POSTag::xsv || r == POSTag::xsn) return false;
			if (l == POSTag::xpn || l == POSTag::so || l == POSTag::ss || l == POSTag::sw) return false;
			if (l == POSTag::sn && r == POSTag::nnb) return false;
			if (!(l == POSTag::sn || l == POSTag::sp || l == POSTag::sf || l == POSTag::sl)
				&& (r == POSTag::sl || r == POSTag::sn)) return true;
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
			}
			return false;
		}

		void Joiner::add(U16StringView form, POSTag tag)
		{
			if (stack.size() == activeStart)
			{
				stack += normalizeHangul(form);
				lastTag = tag;
				return;
			}

			if (isSpaceInsertable(clearIrregular(lastTag), clearIrregular(tag)))
			{
				stack.push_back(u' ');
				activeStart = stack.size();
				stack += normalizeHangul(form);
			}
			else
			{
				CondVowel cv = CondVowel::none;
				if (activeStart > 0)
				{
					cv = isHangulCoda(stack[activeStart - 1]) ? CondVowel::non_vowel : CondVowel::vowel;
				}

				auto normForm = normalizeHangul(form);

				if (!stack.empty() && (isJClass(tag) || isEClass(tag)))
				{
					if (isEClass(tag) && normForm[0] == u'아') normForm[0] = u'어';

					CondVowel lastCv = lastCv = isHangulCoda(stack.back()) ? CondVowel::non_vowel : CondVowel::vowel;
					bool lastCvocalic = lastCvocalic = (lastCv == CondVowel::vowel || stack.back() == u'\u11AF');
					auto it = cr->allomorphPtrMap.find(make_pair(normForm, tag));
					if (it != cr->allomorphPtrMap.end())
					{
						size_t ptrBegin = it->second.first;
						size_t ptrEnd = it->second.second;
						for (size_t i = ptrBegin; i < ptrEnd; ++i)
						{
							auto& m = cr->allomorphData[i];
							if (m.second == CondVowel::vocalic && lastCvocalic || m.second == lastCv)
							{
								normForm = m.first;
								break;
							}
						}
					}
				}

				auto r = cr->combineOneImpl({ stack.data() + activeStart, stack.size() - activeStart }, lastTag, normForm, tag, cv);
				stack.erase(stack.begin() + activeStart, stack.end());
				stack += r.first;
				activeStart += r.second;
			}
			lastTag = tag;
		}

		void Joiner::add(const u16string& form, POSTag tag)
		{
			return add(nonstd::to_string_view(form), tag);
		}

		void Joiner::add(const char16_t* form, POSTag tag)
		{
			return add(U16StringView{ form }, tag);
		}

		u16string Joiner::getU16() const
		{
			return joinHangul(stack);
		}

		string Joiner::getU8() const
		{
			return utf16To8(joinHangul(stack));
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
		void AutoJoiner::add(U16StringView form, POSTag tag, bool inferRegularity, Vector<Candidate<LmState>>& candidates)
		{
			const Form* formHead;
			auto node = kiwi->formTrie.root();
			for (auto c : normalizeHangul(form))
			{
				node = node->template nextOpt<LmState::arch>(kiwi->formTrie, c);
				if (!node) break;
			}

			if (node && (formHead = node->val(kiwi->formTrie)) != nullptr)
			{
				Vector<const Morpheme*> cands;
				do
				{
					for (auto m : formHead->candidate)
					{
						if (inferRegularity && clearIrregular(m->tag) == clearIrregular(tag))
						{
							cands.emplace_back(m);
						}
						else if (!inferRegularity && m->tag == tag)
						{
							cands.emplace_back(m);
						}
					}
					++formHead;
				} while (formHead[-1].form == formHead[0].form);

				if (cands.size() <= 1)
				{
					auto lmId = cands.empty() ? getDefaultMorphemeId(clearIrregular(tag)) : cands[0]->lmMorphemeId;
					if (!cands.empty()) tag = cands[0]->tag;
					for (auto& cand : candidates)
					{
						cand.score += cand.lmState.next(kiwi->langMdl, lmId);
						cand.joiner.add(form, tag);
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
							n.joiner.add(form, cands[i]->tag);
						}
					}
					for (size_t o = 0; o < oSize; ++o)
					{
						auto& n = candidates[o];
						n.score += n.lmState.next(kiwi->langMdl, cands[0]->lmMorphemeId);
						n.joiner.add(form, cands[0]->tag);
					}
				}
			}
			else
			{
				auto lmId = getDefaultMorphemeId(clearIrregular(tag));
				for (auto& cand : candidates)
				{
					cand.score += cand.lmState.next(kiwi->langMdl, lmId);
					cand.joiner.add(form, tag);
				}
			}
			sort(candidates.begin(), candidates.end(), [](const cmb::Candidate<LmState>& a, const cmb::Candidate<LmState>& b)
			{
				return a.score > b.score;
			});
		}

		template<ArchType arch>
		void AutoJoiner::addWithoutSearch(U16StringView form, POSTag tag, bool inferRegularity, Vector<Candidate<VoidState<arch>>>& candidates)
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
						do
						{
							for (auto m : formHead->candidate)
							{
								if (clearIrregular(m->tag) == clearIrregular(tag))
								{
									cands.emplace_back(m);
								}
							}
							++formHead;
						} while (formHead[-1].form == formHead[0].form);

						if (!cands.empty())
						{
							tag = cands[0]->tag;
						}
					}
				}
			}
			candidates[0].joiner.add(form, tag);
		}

		struct AutoJoiner::AddVisitor
		{
			AutoJoiner* joiner;
			U16StringView form;
			POSTag tag;
			bool inferRegularity;

			AddVisitor(AutoJoiner* _joiner, U16StringView _form, POSTag _tag, bool _inferRegularity)
				: joiner{ _joiner }, form{ _form }, tag{ _tag }, inferRegularity{ _inferRegularity }
			{
			}

			template<ArchType arch>
			void operator()(Vector<Candidate<VoidState<arch>>>& o) const
			{
				return joiner->addWithoutSearch(form, tag, inferRegularity, o);
			}

			template<class LmState>
			void operator()(Vector<Candidate<LmState>>& o) const
			{
				return joiner->add(form, tag, inferRegularity, o);
			}
		};

		struct GetU16Visitor
		{
			template<class LmState>
			u16string operator()(const Vector<Candidate<LmState>>& o) const
			{
				return o[0].joiner.getU16();
			}
		};

		struct GetU8Visitor
		{
			template<class LmState>
			string operator()(const Vector<Candidate<LmState>>& o) const
			{
				return o[0].joiner.getU8();
			}
		};

		void AutoJoiner::add(const u16string& form, POSTag tag, bool inferRegularity)
		{
			return mapbox::util::apply_visitor(AddVisitor{ this, nonstd::to_string_view(form), tag, inferRegularity }, reinterpret_cast<CandVector&>(candBuf));
		}

		void AutoJoiner::add(const char16_t* form, POSTag tag, bool inferRegularity)
		{
			return mapbox::util::apply_visitor(AddVisitor{ this, U16StringView{ form }, tag, inferRegularity }, reinterpret_cast<CandVector&>(candBuf));
		}

		u16string AutoJoiner::getU16() const
		{
			return mapbox::util::apply_visitor(GetU16Visitor{}, reinterpret_cast<const CandVector&>(candBuf));
		}

		string AutoJoiner::getU8() const
		{
			return mapbox::util::apply_visitor(GetU8Visitor{}, reinterpret_cast<const CandVector&>(candBuf));
		}
	}
}
