/**
 * @file Form.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 형태 및 형태소에 관한 정보를 담는 구조체들이 선언된 헤더
 * 
 * 
 */

#pragma once

#include <cmath>
#include <kiwi/Types.h>

namespace kiwi
{
	struct Morpheme;

	/** 연속 배열의 일부를 가리키는 읽기 전용 범위 */
	template<class Ty>
	class ConstRange
	{
		const Ty* first = nullptr;
		size_t n = 0;
	public:
		ConstRange() = default;
		ConstRange(const Ty* _first, size_t _n) : first{ _first }, n{ _n } {}

		const Ty* begin() const { return first; }
		const Ty* end() const { return first + n; }
		size_t size() const { return n; }
		bool empty() const { return n == 0; }
		const Ty& operator[](size_t i) const { return first[i]; }
	};

	/**
	 * @brief 형태소에 관한 모든 정보를 담는 구조체의 템플릿
	 * 
	 * @note 변경가능한 상태로 인덱스와 관련된 값이나 std::vector 등의 길이를 변경할 수 있음.
	 * `kiwi::KiwiBuilder`에서 사용한다.
	 */
	struct MorphemeRaw
	{
		uint32_t kform = 0; /**< 형태에 대한 포인터 */
		POSTag tag = POSTag::unknown; /**< 품사 태그 */
		uint8_t vpPack = 0; /**< CondVowel, CondPolarity, complex를 각각 4, 3, 1비트로 묶어서 합친 값 */
		uint8_t senseId = 0; /**< 의미 번호 */

		/**
		 * @brief 형태소가 두 부분으로 분할된 경우 결합 번호를 표기하기 위해 사용된다.
		 *
		 * @note `덥/VA`, `춥/VA` 등의 형태소는 `어/EC`와 만나면 `더워`, `추워`와 같이 형태가 변화한다.
		 * 이 경우를 각각 처리하기 보다는 `더/V + ㅂ/V`, `추/V + ㅂ/V`과 같이 분해하면
		 * `ㅂ/V` + `어/EC`가 `워`로 변한다는 규칙만으로 처리가 가능해진다. (이 규칙은 `chunks`를 이용해 형태소 정보에 담길 수 있음)
		 * 그러나 모든 ㅂ으로 끝나는 형태소가 위와 같은 규칙에 결합되면 안된다.
		 * 예를 들어 `굽/VA`의 경우 `어/EC`와 만나도 `굽어`라고 형태가 유지되기 때문.
		 * 따라서 `ㅂ/V`이 결합할 수 있는 조건을 명시해서 이 조건과 맞는 경우에만 `더/V + ㅂ/V` -> `덥/VA`과 같이 복원해야 한다.
		 * `combineSocket`이 0이면 이런 결합 조건이 없는 일반 형태소임을 뜻하며, 0이 아닌 경우 결합 조건을 가지고 분해된 형태소임을 뜻한다.
		 * `더/V`와 `워/UNK`(`ㅂ/V + 어/EC`)는 예를 들어 3과 같이 동일한 combineSocket을 할당해 둘이 서로 결합이 가능한 형태소임을 식별한다.
		 */
		uint8_t combineSocket = 0;

		/**
		 * @brief 여러 형태소가 결합되어 형태가 변경된 경우, 원 형태소 목록을 표기하기 위해 사용된다.
		 *
		 * @note `되/VV + 어/EC`의 결합은 `돼`라는 형태로 축약될 수 있다.
		 * 분석과정에서 `돼`를 만난 경우 역으로 `되/VV + 어/EC`로 분석할 수 있도록 `돼/UNK`를 더미 형태소로 등록하고
		 * chunks에는 `되/VV`와 `어/EC`에 대한 포인터를 넣어둔다.
		 */
		Vector<uint32_t> chunks;

		/**
		 * @brief 여러 형태소가 결합되어 형태가 변경된 경우, 원 형태소의 위치 정보를 표기하기 위해 사용된다.
		 *
		 * @note pair.first는 시작 지점, pair.second는 끝 지점을 나타낸다. chunkPositions.size()는 항상 chunks.size()와 같다.
		 */
		Vector<std::pair<uint8_t, uint8_t>> chunkPositions;

		/**
		 * @brief 분할된 형태소의 원형 형태소를 가리키는 오프셋
		 *
		 * @note `덥/VA`이 `더/V` + `ㅂ/V`으로 분할된 경우 `더/V`는 `덥/VA`에 대한 오프셋을 combined에 저장해둔다.
		 * `kiwi::Morpheme::getCombined()`를 통해 원형 형태소의 포인터를 구할 수 있음
		 * @sa combineSocket
		 */
		int32_t combined = 0;
		float userScore = 0;

		/**
		 * @brief 형태소의 언어모델 상의 인덱스 값을 보관하는 필드.
		 * 
		 * @note 대부분의 경우는 형태소 자체의 인덱스 값과 동일하지만, 
		 * 이형태 형태소의 경우 원형 형태소의 인덱스 값을 가진다.
		 */
		uint32_t lmMorphemeId = 0;
		uint32_t origMorphemeId = 0;

		/**
		 * @brief 형태소의 그룹 인덱스.
		 * 
		 * @note 형태소 결합 시 음운 조건에 따라 이형태를 선택해야 하는 경우
		   그룹 인덱스가 동일한 형태소만 선택된다.
		 */
		uint32_t groupId = 0;
		Dialect dialect = Dialect::standard; /**< 형태소의 방언 정보. 기본값은 표준어. */
		uint16_t _reserved = 0; /**< 패딩을 위한 필드. */

		MorphemeRaw();
		~MorphemeRaw();
		MorphemeRaw(const MorphemeRaw&);
		MorphemeRaw(MorphemeRaw&&) noexcept;
		MorphemeRaw& operator=(const MorphemeRaw&);
		MorphemeRaw& operator=(MorphemeRaw&&);

		MorphemeRaw(
			POSTag _tag,
			CondVowel _vowel = CondVowel::none,
			CondPolarity _polar = CondPolarity::none,
			bool _complex = false,
			uint8_t _combineSocket = 0
		);

		/**< 선행형태소의 자/모음 조건 */
		CondVowel vowel() const { return static_cast<CondVowel>(vpPack & 0xF); }
		
		/**< 선행형태소의 모음조화 조건 */
		CondPolarity polar() const { return static_cast<CondPolarity>((vpPack >> 4) & 0x7); }

		/**< 추가 분석이 가능한 형태소인지(파생어나 사이시옷이 포함된 합성명사 등) */
		bool complex() const { return !!(vpPack & 0x80); }

		void setVowel(CondVowel v)
		{
			vpPack = (vpPack & 0xF0) | static_cast<uint8_t>(v);
		}

		void setPolar(CondPolarity v)
		{
			vpPack = (vpPack & 0x8F) | (static_cast<uint8_t>(v) << 4);
		}

		void setComplex(bool v)
		{
			vpPack = (vpPack & 0x7F) | (v ? 0x80 : 0x00);
		}

		void serializerRead(std::istream& istr);
		void serializerWrite(std::ostream& ostr) const;
	};

	/**
	 * @brief 형태소에 관한 모든 정보를 담는 구조체의 템플릿
	 * 
	 * @note 변경 불가능한 상태로 인덱스는 모두 포인터로 변경되고, 조각 목록은 소유한 쪽(Kiwi::morphemeChunks 등)의
	 * 연속 배열을 가리킨다. `kiwi::Kiwi` 내 실제 형태소 분석 단계에 쓰인다.
	 */
	struct Morpheme
	{
		const KString* kform = nullptr;
		POSTag tag = POSTag::unknown;
		CondVowel vowel : 4;
		CondPolarity polar : 2;
		bool complex : 1;
		bool saisiot : 1;
		uint8_t senseId = 0;
		uint8_t combineSocket = 0;
		int32_t combined = 0;
		const Morpheme* const* chunkData = nullptr;
		float userScore = 0;
		uint32_t lmMorphemeId = 0, origMorphemeId = 0;
		Dialect dialect = Dialect::standard;
		uint8_t chunkSize = 0;

		Morpheme();
		~Morpheme();
		Morpheme(const Morpheme&);
		Morpheme(Morpheme&&) noexcept;
		Morpheme& operator=(const Morpheme&);
		Morpheme& operator=(Morpheme&&);

		std::ostream& print(std::ostream& os) const;

		/** 형태소의 형태를 반환한다. */
		const KString& getForm() const { return *kform; }

		/** 분할된 형태소의 경우 원형 형태소를 반환한다. 그 외에는 자기 자신을 반환한다. */
		const Morpheme* getCombined() const { return this + combined; }

		ConstRange<const Morpheme*> chunks() const { return { chunkData, chunkSize }; }

		/** 조각별 (시작, 끝) 위치. 풀에서 조각 형태소 포인터들 바로 뒤에 놓인다 */
		ConstRange<std::pair<uint8_t, uint8_t>> chunkPositions() const
		{
			return { reinterpret_cast<const std::pair<uint8_t, uint8_t>*>(chunkData + chunkSize), chunkSize };
		}

		/** 조각 n개가 풀에서 차지하는 칸 수(형태소 포인터 n개 + 위치 쌍 n개) */
		static constexpr size_t chunkSlots(size_t n)
		{
			return n + (n * sizeof(std::pair<uint8_t, uint8_t>) + sizeof(const Morpheme*) - 1) / sizeof(const Morpheme*);
		}

		/**
		 * pool 끝에 조각 n개의 칸을 덧붙여 이 형태소에 연결하고, 채워 넣을 형태소 포인터 배열과 위치 쌍 배열을 돌려준다.
		 * pool은 다시 할당되지 않도록 미리 reserve되어 있어야 한다.
		 */
		std::pair<const Morpheme**, std::pair<uint8_t, uint8_t>*> allocChunks(Vector<const Morpheme*>& pool, size_t n);

		/** 현재 인스턴스가 단일 형태소인지 확인한다 */
		bool isSingle() const { return chunkSize == 0 || complex || saisiot; }

		bool hasComplex() const
		{
			if (getCombined()->complex) return true;

			for (auto c : chunks())
			{
				if (c->complex) return true;
			}
			return false;
		}

		template<class Container>
		bool hasMorpheme(Container&& m) const
		{
			if (m.count(getCombined())) return true;
			for (auto c : chunks())
			{
				if (m.count(c)) return true;
			}
			return false;
		}

	};

	/**
	 * @brief 형태에 관한 모든 정보를 담는 구조체의 템플릿
	 * 
	 * @note 변경가능한 상태로 인덱스와 관련된 값이나 std::vector 등의 길이를 변경할 수 있음. `kiwi::KiwiBuilder`에서 사용한다.
	 */
	struct FormRaw
	{
		KString form; /**< 형태 */
		Vector<uint32_t> candidate;
		/**< 이 형태에 해당하는 형태소들의 목록 */

		FormRaw();
		~FormRaw();
		FormRaw(const FormRaw&);
		FormRaw(FormRaw&&) noexcept;
		FormRaw& operator=(const FormRaw&);
		FormRaw& operator=(FormRaw&&);
		
		FormRaw(const KString& _form);
		bool operator<(const FormRaw& o) const;

		void serializerRead(std::istream& istr);
		void serializerWrite(std::ostream& ostr) const;
	};

	/**
	 * @brief 형태에 관한 모든 정보를 담는 구조체
	 * 
	 * @note 이 구조체는 변경 불가능한 상태로 사용된다. 인덱스는 모두 포인터로 변경되고, 후보 형태소 목록은 소유한 쪽(Kiwi::formCandidates 등)의
	 * 연속 배열을 가리킨다. `kiwi::Kiwi` 내에서 실제 형태소 분석 단계에 쓰인다.
	 */
	struct Form
	{
		KString form;
		const Morpheme* const* candidateData = nullptr;
		uint16_t numSpaces = 0;
		uint16_t candidateSize = 0;
		CondVowel vowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;
		// 아래 비트필드는 모두 16비트 타입이어야 MSVC에서도 2바이트 한 단위에 들어간다
		/** 형태가 바뀔 때마다 0과 1이 번갈아 바뀐다. 이웃한 Form이 같은 형태인지 판별하는 데 쓴다 */
		uint16_t formHash : 1;
		uint16_t zCodaAppendable : 1;
		uint16_t zSiotAppendable : 1;
		uint16_t hasJClass : 1;
		uint16_t hasAnyFullMorphemes : 1;
		/** 
		 * 1이면 바로 다음 Form(this + 1)이 
		 * 같은 형태의 '하/VV + 어미' 후보만 담은 Form이 됨
		 * (Match::quotedHaOmission 용) */
		uint16_t hasQuotedHaForm : 1;
		Dialect dialect : 10;

		Form();
		~Form();
		Form(const Form&);
		Form(Form&&) noexcept;
		Form& operator=(const Form&);
		Form& operator=(Form&&);

		// Form을 정렬하는 데에 사용. Form::form에서 공백 문자를 제거한 뒤 사전식으로 정렬.
		bool operator<(const Form& o) const;

		size_t sizeWithoutSpace() const { return form.size() - numSpaces; }

		ConstRange<const Morpheme*> candidate() const { return { candidateData, candidateSize }; }

		/** data는 Form보다 오래 살고 다시 할당되지 않는 배열이어야 한다 */
		void setCandidates(const Morpheme* const* data, size_t size);
	};

	struct TypoForm
	{
		uint32_t formId = 0;
		float scoreHash = 0;
		uint32_t typoId = 0;
		uint16_t numSpaces = 0;
		CondVowel leftCond = CondVowel::none;
		Dialect dialect = Dialect::standard;

		TypoForm() = default;

		TypoForm(const std::tuple<uint32_t, float, uint16_t, CondVowel, Dialect>& p)
			: formId{ std::get<0>(p) }, 
			scoreHash{ std::get<1>(p) }, 
			numSpaces{ std::get<2>(p) }, 
			leftCond{ std::get<3>(p) }, 
			dialect{ std::get<4>(p) }
		{
		}

		TypoForm(uint32_t _formId, float _score = 0, bool _hash = 0, 
			uint32_t _typoId = 0, 
			uint16_t _numSpaces = 0, 
			CondVowel _leftCond = CondVowel::none,
			Dialect _dialect = Dialect::standard
			)
			: formId{ _formId }, scoreHash{ _hash ? -_score : _score }, 
			typoId{ _typoId }, 
			numSpaces{ _numSpaces }, 
			leftCond{ _leftCond }, 
			dialect{ _dialect }
		{
		}

		const Form& form(const Form* data) const { return data[formId]; }
		float score() const { return std::abs(scoreHash); }
		bool hash() const { return reinterpret_cast<const int32_t&>(scoreHash) < 0; }
	};

	/**
	 * @brief 변경가능한 형태 정보를 bake하여 최적화한다.
	 * 
	 * @param o 변경 가능한 형태 정보
	 * @param morphBase 형태소 배열의 시작 위치
	 * @return 최적화된 형태 정보
	 */
	/** 후보 형태소 포인터는 candidatePool 끝에 덧붙인다. candidatePool은 미리 충분히 reserve되어 있어야 한다 */
	Form bake(const FormRaw& o, const Morpheme* morphBase, bool zCodaAppendable, bool zSiotAppendable,
		Vector<const Morpheme*>& candidatePool, const Vector<uint32_t>& additionalCands = {});

	/**
	 * @brief 변경 가능한 형태소 정보를 bake하여 최적화한다.
	 * 
	 * @param o 변경 가능한 형태소 정보
	 * @param morphBase 형태소 배열의 시작 위치
	 * @param formBase 형태 배열의 시작 위치
	 * @param formMap
	 * @return 최적화된 형태소 정보
	 */
	/** 조각 목록은 chunkPool 끝에 덧붙인다. chunkPool은 미리 충분히 reserve되어 있어야 한다 */
	Morpheme bake(const MorphemeRaw& o, const Morpheme* morphBase, const Form* formBase, const Vector<size_t>& formMap,
		Vector<const Morpheme*>& chunkPool);
}
