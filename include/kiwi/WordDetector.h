/**
 * @file WordDetector.h
 * @author bab2min (bab2min@gmail.com)
 * @brief 텍스트에서 미등록 단어를 추출하는 WordDetector 클래스 정의
 * @version 0.22.1
 * @date 2025-11-21
 * 
 * 텍스트 말뭉치에서 통계적 방법을 사용하여 사전에 없는 새로운 단어를 발견합니다.
 * 응집도(cohesion)와 분기 엔트로피(branching entropy)를 활용한 단어 추출 알고리즘을 구현합니다.
 */

#pragma once

#include <kiwi/Types.h>

namespace kiwi
{
	/**
	 * @brief 추출된 단어의 정보를 담는 구조체
	 */
	struct WordInfo
	{
		std::u16string form;          /**< 단어의 표면형 */
		float score;                  /**< 단어 점수 */
		float lBranch;                /**< 좌측 분기 엔트로피 */
		float rBranch;                /**< 우측 분기 엔트로피 */
		float lCohesion;              /**< 좌측 응집도 */
		float rCohesion;              /**< 우측 응집도 */
		uint32_t freq;                /**< 출현 빈도 */
		std::map<POSTag, float> posScore; /**< 품사별 점수 */

		WordInfo(std::u16string _form = {},
			float _score = 0, float _lBranch = 0, float _rBranch = 0,
			float _lCohesion = 0, float _rCohesion = 0, uint32_t _freq = 0,
			std::map<POSTag, float>&& _posScore = {})
			: form(_form), score(_score), lBranch(_lBranch), rBranch(_rBranch),
			lCohesion(_lCohesion), rCohesion(_rCohesion), freq(_freq), posScore(_posScore)
		{}
	};

	/**
	 * @brief 텍스트 말뭉치로부터 미등록 단어를 추출하는 클래스
	 * 
	 * 통계적 방법을 사용하여 텍스트에서 의미 있는 단어를 자동으로 발견합니다.
	 * 응집도와 분기 엔트로피 등의 지표를 계산하여 단어 후보를 평가합니다.
	 */
	class WordDetector
	{
		struct Counter;
	protected:
		size_t numThreads = 0;
		std::map<std::pair<POSTag, bool>, std::map<char16_t, float>> posScore;
		std::map<std::u16string, float> nounTailScore;

		void loadPOSModelFromTxt(std::istream& is);
		void loadNounTailModelFromTxt(std::istream& is);

		void countUnigram(Counter&, const U16Reader& reader, size_t minCnt) const;
		void countBigram(Counter&, const U16Reader& reader, size_t minCnt) const;
		void countNgram(Counter&, const U16Reader& reader, size_t minCnt, size_t maxWordLen) const;
		float branchingEntropy(const std::map<std::u16string, uint32_t>& cnt, std::map<std::u16string, uint32_t>::iterator it, size_t minCnt, float defaultPerp = 1.f) const;
		std::map<POSTag, float> getPosScore(Counter&, const std::map<std::u16string, uint32_t>& cnt, std::map<std::u16string, uint32_t>::iterator it, bool coda, const std::u16string& realForm) const;
	public:

		/**
		 * @brief 원시 데이터로부터 모델을 생성할 때 사용하는 태그
		 */
		struct FromRawData {};
		static constexpr FromRawData fromRawDataTag = {};

		WordDetector() = default;
		
		/**
		 * @brief 사전 학습된 모델을 로드하여 WordDetector를 생성합니다.
		 * @param modelPath 모델 파일 경로
		 * @param _numThreads 사용할 스레드 수 (-1이면 자동)
		 */
		WordDetector(const std::string& modelPath, size_t _numThreads = -1);
		
		/**
		 * @brief 원시 데이터로부터 WordDetector를 생성합니다.
		 * @param tag FromRawData 태그
		 * @param modelPath 원시 데이터 경로
		 * @param _numThreads 사용할 스레드 수 (-1이면 자동)
		 */
		WordDetector(FromRawData, const std::string& modelPath, size_t _numThreads = -1);
		
		/**
		 * @brief 스트림 제공자를 사용하여 WordDetector를 생성합니다.
		 * @param streamProvider 스트림 제공 함수
		 * @param _numThreads 사용할 스레드 수 (-1이면 자동)
		 */
		WordDetector(const std::function<std::unique_ptr<std::istream>(const std::string&)>& streamProvider, size_t _numThreads = -1);

		/**
		 * @brief WordDetector가 사용 가능한 상태인지 확인합니다.
		 * @return 모델이 로드되어 사용 가능하면 true
		 */
		bool ready() const
		{
			return !posScore.empty();
		}

		/**
		 * @brief 학습된 모델을 파일로 저장합니다.
		 * @param modelPath 저장할 모델 파일 경로
		 */
		void saveModel(const std::string& modelPath) const;
		
		/**
		 * @brief 텍스트에서 단어를 추출합니다.
		 * @param reader 텍스트 데이터 리더
		 * @param minCnt 최소 출현 빈도
		 * @param maxWordLen 최대 단어 길이
		 * @param minScore 최소 단어 점수
		 * @return 추출된 단어 정보 벡터
		 */
		std::vector<WordInfo> extractWords(const U16MultipleReader& reader, size_t minCnt = 10, size_t maxWordLen = 10, float minScore = 0.1f) const;
	};

}

