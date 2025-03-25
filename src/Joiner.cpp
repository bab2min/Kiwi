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
			if (l == POSTag::z_siot && isNNClass(r)) return false;

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
			return add(toStringView(form), tag, space);
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

		AutoJoiner::~AutoJoiner() = default;

		AutoJoiner::AutoJoiner(const AutoJoiner& o) = default;

		AutoJoiner::AutoJoiner(AutoJoiner&& o) = default;

		AutoJoiner& AutoJoiner::operator=(const AutoJoiner& o) = default;

		AutoJoiner& AutoJoiner::operator=(AutoJoiner&& o) = default;

		void AutoJoiner::add(size_t morphemeId, Space space)
		{
			return (*dfAdd)(this, morphemeId, space, candBuf.get<Candidate<lm::VoidState<ArchType::none>>>());
		}

		void AutoJoiner::add(const u16string& form, POSTag tag, bool inferRegularity, Space space)
		{
			return (*dfAdd2)(this, toStringView(form), tag, inferRegularity, space, candBuf.get<Candidate<lm::VoidState<ArchType::none>>>());
		}

		void AutoJoiner::add(const char16_t* form, POSTag tag, bool inferRegularity, Space space)
		{
			return (*dfAdd2)(this, U16StringView{ form }, tag, inferRegularity, space, candBuf.get<Candidate<lm::VoidState<ArchType::none>>>());
		}

		void AutoJoiner::add(U16StringView form, POSTag tag, Space space)
		{
			return (*dfAdd2)(this, form, tag, false, space, candBuf.get<Candidate<lm::VoidState<ArchType::none>>>());
		}

		u16string AutoJoiner::getU16(vector<pair<uint32_t, uint32_t>>* rangesOut) const
		{
			return candBuf.get<Candidate<lm::VoidState<ArchType::none>>>()[0].joiner.getU16(rangesOut);
		}

		string AutoJoiner::getU8(vector<pair<uint32_t, uint32_t>>* rangesOut) const
		{
			return candBuf.get<Candidate<lm::VoidState<ArchType::none>>>()[0].joiner.getU8(rangesOut);
		}
	}
}
