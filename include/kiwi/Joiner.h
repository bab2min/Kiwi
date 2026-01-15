/**
 * @file Joiner.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 형태소를 결합하여 문장을 재구성하는 Joiner 클래스 정의
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * 형태소 분석의 역과정으로, 분석된 형태소들을 다시 원래의 문장 형태로 결합합니다.
 * 한국어의 복잡한 음운 규칙과 철자 규칙을 고려하여 자연스러운 문장을 생성합니다.
 */

#pragma once
#include "Types.h"
#include "ArchUtils.h"
#include "LangModel.h"

namespace kiwi
{
	class Kiwi;
	struct Form;

	namespace cmb
	{
		class CompiledRule;
		class AutoJoiner;

		/**
		 * @brief 형태소 결합 시 공백 처리 방식을 나타내는 열거형
		 */
		enum class Space
		{
			none = 0,         /**< 공백 처리 없음 */
			no_space = 1,     /**< 공백을 삽입하지 않음 */
			insert_space = 2, /**< 공백을 삽입함 */
		};

		/**
		 * @brief 형태소를 결합하여 문장을 재구성하는 클래스
		 * 
		 * 분석된 형태소들을 한국어의 음운 규칙에 따라 결합하여 
		 * 자연스러운 문장 형태로 복원합니다.
		 * CompiledRule을 사용하여 형태소 결합 규칙을 적용합니다.
		 */
		class Joiner
		{
			friend class CompiledRule;
			friend class AutoJoiner;
			template<class LmState> friend struct Candidate;
			const CompiledRule* cr = nullptr;
			KString stack;
			std::vector<std::pair<uint32_t, uint32_t>> ranges;
			size_t activeStart = 0;
			POSTag lastTag = POSTag::unknown, anteLastTag = POSTag::unknown;

			explicit Joiner(const CompiledRule& _cr);			
			void add(U16StringView form, POSTag tag, Space space);

		public:
			~Joiner();

			Joiner(const Joiner&);
			Joiner(Joiner&&) noexcept;
			Joiner& operator=(const Joiner&);
			Joiner& operator=(Joiner&&);

			/**
			 * @brief 형태소를 결합 스택에 추가합니다.
			 * @param form 형태소의 표면형
			 * @param tag 품사 태그
			 * @param space 공백 처리 방식
			 */
			void add(const std::u16string& form, POSTag tag, Space space = Space::none);
			
			/**
			 * @brief 형태소를 결합 스택에 추가합니다.
			 * @param form 형태소의 표면형 (C 문자열)
			 * @param tag 품사 태그
			 * @param space 공백 처리 방식
			 */
			void add(const char16_t* form, POSTag tag, Space space = Space::none);

			/**
			 * @brief 결합된 결과를 UTF-16 문자열로 반환합니다.
			 * @param rangesOut 각 형태소의 문자 위치 범위를 저장할 벡터 (선택 사항)
			 * @return 결합된 UTF-16 문자열
			 */
			std::u16string getU16(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
			
			/**
			 * @brief 결합된 결과를 UTF-8 문자열로 반환합니다.
			 * @param rangesOut 각 형태소의 바이트 위치 범위를 저장할 벡터 (선택 사항)
			 * @return 결합된 UTF-8 문자열
			 */
			std::string getU8(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
		};

		/**
		 * @brief 언어 모델을 사용한 형태소 결합 후보
		 * 
		 * 여러 가능한 결합 방식 중 가장 확률이 높은 것을 선택하기 위해
		 * 언어 모델 상태와 점수를 함께 관리합니다.
		 * 
		 * @tparam LmState 언어 모델 상태 타입
		 */
		template<class LmState>
		struct Candidate
		{
			Joiner joiner;      /**< 형태소 결합기 */
			LmState lmState;    /**< 언어 모델 상태 */
			float score = 0;    /**< 현재까지의 누적 점수 */

			Candidate(const CompiledRule& _cr, const lm::ILangModel* lm)
				: joiner{ _cr }, lmState{ lm }
			{
			}
		};

		/**
		 * @brief VoidLangModel을 위한 Candidate 특수화
		 * 
		 * 언어 모델을 사용하지 않는 경우의 후보입니다.
		 * 
		 * @tparam arch 아키텍처 타입
		 */
		template<ArchType arch>
		struct Candidate<lm::VoidState<arch>>
		{
			Joiner joiner;  /**< 형태소 결합기 */

			Candidate(const CompiledRule& _cr, const lm::ILangModel* lm)
				: joiner{ _cr }
			{
			}
		};

		/**
		 * @brief 타입이 지워진 벡터 컨테이너
		 * 
		 * 템플릿 타입 정보를 런타임에 관리하기 위한 타입 소거(type erasure) 벡터입니다.
		 * 다양한 타입의 Candidate를 동일한 방식으로 저장하고 관리할 수 있게 합니다.
		 */
		class ErasedVector
		{
			using FnDestruct = void(*)(ErasedVector*);
			using FnCopyConstruct = void(*)(ErasedVector*, const ErasedVector&);

			template<class T>
			static void destructImpl(ErasedVector* self)
			{
				auto* target = reinterpret_cast<Vector<T>*>(&self->vec);
				std::destroy_at(target);
			}

			template<class T>
			static void copyConstructImpl(ErasedVector* self, const ErasedVector& other)
			{
				auto* target = reinterpret_cast<Vector<T>*>(&self->vec);
				new (target) Vector<T>{ *reinterpret_cast<const Vector<T>*>(&other.vec) };
			}

			union
			{
				Vector<char> vec;
			};
			FnDestruct destruct = nullptr;
			FnCopyConstruct copyConstruct = nullptr;
		public:

			template<class T>
			ErasedVector(Vector<T>&& v)
			{
				auto* target = reinterpret_cast<Vector<T>*>(&vec);
				new (target) Vector<T>{ move(v) };
				destruct = &destructImpl<T>;
				copyConstruct = &copyConstructImpl<T>;
			}

			~ErasedVector()
			{
				if (destruct)
				{
					(*destruct)(this);
					destruct = nullptr;
					copyConstruct = nullptr;
				}
			}

			ErasedVector(const ErasedVector& other)
				: destruct{ other.destruct }, copyConstruct{ other.copyConstruct }
			{
				if (!destruct) return;
				(*copyConstruct)(this, other);
			}

			ErasedVector(ErasedVector&& other)
			{
				std::swap(vec, other.vec);
				std::swap(destruct, other.destruct);
				std::swap(copyConstruct, other.copyConstruct);
			}

			ErasedVector& operator=(const ErasedVector& other)
			{
				this->~ErasedVector();
				new (this) ErasedVector{ other };
				return *this;
			}

			ErasedVector& operator=(ErasedVector&& other)
			{
				std::swap(vec, other.vec);
				std::swap(destruct, other.destruct);
				std::swap(copyConstruct, other.copyConstruct);
				return *this;
			}

			template<class T>
			Vector<T>& get()
			{
				return *reinterpret_cast<Vector<T>*>(&vec);
			}

			template<class T>
			const Vector<T>& get() const
			{
				return *reinterpret_cast<const Vector<T>*>(&vec);
			}
		};

		/**
		 * @brief 자동으로 형태소를 결합하는 클래스
		 * 
		 * 언어 모델을 활용하여 여러 가능한 결합 방식 중 
		 * 가장 확률이 높은 결합을 자동으로 선택합니다.
		 * 형태소 추가 시 언어 모델 점수를 고려하여 최적의 후보를 유지합니다.
		 */
		class AutoJoiner
		{
			friend class kiwi::Kiwi;

			template<class LmState>
			explicit AutoJoiner(const Kiwi& kiwi, Candidate<LmState>&& state);

			template<class Func>
			void foreachMorpheme(const Form* formHead, Func&& func) const;

			template<class LmState>
			void addImpl(size_t morphemeId, Space space, Vector<Candidate<LmState>>& candidates);

			template<class LmState>
			void addImpl2(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<LmState>>& candidates);

			template<ArchType arch>
			void addWithoutSearchImpl(size_t morphemeId, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates);

			template<ArchType arch>
			void addWithoutSearchImpl2(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates);

			template<class LmState>
			struct Dispatcher;

			using FnAdd = void(*)(AutoJoiner*, size_t, Space, Vector<Candidate<lm::VoidState<ArchType::none>>>&);
			using FnAdd2 = void(*)(AutoJoiner*, U16StringView, POSTag, bool, Space, Vector<Candidate<lm::VoidState<ArchType::none>>>&);

			const Kiwi* kiwi = nullptr;
			FnAdd dfAdd = nullptr;
			FnAdd2 dfAdd2 = nullptr;
			ErasedVector candBuf;
		public:

			~AutoJoiner();
			AutoJoiner(const AutoJoiner&);
			AutoJoiner(AutoJoiner&&);
			AutoJoiner& operator=(const AutoJoiner&);
			AutoJoiner& operator=(AutoJoiner&&);

			/**
			 * @brief 형태소 ID로 형태소를 추가합니다.
			 * @param morphemeId 형태소 인덱스
			 * @param space 공백 처리 방식
			 */
			void add(size_t morphemeId, Space space = Space::none);
			
			/**
			 * @brief 형태소를 추가합니다 (StringView).
			 * @param form 형태소의 표면형
			 * @param tag 품사 태그
			 * @param space 공백 처리 방식
			 */
			void add(U16StringView form, POSTag tag, Space space = Space::none);
			
			/**
			 * @brief 형태소를 추가합니다 (u16string).
			 * @param form 형태소의 표면형
			 * @param tag 품사 태그
			 * @param inferRegularity 규칙 활용 자동 추론 여부
			 * @param space 공백 처리 방식
			 */
			void add(const std::u16string& form, POSTag tag, bool inferRegularity = true, Space space = Space::none);
			
			/**
			 * @brief 형태소를 추가합니다 (C 문자열).
			 * @param form 형태소의 표면형
			 * @param tag 품사 태그
			 * @param inferRegularity 규칙 활용 자동 추론 여부
			 * @param space 공백 처리 방식
			 */
			void add(const char16_t* form, POSTag tag, bool inferRegularity = true, Space space = Space::none);

			/**
			 * @brief 결합된 결과를 UTF-16 문자열로 반환합니다.
			 * @param rangesOut 각 형태소의 문자 위치 범위를 저장할 벡터 (선택 사항)
			 * @return 결합된 UTF-16 문자열
			 */
			std::u16string getU16(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
			
			/**
			 * @brief 결합된 결과를 UTF-8 문자열로 반환합니다.
			 * @param rangesOut 각 형태소의 바이트 위치 범위를 저장할 벡터 (선택 사항)
			 * @return 결합된 UTF-8 문자열
			 */
			std::string getU8(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
		};
	}
}
