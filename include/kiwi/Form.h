/**
 * @file Form.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 형태 및 형태소에 관한 정보를 담는 구조체들이 선언된 헤더
 * @version 0.11.1
 * @date 2022-04-03
 * 
 * 
 */

#pragma once

#include <kiwi/Types.h>
#include <kiwi/FixedVector.hpp>

namespace kiwi
{
	struct Morpheme;

	/**
	 * @brief 형태소에 관한 모든 정보를 담는 구조체의 템플릿
	 * 
	 * @note 변경가능한 상태로 인덱스와 관련된 값이나 std::vector 등의 길이를 변경할 수 있음.
	 * `kiwi::KiwiBuilder`에서 사용한다.
	 * `baked = true`는 변경 불가능한 상태로 인덱스는 모두 포인터로, std::vector는 FixedVector로 변경되어 수정이 불가능한 대신
	 * 각 값에 효율적으로 빠르게 접근 가능하다. 이 상태는 `kiwi::Morpheme`이라는 타입의 부모클래스로 쓰이며, 
	 * `kiwi::Kiwi` 내 실제 형태소 분석 단계에 쓰인다.
	 */
	struct MorphemeRaw
	{
		uint32_t kform = 0; /**< 형태에 대한 포인터 */
		POSTag tag = POSTag::unknown; /**< 품사 태그 */
		CondVowel vowel = CondVowel::none; /**< 선행형태소의 자/모음 조건 */
		CondPolarity polar = CondPolarity::none; /**< 선행형태소의 모음조화 조건 */

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
		 * @note pair.first는 시작 지점, pair.second는 길이를 나타낸다. chunkPositions.size()는 항상 chunks.size()와 같다.
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

		MorphemeRaw();
		~MorphemeRaw();
		MorphemeRaw(const MorphemeRaw&);
		MorphemeRaw(MorphemeRaw&&);
		MorphemeRaw& operator=(const MorphemeRaw&);
		MorphemeRaw& operator=(MorphemeRaw&&);

		MorphemeRaw(
			POSTag _tag,
			CondVowel _vowel = CondVowel::none,
			CondPolarity _polar = CondPolarity::none,
			uint8_t _combineSocket = 0
		);

		void serializerRead(std::istream& istr);
		void serializerWrite(std::ostream& ostr) const;
	};

	/**
	 * @brief 형태소에 관한 모든 정보를 담는 구조체의 템플릿
	 * 
	 * @note 변경 불가능한 상태로 인덱스는 모두 포인터로, std::vector는 FixedVector로 변경되어 수정이 불가능한 대신
	 * 각 값에 효율적으로 빠르게 접근 가능하다. `kiwi::Kiwi` 내 실제 형태소 분석 단계에 쓰인다.
	 */
	struct Morpheme
	{
		const KString* kform = nullptr;
		POSTag tag = POSTag::unknown;
		CondVowel vowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;
		uint8_t combineSocket = 0;
		FixedPairVector<const Morpheme*, std::pair<uint8_t, uint8_t>> chunks;
		int32_t combined = 0;
		float userScore = 0;
		uint32_t lmMorphemeId = 0;

		Morpheme();
		~Morpheme();
		Morpheme(const Morpheme&);
		Morpheme(Morpheme&&);
		Morpheme& operator=(const Morpheme&);
		Morpheme& operator=(Morpheme&&);

		std::ostream& print(std::ostream& os) const;

		/** 형태소의 형태를 반환한다. */
		const KString& getForm() const { return *kform; }

		/** 분할된 형태소의 경우 원형 형태소를 반환한다. 그 외에는 자기 자신을 반환한다. */
		const Morpheme* getCombined() const { return this + combined; }
	};

	/**
	 * @brief 형태에 관한 모든 정보를 담는 구조체의 템플릿
	 * 
	 * @note 변경가능한 상태로 인덱스와 관련된 값이나 std::vector 등의 길이를 변경할 수 있음. `kiwi::KiwiBuilder`에서 사용한다.
	 * `baked = true`는 변경 불가능한 상태로 인덱스는 모두 포인터로, std::vector는 FixedVector로 변경되어 수정이 불가능한 대신
	 * 각 값에 효율적으로 빠르게 접근 가능하다. 이 상태는 `kiwi::Form`이라는 타입의 부모클래스로 쓰이며, 
	 * `kiwi::Kiwi` 내 실제 형태소 분석 단계에 쓰인다.
	 */
	struct FormRaw
	{
		KString form; /**< 형태 */
		Vector<uint32_t> candidate;
		/**< 이 형태에 해당하는 형태소들의 목록 */

		FormRaw();
		~FormRaw();
		FormRaw(const FormRaw&);
		FormRaw(FormRaw&&);
		FormRaw& operator=(const FormRaw&);
		FormRaw& operator=(FormRaw&&);
		
		FormRaw(const KString& _form);
		bool operator<(const FormRaw& o) const;

		void serializerRead(std::istream& istr);
		void serializerWrite(std::ostream& ostr) const;
	};

	/**
	 * @brief 형태에 관한 모든 정보를 담는 구조체의 템플릿
	 * 
	 * @note 변경 불가능한 상태로 인덱스는 모두 포인터로, std::vector는 FixedVector로 변경되어 수정이 불가능한 대신
	 * 각 값에 효율적으로 빠르게 접근 가능하다. `kiwi::Kiwi` 내 실제 형태소 분석 단계에 쓰인다.
	 */
	struct Form
	{
		KString form;
		CondVowel vowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;
		FixedVector<const Morpheme*> candidate;

		Form();
		~Form();
		Form(const Form&);
		Form(Form&&);
		Form& operator=(const Form&);
		Form& operator=(Form&&);
	};

	/**
	 * @brief 변경가능한 형태 정보를 bake하여 최적화한다.
	 * 
	 * @param o 변경 가능한 형태 정보
	 * @param morphBase 형태소 배열의 시작 위치
	 * @return 최적화된 형태 정보
	 */
	Form bake(const FormRaw& o, const Morpheme* morphBase, const Vector<uint32_t>& additionalCands = {});

	/**
	 * @brief 변경 가능한 형태소 정보를 bake하여 최적화한다.
	 * 
	 * @param o 변경 가능한 형태소 정보
	 * @param morphBase 형태소 배열의 시작 위치
	 * @param formBase 형태 배열의 시작 위치
	 * @return 최적화된 형태소 정보
	 */
	Morpheme bake(const MorphemeRaw& o, const Morpheme* morphBase, const Form* formBase);
}
