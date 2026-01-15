/**
 * @file TagUtils.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 품사 태그 관련 유틸리티 함수 및 클래스
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * 품사 태그의 분류, 검사, 점수 계산 등을 위한 유틸리티를 제공합니다.
 */

#pragma once

#include <algorithm>
#include <kiwi/Types.h>

namespace kiwi
{
	/**
	 * @brief 품사 태그 시퀀스의 점수를 계산하는 클래스
	 * 
	 * 형태소 분석 결과의 자연스러움을 평가하기 위해
	 * 품사 태그 시퀀스에 점수를 부여합니다.
	 * 특히 어절 경계에서의 품사 조합을 평가합니다.
	 */
	class TagSequenceScorer
	{
		float leftBoundaryScores[2][(size_t)POSTag::max] = { { 0, }, };
	public:
		float weight; /**< 점수 가중치 */

		/**
		 * @brief TagSequenceScorer 생성자
		 * @param _weight 점수 가중치 (기본값: 5)
		 */
		TagSequenceScorer(float _weight = 5);

		/**
		 * @brief 왼쪽 경계에서의 품사 점수를 계산합니다.
		 * @param hasLeftBoundary 왼쪽에 어절 경계가 있는지 여부
		 * @param right 오른쪽 품사 태그
		 * @return 계산된 점수
		 */
		float evalLeftBoundary(bool hasLeftBoundary, POSTag right) const
		{
			return leftBoundaryScores[hasLeftBoundary ? 1 : 0][(size_t)clearIrregular(right)] * weight;
		}
	};

	/**
	 * @brief 품사가 체언류인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 체언류(명사, 대명사, 수사)이면 true
	 */
	bool isNounClass(POSTag tag);
	
	/**
	 * @brief 품사가 용언류인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 용언류(동사, 형용사)이면 true
	 */
	bool isVerbClass(POSTag tag);
	
	/**
	 * @brief 품사가 어미류인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 어미류이면 true
	 */
	inline bool isEClass(POSTag tag)
	{
		return POSTag::ep <= tag && tag <= POSTag::etm;
	}
	
	/**
	 * @brief 품사가 조사류인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 조사류이면 true
	 */
	inline bool isJClass(POSTag tag)
	{
		return POSTag::jks <= tag && tag <= POSTag::jc;
	}

	/**
	 * @brief 품사가 일반명사류인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 일반명사류이면 true
	 */
	inline bool isNNClass(POSTag tag)
	{
		return POSTag::nng <= tag && tag <= POSTag::nnb;
	}

	/**
	 * @brief 품사가 파생접미사인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 파생접미사이면 true
	 */
	inline bool isSuffix(POSTag tag)
	{
		tag = clearIrregular(tag);
		return POSTag::xsn <= tag && tag <= POSTag::xsm;
	}
	
	/**
	 * @brief 품사가 특수문자류인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 특수문자류이면 true
	 */
	inline bool isSpecialClass(POSTag tag)
	{
		return POSTag::sf <= tag && tag <= POSTag::sn;
	}

	/**
	 * @brief 품사가 사용자 정의 품사인지 확인합니다.
	 * @param tag 품사 태그
	 * @return 사용자 정의 품사이면 true
	 */
	inline bool isUserClass(POSTag tag)
	{
		return POSTag::user0 <= tag && tag <= POSTag::user4;
	}
}
