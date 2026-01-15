/**
 * @file CoNgramModel.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 문맥 기반 N-gram 언어 모델 (Contextual N-gram Model) 구현
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * 단어 임베딩과 문맥 정보를 활용한 신경망 기반 언어 모델입니다.
 * 전통적인 N-gram 모델보다 더 풍부한 의미 정보를 포착할 수 있습니다.
 */

#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>

#include "ArchUtils.h"
#include "Mmap.h"
#include "LangModel.h"

namespace kiwi
{
	namespace lm
	{
		/**
		 * @brief 문맥 N-gram 모델의 헤더 정보
		 */
		struct CoNgramModelHeader
		{
			uint64_t vocabSize;     /**< 어휘 크기 */
			uint64_t contextSize;   /**< 문맥 크기 */
			uint16_t dim;           /**< 임베딩 차원 */
			uint8_t contextType;    /**< 문맥 타입 */
			uint8_t outputType;     /**< 출력 타입 */
			uint8_t keySize;        /**< 키 크기 */
			uint8_t windowSize;     /**< 윈도우 크기 */
			uint8_t qbit;           /**< 양자화 비트 수 */
			uint8_t qgroup;         /**< 양자화 그룹 크기 */
			uint64_t numNodes;      /**< 노드 개수 */
			uint64_t nodeOffset;    /**< 노드 데이터 오프셋 */
			uint64_t keyOffset;     /**< 키 데이터 오프셋 */
			uint64_t valueOffset;   /**< 값 데이터 오프셋 */
			uint64_t embOffset;     /**< 임베딩 데이터 오프셋 */
		};

		/**
		 * @brief 문맥 N-gram 모델의 노드 구조
		 * 
		 * @tparam KeyType 키 타입
		 * @tparam ValueType 값 타입
		 * @tparam DiffType diff 타입
		 */
		template<class KeyType, class ValueType, class DiffType = int32_t>
		struct Node
		{
			KeyType numNexts = 0;     /**< 다음 노드의 개수 */
			ValueType value = 0;      /**< 노드 값 */
			DiffType lower = 0;       /**< 하위 노드로의 오프셋 */
			uint32_t nextOffset = 0;  /**< 다음 노드들의 시작 오프셋 */
		};

		/**
		 * @brief 문맥 기반 N-gram 언어 모델의 기본 클래스
		 * 
		 * 신경망 임베딩을 활용하여 문맥 정보를 효과적으로 활용하는 언어 모델입니다.
		 * 단어의 의미적 유사도와 문맥 유사도를 계산할 수 있습니다.
		 */
		class CoNgramModelBase : public ILangModel
		{
		protected:
			const size_t memorySize = 0;
			CoNgramModelHeader header;
			mutable std::vector<std::vector<uint32_t>> contextWordMapCache;

			CoNgramModelBase(const utils::MemoryObject& mem) : memorySize{ mem.size() }, header{ *reinterpret_cast<const CoNgramModelHeader*>(mem.get()) }
			{
			}
		public:
			virtual ~CoNgramModelBase() {}
			size_t vocabSize() const override { return header.vocabSize; }
			size_t getMemorySize() const override { return memorySize; }

			/**
			 * @brief 모델 헤더 정보를 반환합니다.
			 * @return CoNgramModelHeader에 대한 const 참조
			 */
			const CoNgramModelHeader& getHeader() const { return header; }

			/**
			 * @brief 주어진 단어와 가장 유사한 단어들을 찾습니다.
			 * @param vocabId 단어 ID
			 * @param topN 상위 N개
			 * @param output 결과를 저장할 배열 (단어 ID, 유사도)
			 * @return 찾은 단어의 개수
			 */
			virtual size_t mostSimilarWords(uint32_t vocabId, size_t topN, std::pair<uint32_t, float>* output) const = 0;
			
			/**
			 * @brief 두 단어 간의 유사도를 계산합니다.
			 * @param vocabId1 첫 번째 단어 ID
			 * @param vocabId2 두 번째 단어 ID
			 * @return 유사도 점수
			 */
			virtual float wordSimilarity(uint32_t vocabId1, uint32_t vocabId2) const = 0;

			/**
			 * @brief 주어진 문맥과 가장 유사한 문맥들을 찾습니다.
			 * @param contextId 문맥 ID
			 * @param topN 상위 N개
			 * @param output 결과를 저장할 배열
			 * @return 찾은 문맥의 개수
			 */
			virtual size_t mostSimilarContexts(uint32_t contextId, size_t topN, std::pair<uint32_t, float>* output) const = 0;
			
			/**
			 * @brief 두 문맥 간의 유사도를 계산합니다.
			 * @param contextId1 첫 번째 문맥 ID
			 * @param contextId2 두 번째 문맥 ID
			 * @return 유사도 점수
			 */
			virtual float contextSimilarity(uint32_t contextId1, uint32_t contextId2) const = 0;

			/**
			 * @brief 주어진 문맥에서 예측되는 단어들을 반환합니다.
			 * @param contextId 문맥 ID
			 * @param topN 상위 N개
			 * @param output 결과를 저장할 배열
			 * @return 예측된 단어의 개수
			 */
			virtual size_t predictWordsFromContext(uint32_t contextId, size_t topN, std::pair<uint32_t, float>* output) const = 0;
			
			/**
			 * @brief 문맥 차이를 고려하여 단어를 예측합니다.
			 * @param contextId 문맥 ID
			 * @param bgContextId 배경 문맥 ID
			 * @param weight 가중치
			 * @param topN 상위 N개
			 * @param output 결과를 저장할 배열
			 * @return 예측된 단어의 개수
			 */
			virtual size_t predictWordsFromContextDiff(uint32_t contextId, uint32_t bgContextId, float weight, size_t topN, std::pair<uint32_t, float>* output) const = 0;

			/**
			 * @brief 단어 ID 시퀀스를 문맥 ID로 변환합니다.
			 * @param vocabIds 단어 ID 배열
			 * @param size 배열 크기
			 * @return 문맥 ID
			 */
			virtual uint32_t toContextId(const uint32_t* vocabIds, size_t size) const = 0;
			
			/**
			 * @brief 문맥과 단어의 매핑을 반환합니다.
			 * @return 문맥-단어 매핑 벡터
			 */
			virtual std::vector<std::vector<uint32_t>> getContextWordMap() const = 0;

			/**
			 * @brief 캐시된 문맥-단어 매핑을 반환합니다.
			 * @return 캐시된 문맥-단어 매핑에 대한 const 참조
			 */
			const std::vector<std::vector<uint32_t>>& getContextWordMapCached() const
			{
				if (contextWordMapCache.empty())
				{
					contextWordMapCache = getContextWordMap();
				}
				return contextWordMapCache;
			}

			/**
			 * @brief 문맥 정의와 임베딩으로부터 모델을 빌드합니다.
			 * @param contextDefinition 문맥 정의 파일 경로
			 * @param embedding 임베딩 파일 경로
			 * @param maxContextLength 최대 문맥 길이
			 * @param useVLE VLE(Variable Length Encoding) 사용 여부
			 * @param reorderContextIdx 문맥 인덱스 재정렬 여부
			 * @param selectedEmbIdx 선택된 임베딩 인덱스
			 * @return 빌드된 모델의 메모리 객체
			 */
			static utils::MemoryObject build(const std::string& contextDefinition, const std::string& embedding, 
				size_t maxContextLength = -1, 
				bool useVLE = true, 
				bool reorderContextIdx = true,
				const std::vector<size_t>* selectedEmbIdx = nullptr);

			static std::unique_ptr<CoNgramModelBase> create(utils::MemoryObject&& mem, 
				ArchType archType = ArchType::none, 
				bool useDistantTokens = false, 
				bool quantized = true);
		};
	}
}
