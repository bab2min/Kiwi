#pragma once
#include <kiwi/Kiwi.h>

namespace kiwi
{
	using FnNewJoiner = cmb::AutoJoiner(*)(const Kiwi*);

	template<class LmState>
	cmb::AutoJoiner Kiwi::newJoinerImpl() const
	{
		return cmb::AutoJoiner{ *this, cmb::Candidate<LmState>{ *combiningRule, langMdl.get() }};
	}

	template<class LmState>
	cmb::AutoJoiner newJoinerWithKiwi(const Kiwi* kiwi)
	{
		return kiwi->newJoinerImpl<LmState>();
	}
}
