#include <kiwi/Joiner.h>
#include <kiwi/Kiwi.h>
#include "Combiner.h"
#include "StrUtils.h"

using namespace std;
using namespace kiwi;
using namespace kiwi::cmb;

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


AutoJoiner::AutoJoiner(const Kiwi& _kiwi) : Joiner{ *_kiwi.combiningRule }, kiwi{&_kiwi} {}
AutoJoiner::~AutoJoiner() = default;
AutoJoiner::AutoJoiner(const AutoJoiner&) = default;
AutoJoiner::AutoJoiner(AutoJoiner&&) = default;
AutoJoiner& AutoJoiner::operator=(const AutoJoiner&) = default;
AutoJoiner& AutoJoiner::operator=(AutoJoiner&&) = default;

void AutoJoiner::add(U16StringView form, POSTag tag, bool inferRegularity)
{
	if (inferRegularity)
	{
		
	}
	return Joiner::add(form, tag);
}

void AutoJoiner::add(const u16string& form, POSTag tag, bool inferRegularity)
{
	return add(nonstd::to_string_view(form), tag, inferRegularity);
}

void AutoJoiner::add(const char16_t* form, POSTag tag, bool inferRegularity)
{
	return add(U16StringView{ form }, tag, inferRegularity);
}
