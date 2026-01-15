/**
 * @file Knlm.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kneser-Ney 언어 모델 구현
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * Kneser-Ney 스무딩을 사용한 N-gram 언어 모델을 구현합니다.
 * 형태소 분석 시 가장 가능성 높은 형태소 시퀀스를 선택하는 데 사용됩니다.
 * 압축과 양자화를 지원하여 메모리 효율적인 모델을 제공합니다.
 */

#pragma once

#include "LangModel.h"

namespace kiwi
{
	namespace lm
	{
		/**
		 * @brief Kneser-Ney 언어 모델의 헤더 정보
		 * 
		 * 모델의 메타데이터와 각 데이터 섹션의 오프셋을 저장합니다.
		 */
		struct KnLangModelHeader
		{
			uint64_t num_nodes;       /**< 노드의 총 개수 */
			uint64_t node_offset;     /**< 노드 데이터의 시작 오프셋 */
			uint64_t key_offset;      /**< 키 데이터의 시작 오프셋 */
			uint64_t ll_offset;       /**< 로그 우도(log-likelihood) 데이터의 시작 오프셋 */
			uint64_t gamma_offset;    /**< 감마(백오프 가중치) 데이터의 시작 오프셋 */
			uint64_t qtable_offset;   /**< 양자화 테이블의 시작 오프셋 */
			uint64_t htx_offset;      /**< 히스토리 변환 데이터의 시작 오프셋 */
			uint64_t unk_id;          /**< 미등록어(unknown) ID */
			uint64_t bos_id;          /**< 문장 시작(beginning of sentence) ID */
			uint64_t eos_id;          /**< 문장 종료(end of sentence) ID */
			uint64_t vocab_size;      /**< 어휘 크기 */
			uint8_t order;            /**< N-gram 차수 */
			uint8_t key_size;         /**< 키의 크기 (바이트) */
			uint8_t diff_size;        /**< diff 값의 크기 (바이트) */
			uint8_t quantized;        /**< 양자화 여부 */
			uint32_t extra_buf_size;  /**< 추가 버퍼 크기 */
		};

		/**
		 * @brief Kneser-Ney 언어 모델의 노드 구조
		 * 
		 * 각 N-gram을 표현하는 트리 노드입니다.
		 * 
		 * @tparam KeyType 키의 타입 (어휘 인덱스)
		 * @tparam DiffType diff 값의 타입
		 */
		template<class KeyType, class DiffType = int32_t>
		struct KnLangModelNode
		{
			KeyType num_nexts = 0;     /**< 다음 노드의 개수 */
			DiffType lower = 0;        /**< 하위 노드로의 오프셋 */
			uint32_t next_offset = 0;  /**< 다음 노드들의 시작 오프셋 */
			float ll = 0;              /**< 로그 우도 */
			float gamma = 0;           /**< 백오프 가중치 */
		};

		/**
		 * @brief Kneser-Ney 언어 모델의 기본 클래스
		 * 
		 * 모든 Kneser-Ney 언어 모델 구현의 베이스 클래스입니다.
		 * 메모리 매핑된 모델 데이터를 관리하고 N-gram 확률 계산을 제공합니다.
		 */
		class KnLangModelBase : public ILangModel
		{
		protected:
			utils::MemoryObject base;

			KnLangModelBase(utils::MemoryObject&& mem) : base{ std::move(mem) }
			{
			}

			//virtual float getLL(ptrdiff_t node_idx, size_t next) const = 0;
			virtual float _progress(ptrdiff_t& node_idx, size_t next) const = 0;
			virtual std::vector<float> allNextLL(ptrdiff_t node_idx) const = 0;
			virtual std::vector<float> allNextLL(ptrdiff_t node_idx, std::vector<ptrdiff_t>& next_node_idx) const = 0;
			virtual void nextTopN(ptrdiff_t node_idx, size_t top_n, uint32_t* idx_out, float* ll_out) const = 0;

		public:

			virtual ~KnLangModelBase() {}
			size_t vocabSize() const override { return getHeader().vocab_size; }
			size_t getMemorySize() const override { return base.size(); }

			/**
			 * @brief 모델 헤더 정보를 반환합니다.
			 * @return KnLangModelHeader에 대한 const 참조
			 */
			const KnLangModelHeader& getHeader() const { return *reinterpret_cast<const KnLangModelHeader*>(base.get()); }

			/**
			 * @brief 하위 노드의 인덱스를 반환합니다.
			 * @param node_idx 현재 노드 인덱스
			 * @return 하위 노드 인덱스
			 */
			virtual ptrdiff_t getLowerNode(ptrdiff_t node_idx) const = 0;

			virtual size_t nonLeafNodeSize() const = 0;
			
			/**
			 * @brief 추가 버퍼를 반환합니다.
			 * @return 추가 버퍼 포인터
			 */
			virtual const void* getExtraBuf() const = 0;

			/**
			 * @brief 메모리로부터 Kneser-Ney 언어 모델을 생성합니다.
			 * @param mem 모델 데이터가 담긴 메모리 객체
			 * @param archType 아키텍처 타입 (최적화를 위한)
			 * @param transposed 전치 여부
			 * @return 생성된 언어 모델의 unique_ptr
			 */
			static std::unique_ptr<KnLangModelBase> create(utils::MemoryObject&& mem, ArchType archType = ArchType::none, bool transposed = false);

			template<class VocabTy, class Trie, class HistoryTx = std::vector<VocabTy>>
			static utils::MemoryOwner build(Trie&& ngram_cf,
				size_t order, const std::vector<size_t>& min_cf_by_order,
				size_t unk_id, size_t bos_id, size_t eos_id,
				float unigram_alpha, size_t quantize, bool compress,
				const std::vector<std::pair<VocabTy, VocabTy>>* bigram_list = nullptr,
				const HistoryTx* history_transformer = nullptr,
				const void* extra_buf = nullptr,
				size_t extra_buf_size = 0
			);

			/**
			 * @brief 메모리 객체를 반환합니다.
			 * @return 모델 데이터가 담긴 메모리 객체에 대한 const 참조
			 */
			const utils::MemoryObject& getMemory() const { return base; }

			/**
			 * @brief 다음 토큰으로 상태를 진행하고 로그 확률을 반환합니다.
			 * @param node_idx 현재 노드 인덱스 (참조로 업데이트됨)
			 * @param next 다음 토큰
			 * @return 로그 확률
			 */
			template<class Ty>
			float progress(ptrdiff_t& node_idx, Ty next) const
			{
				return _progress(node_idx, next);
			}

			/**
			 * @brief 토큰 시퀀스를 평가하여 로그 확률을 계산합니다.
			 * @param in_first 입력 시퀀스의 시작 반복자
			 * @param in_last 입력 시퀀스의 끝 반복자
			 * @param out_first 출력 확률을 저장할 시작 반복자
			 */
			template<class InTy, class OutTy>
			void evaluate(InTy in_first, InTy in_last, OutTy out_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					*out_first = _progress(node_idx, *in_first);
					++out_first;
				}
			}

			template<class InTy, class OutProbTy, class OutNodeTy>
			void evaluate(InTy in_first, InTy in_last, OutProbTy prob_first, OutNodeTy node_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					*node_first = node_idx;
					*prob_first = _progress(node_idx, *in_first);
					++prob_first;
					++node_first;
				}
			}

			/**
			 * @brief 토큰 시퀀스의 총 로그 확률을 계산합니다.
			 * @param in_first 입력 시퀀스의 시작 반복자
			 * @param in_last 입력 시퀀스의 끝 반복자
			 * @param min_score 최소 점수 임계값
			 * @return 총 로그 확률
			 */
			template<class InTy>
			float sum(InTy in_first, InTy in_last, float min_score = -100) const
			{
				float ret = 0;
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					ret += std::max(_progress(node_idx, *in_first), min_score);
				}
				return ret;
			}

			/**
			 * @brief 주어진 히스토리에 대한 다음 토큰들의 로그 확률을 반환합니다.
			 * @param in_first 히스토리 시퀀스의 시작 반복자
			 * @param in_last 히스토리 시퀀스의 끝 반복자
			 * @return 모든 다음 토큰의 로그 확률 벡터
			 */
			template<class InTy>
			std::vector<float> getNextLL(InTy in_first, InTy in_last) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					_progress(node_idx, *in_first);
				}
				return allNextLL(node_idx);
			}

			template<class InTy, class OutTy>
			void predict(InTy in_first, InTy in_last, OutTy out_first) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					_progress(node_idx, *in_first);
					*out_first = allNextLL(node_idx);
					++out_first;
				}
			}

			template<class InTy>
			void predictTopN(InTy in_first, InTy in_last, size_t top_n, uint32_t* idx_out, float* ll_out) const
			{
				ptrdiff_t node_idx = 0;
				for (; in_first != in_last; ++in_first)
				{
					_progress(node_idx, *in_first);
					nextTopN(node_idx, top_n, idx_out, ll_out);
					idx_out += top_n;
					ll_out += top_n;
				}
			}

			template<class PfTy, class SfTy, class OutTy>
			void fillIn(PfTy prefix_first, PfTy prefix_last, SfTy suffix_first, SfTy suffix_last, OutTy out_first, bool reduce = true) const
			{
				ptrdiff_t node_idx = 0;
				for (; prefix_first != prefix_last; ++prefix_first)
				{
					_progress(node_idx, *prefix_first);
				}

				std::vector<ptrdiff_t> next_node_idcs;
				*out_first = allNextLL(node_idx, next_node_idcs);

				if (reduce)
				{
					for (size_t i = 0; i < next_node_idcs.size(); ++i)
					{
						auto node_idx = next_node_idcs[i];
						for (auto it = suffix_first; it != suffix_last; ++it)
						{
							(*out_first)[i] += progress(node_idx, *it);
						}
					}
				}
				else
				{
					++out_first;
					for (size_t i = 0; i < next_node_idcs.size(); ++i)
					{
						auto node_idx = next_node_idcs[i];
						auto out_next = out_first;
						for (auto it = suffix_first; it != suffix_last; ++it)
						{
							(*out_next)[i] = progress(node_idx, *it);
							++out_next;
						}
					}
				}
			}
		};
	}
}
