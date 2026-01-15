/**
 * @file SkipBigramModel.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Skip-bigram 언어 모델 구현
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * Skip-bigram은 인접하지 않은 두 단어 사이의 관계를 학습하는 언어 모델입니다.
 * 일반적인 bigram이 연속된 두 단어만을 고려하는 것과 달리,
 * 중간에 다른 단어가 있어도 두 단어 사이의 관계를 포착할 수 있습니다.
 */

#pragma once

#include "Knlm.h"

namespace kiwi
{
	namespace lm
	{
		/**
		 * @brief Skip-bigram 모델의 헤더 정보
		 */
		struct SkipBigramModelHeader
		{
			uint64_t vocabSize;    /**< 어휘 크기 */
			uint8_t keySize;       /**< 키의 크기 */
			uint8_t windowSize;    /**< 윈도우 크기 (skip 거리) */
			uint8_t compressed;    /**< 압축 여부 */
			uint8_t quantize;      /**< 양자화 비트 수 */
			uint8_t _rsv[4];       /**< 예약 필드 */
		};

		/**
		 * @brief Skip-bigram 언어 모델의 기본 클래스
		 * 
		 * 중간 단어를 건너뛰며 단어 간 관계를 학습하는 언어 모델입니다.
		 * 긴 거리 의존성을 포착하여 더 정확한 형태소 분석을 가능하게 합니다.
		 */
		class SkipBigramModelBase : public ILangModel
		{
		protected:
			utils::MemoryObject base;

			SkipBigramModelBase(utils::MemoryObject&& mem) : base{ std::move(mem) }
			{
			}
		public:
			virtual ~SkipBigramModelBase() {}
			size_t vocabSize() const override { return getHeader().vocabSize; }
			ModelType getType() const override { return ModelType::sbg; }

			/**
			 * @brief 모델 헤더 정보를 반환합니다.
			 * @return SkipBigramModelHeader에 대한 const 참조
			 */
			const SkipBigramModelHeader& getHeader() const { return *reinterpret_cast<const SkipBigramModelHeader*>(base.get()); }

			/**
			 * @brief 메모리로부터 Skip-bigram 모델을 생성합니다.
			 * @param knlmMem Kneser-Ney 언어 모델 메모리
			 * @param sbgMem Skip-bigram 모델 메모리
			 * @param archType 아키텍처 타입 (최적화를 위한)
			 * @return 생성된 Skip-bigram 모델의 unique_ptr
			 */
			static std::unique_ptr<SkipBigramModelBase> create(utils::MemoryObject&& knlmMem, utils::MemoryObject&& sbgMem, ArchType archType = ArchType::none);
		};
	}
}
