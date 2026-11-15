#include <cassert>
#include <algorithm>
#include <kiwi/Utils.h>
#include <kiwi/Form.h>
#include "serializer.hpp"

using namespace std;

namespace kiwi
{
	MorphemeRaw::MorphemeRaw() = default;

	MorphemeRaw::~MorphemeRaw() = default;

	MorphemeRaw::MorphemeRaw(const MorphemeRaw&) = default;

	MorphemeRaw::MorphemeRaw(MorphemeRaw&&) noexcept = default;

	MorphemeRaw& MorphemeRaw::operator=(const MorphemeRaw&) = default;

	MorphemeRaw& MorphemeRaw::operator=(MorphemeRaw&&) = default;

	MorphemeRaw::MorphemeRaw(
		POSTag _tag,
		CondVowel _vowel,
		CondPolarity _polar,
		bool _complex,
		uint8_t _combineSocket)
		: tag(_tag), combineSocket(_combineSocket)
	{
		setVowel(_vowel);
		setPolar(_polar);
		setComplex(_complex);
	}

	DEFINE_SERIALIZER_OUTSIDE(MorphemeRaw, kform, tag, vpPack, senseId, combineSocket, combined, userScore, chunks, chunkPositions, lmMorphemeId, groupId, dialect, _reserved);

	Morpheme::Morpheme() = default;

	Morpheme::~Morpheme() = default;

	Morpheme::Morpheme(const Morpheme&) = default;

	Morpheme::Morpheme(Morpheme&&) noexcept = default;

	Morpheme& Morpheme::operator=(const Morpheme&) = default;

	Morpheme& Morpheme::operator=(Morpheme&&) = default;

	FormRaw::FormRaw() = default;

	FormRaw::~FormRaw() = default;

	FormRaw::FormRaw(const FormRaw&) = default;

	FormRaw::FormRaw(FormRaw&&) noexcept = default;

	FormRaw& FormRaw::operator=(const FormRaw&) = default;

	FormRaw& FormRaw::operator=(FormRaw&&) = default;

	FormRaw::FormRaw(const KString& _form)
		: form(_form)
	{}

	bool FormRaw::operator<(const FormRaw& o) const
	{
		return form < o.form;
	}

	DEFINE_SERIALIZER_OUTSIDE(FormRaw, form, candidate);

	Form::Form()
		: formHash(0), 
		zCodaAppendable(0), 
		zSiotAppendable(0), 
		hasJClass(0), 
		hasAnyFullMorphemes(0), 
		hasQuotedHaForm(0), 
		dialect(Dialect::standard)
	{
	}

	Form::~Form() = default;

	Form::Form(const Form&) = default;

	Form::Form(Form&&) noexcept = default;

	Form& Form::operator=(const Form&) = default;

	Form& Form::operator=(Form&&) = default;

	bool Form::operator<(const Form& o) const
	{
		return ComparatorIgnoringSpace::less(form, o.form);
	}

	void Form::setCandidates(const Morpheme* const* data, size_t size)
	{
		if (size > UINT16_MAX) throw std::length_error{ "Form has too many candidates" };
		candidateData = data;
		candidateSize = (uint16_t)size;
	}

	Form bake(const FormRaw& o, const Morpheme* morphBase, bool zCodaAppendable, bool zSiotAppendable,
		Vector<const Morpheme*>& candidatePool, const Vector<uint32_t>& additionalCands)
	{
		Form ret;
		ret.numSpaces = (uint16_t)count(o.form.begin(), o.form.end(), u' ');
		ret.form = o.form;
		const size_t first = candidatePool.size();
		for (auto c : o.candidate) candidatePool.emplace_back(morphBase + c);
		for (auto c : additionalCands) candidatePool.emplace_back(morphBase + c);
		ret.setCandidates(candidatePool.data() + first, candidatePool.size() - first);
		ret.zCodaAppendable = zCodaAppendable ? 1 : 0;
		ret.zSiotAppendable = zSiotAppendable ? 1 : 0;
		// 다음 값들은 KiwiBuilder::build에서 채워 넣는다
		ret.hasJClass = 0;
		ret.hasAnyFullMorphemes = 0;
		ret.hasQuotedHaForm = 0;
		return ret;
	}

	std::pair<const Morpheme**, std::pair<uint8_t, uint8_t>*> Morpheme::allocChunks(Vector<const Morpheme*>& pool, size_t n)
	{
		if (n > UINT8_MAX) throw std::length_error{ "Morpheme has too many chunks" };
		if (!n)
		{
			chunkData = nullptr;
			chunkSize = 0;
			return { nullptr, nullptr };
		}
		const size_t first = pool.size(), slots = chunkSlots(n);
		if (first + slots > pool.capacity()) throw std::logic_error{ "chunk pool must be reserved in advance" };
		pool.resize(first + slots);
		const Morpheme** morphs = pool.data() + first;
		auto* positions = reinterpret_cast<std::pair<uint8_t, uint8_t>*>(morphs + n);
		for (size_t i = 0; i < n; ++i) new (positions + i) std::pair<uint8_t, uint8_t>{};
		chunkData = morphs;
		chunkSize = (uint8_t)n;
		return { morphs, positions };
	}

	Morpheme bake(const MorphemeRaw& o, const Morpheme* morphBase, const Form* formBase, const Vector<size_t>& formMap,
		Vector<const Morpheme*>& chunkPool)
	{
		Morpheme ret;
		ret.kform = &formBase[formMap[o.kform]].form;
		ret.tag = o.tag;
		ret.vowel = o.vowel();
		ret.polar = o.polar();
		ret.combineSocket = o.combineSocket;
		ret.combined = o.combined;
		ret.userScore = o.userScore;
		ret.lmMorphemeId = o.lmMorphemeId;
		ret.origMorphemeId = o.origMorphemeId;
		ret.senseId = o.senseId;
		auto [chunkMorphs, chunkPositions] = ret.allocChunks(chunkPool, o.chunks.size());
		for (size_t i = 0; i < o.chunks.size(); ++i)
		{
			chunkMorphs[i] = morphBase + o.chunks[i];
			chunkPositions[i] = o.chunkPositions[i];
		}
		// chunks can point to morphemes not baked yet; saisiot is set in KiwiBuilder::build
		ret.complex = o.complex();
		ret.saisiot = false;

		ret.dialect = o.dialect;
		return ret;
	}

	std::ostream& Morpheme::print(std::ostream& os) const
	{
		os << utf16To8(kform ? joinHangul(*kform) : u"_");
		os << '/';
		os << tagToString(tag);
		if (combineSocket) os << '+' << (size_t)combineSocket;
		return os;
	}
}
