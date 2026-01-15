/**
 * @file LangModel.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 언어 모델 인터페이스 및 기본 구현을 정의하는 헤더 파일
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * 이 파일은 형태소 분석에서 사용되는 언어 모델의 인터페이스를 정의합니다.
 * 언어 모델은 형태소 시퀀스의 확률을 계산하여 가장 가능성 높은 분석 결과를 선택하는 데 사용됩니다.
 */

#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>

#include "Utils.h"
#include "Mmap.h"
#include "ArchUtils.h"
#include "Types.h"

namespace kiwi
{
	namespace lm
	{
		/**
		 * @brief 언어 모델의 기본 인터페이스
		 * 
		 * 모든 언어 모델 구현체가 상속해야 하는 추상 인터페이스입니다.
		 * 형태소 분석 과정에서 각 형태소 시퀀스의 확률을 계산하는데 사용됩니다.
		 */
		class ILangModel
		{
		public:
			virtual ~ILangModel() = default;
			
			/**
			 * @brief 언어 모델의 타입을 반환합니다.
			 * @return 언어 모델 타입 (none, knlm, skipbigram 등)
			 */
			virtual ModelType getType() const = 0;
			
			/**
			 * @brief 언어 모델의 어휘 크기를 반환합니다.
			 * @return 어휘(vocabulary)에 포함된 형태소의 개수
			 */
			virtual size_t vocabSize() const = 0;
			
			/**
			 * @brief 언어 모델이 사용하는 메모리 크기를 반환합니다.
			 * @return 메모리 사용량 (바이트 단위)
			 */
			virtual size_t getMemorySize() const = 0;

			/**
			 * @brief 최적 경로 탐색 함수 포인터를 반환합니다.
			 * @return 최적 경로 탐색에 사용되는 함수 포인터
			 */
			virtual void* getFindBestPathFn() const = 0;
			
			/**
			 * @brief 새로운 Joiner 생성 함수 포인터를 반환합니다.
			 * @return Joiner 생성에 사용되는 함수 포인터
			 */
			virtual void* getNewJoinerFn() const = 0;
		};

		/**
		 * @brief 언어 모델 상태의 베이스 템플릿
		 * 
		 * CRTP(Curiously Recurring Template Pattern)를 사용하여
		 * 파생 클래스의 구현을 정적으로 디스패치합니다.
		 * 
		 * @tparam DerivedLM 파생된 언어 모델 클래스
		 */
		template<class DerivedLM>
		struct LmStateBase
		{
			/**
			 * @brief 다음 토큰에 대한 확률을 계산하고 상태를 업데이트합니다.
			 * @param langMdl 언어 모델 포인터
			 * @param nextToken 다음 토큰
			 * @return 다음 토큰의 로그 확률
			 */
			float next(const ILangModel* langMdl, typename DerivedLM::VocabType nextToken)
			{
				using LmStateType = typename DerivedLM::LmStateType;
				return static_cast<LmStateType*>(this)->nextImpl(static_cast<const DerivedLM*>(langMdl), nextToken);
			}
		};

		template<ArchType arch>
		class VoidLangModel;

		/**
		 * @brief VoidLangModel의 상태 클래스
		 * 
		 * 언어 모델을 사용하지 않을 때 사용되는 더미 상태입니다.
		 * 항상 0의 확률을 반환합니다.
		 * 
		 * @tparam arch 아키텍처 타입
		 */
		template<ArchType arch>
		struct VoidState : public LmStateBase<VoidLangModel<arch>>
		{
			bool operator==(const VoidState& other) const
			{
				return true;
			}

			float nextImpl(const VoidLangModel<arch>* langMdl, uint32_t nextToken)
			{
				return 0;
			}
		};

		/**
		 * @brief 언어 모델을 사용하지 않는 더미 언어 모델
		 * 
		 * 언어 모델 없이 형태소 분석을 수행할 때 사용됩니다.
		 * 모든 확률 계산에서 0을 반환하여 언어 모델의 영향을 받지 않습니다.
		 * 
		 * @tparam arch 아키텍처 타입
		 */
		template<ArchType arch>
		class VoidLangModel : public ILangModel
		{
		public:
			using VocabType = uint32_t;
			using LmStateType = VoidState<arch>;

			ModelType getType() const override { return ModelType::none; }
			size_t vocabSize() const override { return 0; }
			void* getFindBestPathFn() const override { return nullptr; }
			void* getNewJoinerFn() const override { return nullptr; }
		};
	}

	template<ArchType arch>
	struct Hash<lm::VoidState<arch>>
	{
		size_t operator()(const lm::VoidState<arch>& state) const
		{
			return 0;
		}
	};
}
