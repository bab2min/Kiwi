/**
 * @file capi.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kiwi C API를 담고 있는 헤더 파일
 * @version 0.20.0
 * @date 2024-07-01
 * 
 * 
 */

#pragma once

#include <stdint.h>
#include "Macro.h"

#define KIWIERR_FAIL -1
#define KIWIERR_INVALID_HANDLE -2
#define KIWIERR_INVALID_INDEX -3

#if !defined(DLL_EXPORT)
#define DECL_DLL
#elif defined(_MSC_VER)
#define DECL_DLL __declspec(dllexport)
#elif defined(__GNUC__)
#define DECL_DLL __attribute__((visibility("default")))
#endif

typedef struct kiwi_s* kiwi_h;
typedef struct kiwi_builder* kiwi_builder_h;
typedef struct kiwi_res* kiwi_res_h;
typedef struct kiwi_ws* kiwi_ws_h;
typedef struct kiwi_ss* kiwi_ss_h;
typedef struct kiwi_joiner* kiwi_joiner_h;
typedef struct kiwi_typo* kiwi_typo_h;
typedef struct kiwi_morphset* kiwi_morphset_h;
typedef struct kiwi_pretokenized* kiwi_pretokenized_h;
typedef unsigned short kchar16_t;

typedef struct kiwi_swtokenizer* kiwi_swtokenizer_h;

typedef struct {
	uint32_t chr_position; /**< 시작 위치(UTF16 문자 기준) */
	uint32_t word_position; /**< 어절 번호(공백 기준)*/
	uint32_t sent_position; /**< 문장 번호*/
	uint32_t line_number; /**< 줄 번호*/
	uint16_t length; /**< 길이(UTF16 문자 기준) */
	uint8_t tag; /**< 품사 태그 */
	union
	{
		uint8_t sense_id; /**< 의미 번호 */
		uint8_t script; /**< 유니코드 영역에 기반한 문자 타입 */
	};
	float score; /**< 해당 형태소의 언어모델 점수 */
	float typo_cost; /**< 오타가 교정된 경우 오타 비용. 그렇지 않은 경우 0 */
	uint32_t typo_form_id; /**< 교정 전 오타의 형태에 대한 정보 (typoCost가 0인 경우 의미 없음) */
	uint32_t paired_token; /**< SSO, SSC 태그에 속하는 형태소의 경우 쌍을 이루는 반대쪽 형태소의 위치(-1인 경우 해당하는 형태소가 없는 것을 뜻함) */
	uint32_t sub_sent_position; /**< 인용부호나 괄호로 둘러싸인 하위 문장의 번호. 1부터 시작. 0인 경우 하위 문장이 아님을 뜻함 */
} kiwi_token_info_t;

/*
int (*kiwi_reader_t)(int id, char* buffer, void* user_data)
id: id number of line to be read. if id == 0, kiwi_reader should roll back file and read lines from the beginning
buffer: buffer where string data should be stored. if buffer == null, kiwi_reader provide the length of string as return value.
user_data: user_data from kiwi_extract~, kiwi_perform, kiwi_analyze_m functions.
*/

/**
 * @brief 문자열을 읽어들여 Kiwi에 제공하기 위한 콜백 함수 타입.
 * 
 * @param int 읽어들일 문자열의 줄 번호입니다. 0부터 시작하여 차례로 1씩 증가합니다. 
 * @param char* 읽어들인 문자열이 저장될 버퍼의 주소입니다. 이 값이 null인 경우 버퍼의 크기를 반환해야 합니다.
 * @param void* user data를 위한 인자입니다.
 * 
 * @return int 두번째 인자가 null인 경우 읽어들일 버퍼의 크기를 반환합니다. 
 */
typedef int(*kiwi_reader_t)(int, char*, void*);
typedef int(*kiwi_reader_w_t)(int, kchar16_t*, void*);


typedef int(*kiwi_receiver_t)(int, kiwi_res_h, void*);

/**
 * @brief 문자열의 변형결과를 Kiwi에 제공하기 위한 콜백 함수 타입
 *
 * @param const char* 원본 문자열의 값입니다.
 * @param int 원본 문자열의 바이트 단위 길이입니다.
 * @param char* 변형된 문자열을 쓸 버퍼입니다. 이 값이 null인 경우 버퍼의 크기를 반환해야 합니다.
 * @param void* user data를 위한 인자입니다.
 *
 * @return int 세번째 인자가 null인 경우 출력할 문자열의 버퍼의 크기를 반환합니다.
 */
typedef int(*kiwi_builder_replacer_t)(const char*, int, char*, void*);

enum
{
	KIWI_BUILD_INTEGRATE_ALLOMORPH = 1,
	KIWI_BUILD_LOAD_DEFAULT_DICT = 2,
	KIWI_BUILD_LOAD_TYPO_DICT = 4,
	KIWI_BUILD_LOAD_MULTI_DICT = 8,
	KIWI_BUILD_DEFAULT = 15,
	KIWI_BUILD_MODEL_TYPE_KNLM = 0x0000,
	KIWI_BUILD_MODEL_TYPE_SBG = 0x0100,
};

enum
{
	KIWI_NUM_THREADS = 0x8001,
	KIWI_MAX_UNK_FORM_SIZE = 0x8002,
	KIWI_SPACE_TOLERANCE = 0x8003,
};

enum
{
	KIWI_CUT_OFF_THRESHOLD = 0x9001,
	KIWI_UNK_FORM_SCORE_SCALE = 0x9002,
	KIWI_UNK_FORM_SCORE_BIAS = 0x9003,
	KIWI_SPACE_PENALTY = 0x9004,
	KIWI_TYPO_COST_WEIGHT = 0x9005,
};

enum
{
	KIWI_MATCH_URL = 1,
	KIWI_MATCH_EMAIL = 2,
	KIWI_MATCH_HASHTAG = 4,
	KIWI_MATCH_MENTION = 8,
	KIWI_MATCH_SERIAL = 16,

	KIWI_MATCH_NORMALIZE_CODA = 1 << 16,
	KIWI_MATCH_JOIN_NOUN_PREFIX = 1 << 17,
	KIWI_MATCH_JOIN_NOUN_SUFFIX = 1 << 18,
	KIWI_MATCH_JOIN_VERB_SUFFIX = 1 << 19,
	KIWI_MATCH_JOIN_ADJ_SUFFIX = 1 << 20,
	KIWI_MATCH_JOIN_ADV_SUFFIX = 1 << 21,
	KIWI_MATCH_JOIN_V_SUFFIX = KIWI_MATCH_JOIN_VERB_SUFFIX | KIWI_MATCH_JOIN_ADJ_SUFFIX,
	KIWI_MATCH_JOIN_AFFIX = KIWI_MATCH_JOIN_NOUN_PREFIX | KIWI_MATCH_JOIN_NOUN_SUFFIX | KIWI_MATCH_JOIN_V_SUFFIX | KIWI_MATCH_JOIN_ADV_SUFFIX,
	KIWI_MATCH_SPLIT_COMPLEX = 1 << 22,
	KIWI_MATCH_Z_CODA = 1 << 23,
	KIWI_MATCH_COMPATIBLE_JAMO = 1 << 24,
	KIWI_MATCH_SPLIT_SAISIOT = 1 << 25,
	KIWI_MATCH_MERGE_SAISIOT = 1 << 26,

	KIWI_MATCH_ALL = KIWI_MATCH_URL | KIWI_MATCH_EMAIL | KIWI_MATCH_HASHTAG | KIWI_MATCH_MENTION | KIWI_MATCH_SERIAL | KIWI_MATCH_Z_CODA,
	KIWI_MATCH_ALL_WITH_NORMALIZING = KIWI_MATCH_ALL | KIWI_MATCH_NORMALIZE_CODA,
};

#ifdef __cplusplus  
extern "C" {
#endif 

/**
 * @brief 설치된 Kiwi의 버전을 반환합니다.
 * 
 * @return "major.minor.patch"로 구성되는 버전 문자열.
 */
DECL_DLL const char* kiwi_version();

/**
 * @brief 현재 스레드에서 발생한 에러 메세지를 반환합니다. 발생한 에러가 없을 경우 null를 반환합니다.
 * 
 * @return 에러 메세지 혹은 null.
 */
DECL_DLL const char* kiwi_error();

/**
 * @brief 현재 스레드의 에러 메세지를 초기화합니다.
 * 
 * @return void
 */
DECL_DLL void kiwi_clear_error();

/**
 * @brief Kiwi Builder를 생성합니다.
 * 
 * @param model_path 모델의 경로.
 * @param num_threads 사용할 스레드의 개수. 0으로 지정시 가용한 스레드 개수를 자동으로 판단합니다.
 * @param options 생성 옵션. KIWI_BUILD_* 열거형을 참조하십시오.
 * @return 성공 시 Kiwi Builder의 핸들을 반환합니다. 
 * 실패시 null를 반환하고 에러 메세지를 설정합니다. 
 * 에러 메세지는 kiwi_error()를 통해 확인할 수 있습니다.
 * 
 * @see kiwi_builder_close
 */
DECL_DLL kiwi_builder_h kiwi_builder_init(const char* model_path, int num_threads, int options);

/**
 * @brief 사용이 끝난 KiwiBuilder를 해제합니다.
 * 
 * @param handle KiwiBuilder의 핸들.
 * @return 성공 시 0를 반환합니다.
 * 
 * @note kiwi_builder_init로 생성된 kiwi_builder_h는 반드시 이 함수로 해제되어야 합니다.
 */
DECL_DLL int kiwi_builder_close(kiwi_builder_h handle);

/**
 * @brief 사용자 형태소를 추가합니다.
 *        이 함수로 등록한 형태소의 경우
 *        언어 모델 내에서 UNK(사전 미등재 단어)로 처리됩니다.
 *        특정 형태소의 변이형을 등록하려는 경우 kiwi_builder_add_alias_word 함수를 사용하는 걸 권장합니다.
 * 
 * @param handle KiwiBuilder의 핸들.
 * @param word 추가할 형태소 (utf-8).
 * @param pos 품사 태그 (kiwi#POSTag).
 * @param score 점수.
 * @return 성공 시 0를 반환합니다.
 */
DECL_DLL int kiwi_builder_add_word(kiwi_builder_h handle, const char* word, const char* pos, float score);


/**
 * @brief 원본 형태소를 기반으로하는 새 형태소를 추가합니다.
 *        kiwi_builder_add_word로 등록한 형태소의 경우 
 *        언어 모델 내에서 UNK(사전 미등재 단어)로 처리되는 반면, 
 *        이 함수로 등록한 형태소의 경우 언어모델 내에서 원본 형태소와 동일하게 처리됩니다.
 *
 * @param handle KiwiBuilder의 핸들.
 * @param alias 새 형태소 (utf-8)
 * @param pos 품사 태그 (kiwi#POSTag).
 * @param score 점수.
 * @param orig_word 원 형태소 (utf-8)
 * @return 성공 시 0를 반환합니다.
 * 만약 orig_word에 pos 태그를 가진 원본 형태소가 존재하지 않는 경우 이 함수는 실패합니다.
 */
DECL_DLL int kiwi_builder_add_alias_word(kiwi_builder_h handle, const char* alias, const char* pos, float score, const char* orig_word);

/**
 * @brief 기분석 형태소열을 추가합니다.
 *        불규칙적으로 분석되어야하는 패턴을 추가하는 데 용이합니다.
 *        예) 사겼다 -> 사귀/VV + 었/EP + 다/EF
 *        
 * 
 * @param handle KiwiBuilder의 핸들.
 * @param form 등록할 형태 (utf-8)
 * @param size 형태소의 개수
 * @param analyzed_morphs size 개수의 const char* 배열의 시작 포인터. 분석되어야할 각 형태소의 형태를 나타냅니다.
 * @param analyzed_pos size 개수의 const char* 배열의 시작 포인터. 분석되어야할 각 형태소의 품사를 나타냅니다.
 * @param score 점수. 기본적으로는 0을 사용합니다. 0보다 클 경우 이 분석 결과가 더 높은 우선순위를, 작을 경우 더 낮은 우선순위를 갖습니다.
 * @param positions size * 2 개수의 int 배열의 시작 포인터. 각 형태소가 형태 내에서 차지하는 위치를 지정합니다. null을 입력하여 생략할 수 있습니다.
 * @return 성공 시 0를 반환합니다.
 * 만약 analyzed_morphs와 analyzed_pos로 지정된 형태소가 사전 내에 존재하지 않으면 이 함수는 실패합니다.
 */
DECL_DLL int kiwi_builder_add_pre_analyzed_word(kiwi_builder_h handle, const char* form, int size, const char** analyzed_morphs, const char** analyzed_pos, float score, const int* positions);

/**
 * @brief 규칙에 의해 변형된 형태소 목록을 생성하여 자동 추가합니다.
 *
 * @param handle KiwiBuilder의 핸들.
 * @param pos 변형할 형태소의 품사 태그
 * @param replacer 변형 결과를 제공하는데에 쓰일 콜백 함수
 * @param user_data replacer 호출시 사용될 유저 데이터
 * @param score 점수. 기본적으로는 0을 사용합니다. 0보다 클 경우 이 변형 결과가 더 높은 우선순위를, 작을 경우 더 낮은 우선순위를 갖습니다.
 * @return 성공 시 새로 추가된 형태소의 개수를 반환합니다. 실패 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_builder_add_rule(kiwi_builder_h handle, const char* pos, kiwi_builder_replacer_t replacer, void* user_data, float score);


/**
 * @brief 사용자 사전으로부터 단어를 읽어들입니다.
 * 
 * @param handle KiwiBuilder의 핸들.
 * @param dict_path 사전 파일 경로 (디렉토리가 아니라 파일명까지 입력해야함).
 * @return 추가된 단어 수.
 */
DECL_DLL int kiwi_builder_load_dict(kiwi_builder_h handle, const char* dict_path);

/**
 * @brief 
 * 
 * @param handle KiwiBuilder의 핸들.
 * @param reader 
 * @param user_data 
 * @param min_cnt 
 * @param max_word_len 
 * @param min_score 
 * @param pos_threshold 
 * @return  
 */
DECL_DLL kiwi_ws_h kiwi_builder_extract_words(kiwi_builder_h handle, kiwi_reader_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);

/**
 * @brief 
 * 
 * @param handle KiwiBuilder의 핸들.
 * @param reader 
 * @param user_data 
 * @param min_cnt 
 * @param max_word_len 
 * @param min_score 
 * @param pos_threshold 
 * @return  
 */
DECL_DLL kiwi_ws_h kiwi_builder_extract_add_words(kiwi_builder_h handle, kiwi_reader_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);

/**
 * @brief 
 * 
 * @param handle KiwiBuilder의 핸들.
 * @param reader 
 * @param user_data 
 * @param min_cnt 
 * @param max_word_len 
 * @param min_score 
 * @param pos_threshold 
 * @return  
 */
DECL_DLL kiwi_ws_h kiwi_builder_extract_words_w(kiwi_builder_h handle, kiwi_reader_w_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);

/**
 * @brief 
 * 
 * @param handle KiwiBuilder의 핸들.
 * @param reader 
 * @param user_data 
 * @param min_cnt 
 * @param max_word_len 
 * @param min_score 
 * @param pos_threshold 
 * @return  
 */
DECL_DLL kiwi_ws_h kiwi_builder_extract_add_words_w(kiwi_builder_h handle, kiwi_reader_w_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);

/**
 * @brief KiwiBuilder로부터 Kiwi instance를 생성합니다.
 * 
 * @param handle KiwiBuilder.
 * @param typos 오타 교정기의 핸들. 오타 교정을 사용하지 않을 경우 null을 입력합니다.
 * @param typo_cost_threshold 오타 교정기에서 생성하는 오타 중 비용이 이 값 이하인 오타만 사용합니다.
 * 
 * @return Kiwi의 핸들.
 * 
 * @note kiwi_close, kiwi_init
 */
DECL_DLL kiwi_h kiwi_builder_build(kiwi_builder_h handle, kiwi_typo_h typos, float typo_cost_threshold);


/**
 * @brief 오타 교정기를 새로 생성합니다.
 * 
 * @return 성공 시 오타 교정기의 핸들을 반환합니다. 실패 시 null를 반환하고 에러 메세지를 설정합니다.
 * 
 * @note 생성된 오타 교정기는 kiwi_typo_close를 통해 반드시 해제되어야 합니다.
 */
DECL_DLL kiwi_typo_h kiwi_typo_init();

/**
 * @brief Kiwi 기본 내장 오타 교정기의 핸들을 반환합니다.
 *
 * @return
 *
 * @note 이 핸들은 kiwi_typo_close에 사용할 수 없음. 
 * 이 함수의 반환값은 kiwi_typo_get_default(KIWI_TYPO_BASIC_TYPO_SET)과 동일합니다.
 * 이 함수보다 더 다양한 기능을 제공하는 kiwi_typo_get_default를 사용하는 것을 권장합니다.
 */
DECL_DLL kiwi_typo_h kiwi_typo_get_basic();


enum
{
	KIWI_TYPO_WITHOUT_TYPO = 0,
	KIWI_TYPO_BASIC_TYPO_SET = 1,
	KIWI_TYPO_CONTINUAL_TYPO_SET = 2,
	KIWI_TYPO_BASIC_TYPO_SET_WITH_CONTINUAL = 3,
	KIWI_TYPO_LENGTHENING_TYPO_SET = 4,
	KIWI_TYPO_BASIC_TYPO_SET_WITH_CONTINUAL_AND_LENGTHENING = 5,
};

/**
 * @brief Kiwi에 기본적으로 내장된 오타 교정기의 핸들을 반환합니다.
 *
 * @return 성공 시 오타 교정기의 핸들을 반환합니다. 실패 시 null를 반환하고 에러 메세지를 설정합니다.
 *
 * @note 이 핸들은 kiwi_typo_close에 사용할 수 없음.
 */
DECL_DLL kiwi_typo_h kiwi_typo_get_default(int kiwi_typo_set);

/**
 * @brief 오타 교정기에 새로운 오타 정의를 추가합니다.
 *
 * @return
 *
 * @note 이 함수는 kiwi_typo_get_default로 얻은 핸들에는 사용할 수 없습니다.
 */
DECL_DLL int kiwi_typo_add(kiwi_typo_h handle, const char** orig, int orig_size, const char** error, int error_size, float cost, int condition);

/**
* @brief 오타 교정기를 복사하여 새로운 핸들을 생성합니다.
*
* @return 성공 시 새로운 오타 교정기의 핸들을 반환합니다. 실패 시 null를 반환하고 에러 메세지를 설정합니다.
*
* @note 복사하여 새로 생성된 오타 교정기의 핸들은 kiwi_typo_close를 통해 반드시 해제되어야 합니다.
*/
DECL_DLL kiwi_typo_h kiwi_typo_copy(kiwi_typo_h handle);

/**
* @brief 현재 오타 교정기에 다른 오타 교정기 내의 오타 정의들을 추가합니다.
* 
* @param handle 오타가 삽입될 교정기의 핸들
* @param src 오타 정의 출처
* @return 성공 시 0를 반환합니다. 실패 시 음수를 반환하고 에러 메세지를 설정합니다.
* 
* @note kiwi_typo_get_default로 얻은 핸들은 handle로 사용할 수 없습니다. src로 사용하는 것은 가능합니다.
*/
DECL_DLL int kiwi_typo_update(kiwi_typo_h handle, kiwi_typo_h src);

/**
* @brief 현재 오타 교정기의 오타 비용을 일정한 비율로 늘리거나 줄입니다.
*
* @param handle 오타 교정기의 핸들
* @param scale 0보다 큰 실수. 모든 오타 비용에 이 값이 곱해집니다.
* @return 성공 시 0를 반환합니다. 실패 시 음수를 반환하고 에러 메세지를 설정합니다.
*/
DECL_DLL int kiwi_typo_scale_cost(kiwi_typo_h handle, float scale);

/**
* @brief 현재 오타 교정기의 연철 오타 비용을 설정합니다.
* 
* @param handle 오타 교정기의 핸들
* @param threshold 연철 오타의 새로운 비용
* @return 성공 시 0를 반환합니다. 실패 시 음수를 반환하고 에러 메세지를 설정합니다.
* 
* @note 연철 오타의 초기값은 무한대, 즉 비활성화 상태입니다. 유한한 값으로 설정하면 연철 오타가 활성화됩니다.
*/
DECL_DLL int kiwi_typo_set_continual_typo_cost(kiwi_typo_h handle, float threshold);

/**
* @brief 현재 오타 교정기의 장음화 오타 비용을 설정합니다.
* 
* @param handle 오타 교정기의 핸들
* @param threshold 장음화 오타의 새로운 비용
* @return 성공 시 0를 반환합니다. 실패 시 음수를 반환하고 에러 메세지를 설정합니다.
* 
* @note 장음화 오타의 초기값은 무한대, 즉 비활성화 상태입니다. 유한한 값으로 설정하면 장음화 오타가 활성화됩니다.
*/
DECL_DLL int kiwi_typo_set_lengthening_typo_cost(kiwi_typo_h handle, float threshold);

/**
 * @brief 생성된 오타 교정기를 해제합니다.
 *
 * @return 성공 시 0를 반환합니다. 실패 시 음수를 반환하고 에러 메세지를 설정합니다.
 *
 * @note kiwi_typo_get_default로 얻은 핸들은 절대 해제해서는 안됩니다.
 */
DECL_DLL int kiwi_typo_close(kiwi_typo_h handle);

/**
 * @brief KiwiBuilder를 거치지 않고 바로 Kiwi instance를 생성합니다.
 * 
 * @param model_path 모델이 들어있는 디렉토리 경로 (e.g., ./models/base).
 * @param num_threads 사용할 쓰레드의 수 (0일 경우, 자동으로 설정).
 * @param options 생성 옵션. KIWI_BUILD_* 참조.
 * @return Kiwi의 핸들.
 */
DECL_DLL kiwi_h kiwi_init(const char* model_path, int num_threads, int options);

/**
 * @brief int 타입 옵션의 값을 변경합니다.
 * 
 * @param handle Kiwi.
 * @param option {KIWI_BUILD_INTEGRATE_ALLOMORPH, KIWI_MAX_UNK_FORM_SIZE, KIWI_SPACE_TOLERANCE}.
 * @param value 옵션의 설정값
 * 
 * @see kiwi_get_option, kiwi_set_option_f
 */
DECL_DLL void kiwi_set_option(kiwi_h handle, int option, int value);

/**
 * @brief int 타입 옵션의 값을 반환합니다.
 * 
 * @param handle  Kiwi.
 * @param option {KIWI_BUILD_INTEGRATE_ALLOMORPH, KIWI_NUM_THREADS, KIWI_MAX_UNK_FORM_SIZE, KIWI_SPACE_TOLERANCE}.
 * @return 해당 옵션의 값을 반환합니다.
 *
 * - KIWI_BUILD_INTEGRATE_ALLOMORPH: 이형태 통합 기능 사용 유무 (0 혹은 1)
 * - KIWI_NUM_THREADS: 사용중인 쓰레드 수 (1 이상의 정수)
 * - KIWI_MAX_UNK_FORM_SIZE: 추출 가능한 사전 미등재 형태의 최대 길이 (0 이상의 정수)
 * - KIWI_SPACE_TOLERANCE: 무시할 수 있는 공백의 최대 개수 (0 이상의 정수)
 */
DECL_DLL int kiwi_get_option(kiwi_h handle, int option);

/**
 * @brief float 타입 옵션의 값을 변경합니다.
 *
 * @param handle Kiwi.
 * @param option {KIWI_CUT_OFF_THRESHOLD, KIWI_UNK_FORM_SCORE_SCALE, KIWI_UNK_FORM_SCORE_BIAS, KIWI_SPACE_PENALTY}.
 * @param value 옵션의 설정값
 * 
 * @see kiwi_get_option_f, kiwi_set_option
 */
DECL_DLL void kiwi_set_option_f(kiwi_h handle, int option, float value);

/**
 * @brief float 타입 옵션의 값을 반환합니다.
 *
 * @param handle  Kiwi.
 * @param option {KIWI_CUT_OFF_THRESHOLD, KIWI_UNK_FORM_SCORE_SCALE, KIWI_UNK_FORM_SCORE_BIAS, KIWI_SPACE_PENALTY}.
 * @return 해당 옵션의 값을 반환합니다.
 *
 * - KIWI_CUT_OFF_THRESHOLD: 분석 과정에서 이 값보다 더 크게 차이가 나는 후보들은 제거합니다.
 * - KIWI_UNK_FORM_SCORE_SCALE: 미등재 형태 추출 시 사용하는 기울기 값
 * - KIWI_UNK_FORM_SCORE_BIAS: 미등재 형태 추출 시 사용하는 편차 값
 * - KIWI_SPACE_PENALTY: 무시하는 공백 1개당 발생하는 언어 점수 페널티 값
 */
DECL_DLL float kiwi_get_option_f(kiwi_h handle, int option);

/**
 * @brief 새 형태소집합을 생성합니다. 형태소집합은 kiwi_analyze 함수의 blocklist 등으로 사용될 수 있습니다.
 * 
 * @param handle Kiwi.
 * @return 새 형태소 집합의 핸들. kiwi_morphset_* 함수에 사용가능합니다. 이 핸들은 사용 후 kiwi_morphset_close를 통해 반드시 해제되어야 합니다.
 */
DECL_DLL kiwi_morphset_h kiwi_new_morphset(kiwi_h handle);

/**
 * @brief 텍스트를 분석해 형태소 결과를 반환합니다.
 *
 * @param handle Kiwi.
 * @param text 분석할 텍스트 (utf-16).
 * @param top_n 반환할 결과물.
 * @param match_options KIWI_MATCH_ALL 등 KIWI_MATCH_* 열거형 참고.
 * @param blocklist 분석 후보 탐색 과정에서 blocklist에 포함된 형태소들은 배제됩니다. null 입력 시에는 blocklist를 사용하지 않습니다.
 * @param pretokenized 입력 텍스트 중 특정 영역의 분석 방법을 강제로 지정합니다. null 입력 시에는 pretokenization을 사용하지 않습니다.
 * @return 형태소 분석 결과의 핸들. kiwi_res_* 함수를 통해 값에 접근가능합니다. 이 핸들은 사용 후 kiwi_res_close를 사용해 반드시 해제되어야 합니다.
 * 
 * @see kiwi_analyze
 */
DECL_DLL kiwi_res_h kiwi_analyze_w(kiwi_h handle, const kchar16_t* text, int top_n, int match_options, kiwi_morphset_h blocklist, kiwi_pretokenized_h pretokenized);

/**
 * @brief 텍스트를 분석해 형태소 결과를 반환합니다.
 * 
 * @param handle Kiwi.
 * @param text 분석할 텍스트 (utf-8).
 * @param top_n 반환할 결과물.
 * @param match_options KIWI_MATCH_ALL 등 KIWI_MATCH_* 열거형 참고.
 * @param blocklist 분석 후보 탐색 과정에서 blocklist에 포함된 형태소들은 배제됩니다. null 입력 시에는 blocklist를 사용하지 않습니다.
 * @param pretokenized 입력 텍스트 중 특정 영역의 분석 방법을 강제로 지정합니다. null 입력 시에는 pretokenization을 사용하지 않습니다.
 * @return 형태소 분석 결과의 핸들. kiwi_res_* 함수를 통해 값에 접근가능합니다. 이 핸들은 사용 후 kiwi_res_close를 사용해 반드시 해제되어야 합니다.
 * 
 * @see kiwi_analyze_w
 */
DECL_DLL kiwi_res_h kiwi_analyze(kiwi_h handle, const char* text, int top_n, int match_options, kiwi_morphset_h blocklist, kiwi_pretokenized_h pretokenized);

/**
 * @brief 
 * 
 * @param handle 
 * @param reader 
 * @param receiver 
 * @param user_data 
 * @param top_n 
 * @param match_options 
 * @return  
 */
DECL_DLL int kiwi_analyze_mw(kiwi_h handle, kiwi_reader_w_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options, kiwi_morphset_h blocklist);

/**
 * @brief 
 * 
 * @param handle 
 * @param reader 
 * @param receiver 
 * @param user_data 
 * @param top_n 
 * @param match_options 
 * @return  
 */
DECL_DLL int kiwi_analyze_m(kiwi_h handle, kiwi_reader_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options, kiwi_morphset_h blocklist);

/**
 * @brief 텍스트를 문장 단위로 분할합니다.
 *
 * @param handle Kiwi.
 * @param text 분할할 텍스트 (utf-16).
 * @param match_options KIWI_MATCH_ALL 등 KIWI_MATCH_* 열거형 참고.
 * @param tokenized_res (선택사항) 형태소 분석 결과를 받으려는 경우 kiwi_res_h 값을 받을 포인터를 넘겨주세요.
 *              null을 입력시 형태소 분석 결과는 내부적으로 사용된 뒤 버려집니다.
 * @return 문장 분할 결과의 핸들. kiwi_ss_* 함수를 통해 값에 접근가능합니다.  이 핸들은 사용 후 kiwi_ss_close를 사용해 반드시 해제되어야 합니다.
 * 
 * @see kiwi_split_into_sents
 */
DECL_DLL kiwi_ss_h kiwi_split_into_sents_w(kiwi_h handle, const kchar16_t* text, int match_options, kiwi_res_h* tokenized_res);

/**
 * @brief 텍스트를 문장 단위로 분할합니다.
 *
 * @param handle Kiwi.
 * @param text 분할할 텍스트 (utf-8).
 * @param match_options KIWI_MATCH_ALL 등 KIWI_MATCH_* 열거형 참고.
 * @param tokenized_res (선택사항) 형태소 분석 결과를 받으려는 경우 kiwi_res_h 값을 받을 포인터를 넘겨주세요. 
 *              null을 입력시 형태소 분석 결과는 내부적으로 사용된 뒤 버려집니다.
 * @return 문장 분할 결과의 핸들. kiwi_ss_* 함수를 통해 값에 접근가능합니다.  이 핸들은 사용 후 kiwi_ss_close를 통해 반드시 해제되어야 합니다.
 * 
 * @see kiwi_split_into_sents_w
 */
DECL_DLL kiwi_ss_h kiwi_split_into_sents(kiwi_h handle, const char* text, int match_options, kiwi_res_h* tokenized_res);

/**
 * @brief 형태소를 결합하여 텍스트로 만들어주는 Joiner를 새로 생성합니다.
 *
 * @param handle Kiwi.
 * @param lm_search True일 경우 언어 모델 탐색을 사용하여 최적의 품사를 선택합니다.
 * @return 새 Joiner의 핸들. kiwi_joiner_* 함수에 사용가능합니다. 이 핸들은 사용 후 kiwi_joiner_close를 통해 반드시 해제되어야 합니다.
 */
DECL_DLL kiwi_joiner_h kiwi_new_joiner(kiwi_h handle, int lm_search);

/**
 * @brief 사용이 완료된 Kiwi객체를 해제합니다.
 * 
 * @param handle Kiwi 핸들
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 * 
 * @note kiwi_builder_build 및 kiwi_init으로 생성된 kiwi_h는 반드시 이 함수로 해제되어야 합니다.
 */
DECL_DLL int kiwi_close(kiwi_h handle);

/**
 * @brief 분석 결과 내에 포함된 리스트의 개수를 반환합니다.
 * 
 * @param result 분석 결과의 핸들
 * @return 성공시 0이상의 값, 실패 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_res_size(kiwi_res_h result);

/**
 * @brief index번째 분석 결과의 확률 점수를 반환합니다.
 * 
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @return 성공 시 0이 아닌 값, 실패 시 0을 반환합니다.
 */
DECL_DLL float kiwi_res_prob(kiwi_res_h result, int index);

/**
 * @brief index번째 분석 결과 내에 포함된 형태소의 개수를 반환합니다.
 * 
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @return 성공시 0이상의 값, 실패 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_res_word_num(kiwi_res_h result, int index);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 정보를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return 형태소 정보가 담긴 `kiwi_token_info_t`에 대한 포인터를 반환합니다. 실패 시 null을 반환합니다. 이 포인터는 Kiwi API가 관리하므로 별도로 해제할 필요가 없습니다.
 */
DECL_DLL const kiwi_token_info_t* kiwi_res_token_info(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 형태를 반환합니다.
 * 
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return UTF-16으로 인코딩된 문자열. 실패 시 null을 반환합니다. 이 포인터는 Kiwi API가 관리하므로 별도로 해제할 필요가 없습니다.
 */
DECL_DLL const kchar16_t* kiwi_res_form_w(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 품사 태그를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return UTF-16으로 인코딩된 문자열. 실패 시 null을 반환합니다. 이 값은 Kiwi API가 관리하므로 별도로 해제할 필요가 없습니다.
 */
DECL_DLL const kchar16_t* kiwi_res_tag_w(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 형태를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return UTF-8으로 인코딩된 문자열. 실패 시 null을 반환합니다. 이 값은 Kiwi API가 관리하므로 별도로 해제할 필요가 없습니다.
 */
DECL_DLL const char* kiwi_res_form(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 품사 태그를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return UTF-8으로 인코딩된 문자열. 실패 시 null을 반환합니다. 이 값은 Kiwi API가 관리하므로 별도로 해제할 필요가 없습니다.
 */
DECL_DLL const char* kiwi_res_tag(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 시작 위치(UTF-16 문자열 기준)를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return 성공 시 0 이상의 값, 실패 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_res_position(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 길이(UTF-16 문자열 기준)를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return 성공 시 0 이상의 값, 실패 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_res_length(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 문장 내 어절 번호를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return 성공 시 0 이상의 값, 실패 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_res_word_position(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 문장 번호를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return 성공 시 0 이상의 값, 실패 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_res_sent_position(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 언어 모델 점수를 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return 성공 시 0이 아닌 값, 실패 시 0을 반환합니다.
 */
DECL_DLL float kiwi_res_score(kiwi_res_h result, int index, int num);

/**
 * @brief index번째 분석 결과의 num번째 형태소의 오타 교정 비용을 반환합니다.
 *
 * @param result 분석 결과의 핸들
 * @param index `0` 이상 `kiwi_res_size(result)` 미만의 정수
 * @param num `0` 이상 `kiwi_res_word_num(result, index)` 미만의 정수
 * @return 성공 시 0 이상의 값, 실패 시 음수를 반환합니다. 0은 오타 교정이 발생하지 않았음을 뜻합니다.
 */
DECL_DLL float kiwi_res_typo_cost(kiwi_res_h result, int index, int num);

/**
 * @brief 사용이 완료된 형태소 분석 결과를 해제합니다.
 *
 * @param handle 형태소 분석 결과 핸들
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 * 
 * @note kiwi_analyze 계열의 함수들에서 반환된 kiwi_res_h 값들은 반드시 이 함수를 통해 해제되어야 합니다.
 */
DECL_DLL int kiwi_res_close(kiwi_res_h result);


/**
 * @brief 
 * 
 * @param result 
 * @return  
 */
DECL_DLL int kiwi_ws_size(kiwi_ws_h result);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @return 
 */
DECL_DLL const kchar16_t* kiwi_ws_form_w(kiwi_ws_h result, int index);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @return 
 */
DECL_DLL const char* kiwi_ws_form(kiwi_ws_h result, int index);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @return  
 */
DECL_DLL float kiwi_ws_score(kiwi_ws_h result, int index);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @return  
 */
DECL_DLL int kiwi_ws_freq(kiwi_ws_h result, int index);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @return  
 */
DECL_DLL float kiwi_ws_pos_score(kiwi_ws_h result, int index);

/**
 * @brief 
 * 
 * @param result 
 * @return  
 */
DECL_DLL int kiwi_ws_close(kiwi_ws_h result);

/**
 * @brief
 *
 * @param result
 * @return
 */
DECL_DLL int kiwi_ss_size(kiwi_ss_h result);

/**
 * @brief
 *
 * @param result
 * @param index
 * @return
 */
DECL_DLL int kiwi_ss_begin_position(kiwi_ss_h result, int index);

/**
 * @brief
 *
 * @param result
 * @param index
 * @return
 */
DECL_DLL int kiwi_ss_end_position(kiwi_ss_h result, int index);

/**
 * @brief 사용이 완료된 문장 분리 객체를 해제합니다.
 *
 * @param handle 문장 분리 결과 핸들
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 * 
 * @note kiwi_split_into_sents 계열 함수에서 반환된 kiwi_ss_h는 반드시 이 함수로 해제되어야 합니다.
 */
DECL_DLL int kiwi_ss_close(kiwi_ss_h result);

/**
 * @brief Joiner에 새 형태소를 삽입합니다.
 *
 * @param handle Joiner 객체의 핸들
 * @param form 삽입할 형태소의 형태
 * @param tag 삽입할 형태소의 품사 태그
 * @param option 1이면 불규칙 활용여부를 자동으로 탐색합니다. 0인 경우 tag로 입력한 불규칙 활용여부를 그대로 사용합니다.
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 */
DECL_DLL int kiwi_joiner_add(kiwi_joiner_h handle, const char* form, const char* tag, int option);

/**
 * @brief Joiner에 삽입된 형태소들을 텍스트로 결합하여 반환합니다.
 *
 * @param handle Joiner 객체의 핸들
 * @return 성공시 UTF-8로 인코딩된 텍스트의 포인터를 반환합니다. 실패시 null을 반환합니다.
 */
DECL_DLL const char* kiwi_joiner_get(kiwi_joiner_h handle);

/**
 * @brief Joiner에 삽입된 형태소들을 텍스트로 결합하여 반환합니다.
 *
 * @param handle Joiner 객체의 핸들
 * @return 성공시 UTF-16로 인코딩된 텍스트의 포인터를 반환합니다. 실패시 null을 반환합니다.
 */
DECL_DLL const kchar16_t* kiwi_joiner_get_w(kiwi_joiner_h handle);

/**
 * @brief 사용이 완료된 Joiner 객체를 해제합니다.
 *
 * @param handle 해제할 Joiner 객체의 핸들
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 * 
 * @note kiwi_new_joiner 함수에서 반환된 kiwi_joiner_h는 반드시 이 함수로 해제되어야 합니다.
 */
DECL_DLL int kiwi_joiner_close(kiwi_joiner_h handle);

/**
 * @brief 형태소 집합에 특정 형태소를 삽입합니다.
 * 
 * @param handle 형태소 집합의 핸들
 * @param form 삽입할 형태소의 형태
 * @param tag 삽입할 형태소의 품사 태그. 만약 이 값을 null로 설정하면 형태가 form과 일치하는 형태소가 품사에 상관없이 모두 삽입됩니다.
 * @return 집합에 추가된 형태소의 개수를 반환합니다. 만약 form, tag로 지정한 형태소가 없는 경우 0을 반환합니다. 오류 발생 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_morphset_add(kiwi_morphset_h handle, const char* form, const char* tag);

/**
 * @brief 형태소 집합에 특정 형태소를 삽입합니다.
 *
 * @param handle 형태소 집합의 핸들
 * @param form 삽입할 형태소의 형태
 * @param tag 삽입할 형태소의 품사 태그. 만약 이 값을 null로 설정하면 형태가 form과 일치하는 형태소가 품사에 상관없이 모두 삽입됩니다.
 * @return 집합에 추가된 형태소의 개수를 반환합니다. 만약 form, tag로 지정한 형태소가 없는 경우 0을 반환합니다. 오류 발생 시 음수를 반환합니다.
 */
DECL_DLL int kiwi_morphset_add_w(kiwi_morphset_h handle, const kchar16_t* form, const char* tag);

/**
 * @brief 사용이 완료된 형태소 집합 객체를 해제합니다.
 *
 * @param handle 해제할 형태소 집합의 핸들
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 *
 * @note kiwi_new_morphset 함수에서 반환된 kiwi_morphset_h는 반드시 이 함수로 해제되어야 합니다.
 */
DECL_DLL int kiwi_morphset_close(kiwi_morphset_h handle);

/**
 * @brief 새로운 SwTokenizer 객체를 생성합니다.
 * 
 * @param path 읽어들일 json 파일의 경로
 * @param kiwi SwTokenizer에서 사용할 Kiwi의 핸들
 * @return 성공 시 SwTokenizer의 핸들을 반환합니다. 실패 시 null를 반환합니다.
 * 
 * @note 인자로 주어진 kiwi는 해당 SwTokenizer가 사용 중일 때는 해제되면 안됩니다.
 * 이 함수로 생성된 핸들은 사용이 끝난 뒤 kiwi_swt_close로 해제되어야 합니다.
 */
DECL_DLL kiwi_swtokenizer_h kiwi_swt_init(const char* path, kiwi_h kiwi);

/**
 * @brief 주어진 문자열을 token ids로 변환합니다.
 * 
 * @param handle SwTokenizer의 핸들
 * @param text token ids로 변환할 UTF8 문자열
 * @param text_size text가 가리키는 문자열의 길이. 음수로 지정할 경우 text를 null-terminated string으로 간주하고 자동으로 길이를 계산합니다.
 * @param token_ids token ids 결과를 돌려받을 버퍼. 이 값을 null로 줄 경우 전체 토큰의 개수를 계산해줍니다.
 * @param token_ids_buf_size token_ids 버퍼의 크기
 * @param offsets token ids의 바이트 단위 offset를 돌려받을 버퍼. offset이 필요없는 경우에는 null로 지정할 수 있습니다.
 * @param offset_buf_size offsets 버퍼의 크기. offset 버퍼의 크기는 최소 token_ids 버퍼 크기의 두 배여야 합니다.
 * @return token_ids가 null인 경우 해당 텍스트를 변환했을때의 토큰 개수를 반환합니다. 실패 시 -1를 반환합니다.
 * token_ids가 null이 아닌 경우 성공 시 token_ids에 입력된 토큰의 개수를 반환합니다. 실패 시 -1를 반환합니다.
 * 
 * @note 임의의 텍스트를 token ids로 변환하면 정확하게 몇 개의 토큰이 생성될 지 아는 것은 어렵습니다. 
 * 따라서 먼저 token_ids를 null로 입력하여 토큰의 개수를 확인한 뒤 충분한 크기의 메모리를 확보하여 이 함수를 다시 호출하는 것이 좋습니다.
 * 
 \code{.c}
 const char* text = "어떤 텍스트";
 int token_size = kiwi_swt_encode(handle, text, -1, NULL, 0, NULL, 0);
 if (token_size < 0) exit(1); // failure
 int* token_ids_buf = malloc(sizeof(int) * token_size);
 int* offset_ids_buf = malloc(sizeof(int) * token_size * 2);
 int result = kiwi_swt_encode(handle, text, -1, token_ids_buf, token_size, offset_ids_buf, token_size * 2);
 if (result < 0) exit(1); // failure
 \endcode 
 */
DECL_DLL int kiwi_swt_encode(kiwi_swtokenizer_h handle, const char* text, int text_size, int* token_ids, int token_ids_buf_size, int* offsets, int offset_buf_size);

/**
 * @brief 주어진 token ids를 UTF8 문자열로 변환합니다.
 * 
 * @param handle SwTokenizer의 핸들
 * @param token_ids UTF8 문자열로 변환할 token ids
 * @param token_size token ids의 길이
 * @param text 변환된 문자열이 저장될 버퍼. 이 값을 null로 줄 경우에 해당 문자열을 저장하는데에 필요한 버퍼의 크기를 반환해줍니다.
 * @param text_buf_size text 버퍼의 크기
 * @return text가 null인 경우 텍스트로 변환했을때의 바이트 길이를 반환합니다. 실패 시 -1를 반환합니다.
 * text가 null이 아닌 경우 성공 시 text에 입력된 바이트 개수를 반환합니다. 실패 시 -1를 반환합니다.
 * 
 * @note 임의의 token ids를 변환 시의 결과 텍스트 길이를 정확하게 예측하는 것은 어렵습니다. 
 * 따라서 먼저 text를 null로 입력하여 결과 텍스트의 길이를 확인한 뒤 충분한 크기의 메모리를 확보하여 이 함수를 다시 호출하는 것이 좋습니다.
 * 
 * 이 함수가 text에 생성된 문자열을 쓸 때 null 문자는 포함되지 않습니다. 
 * 따라서 null-terminated string으로 사용하고자 한다면 버퍼 할당 시 1바이트를 추가로 할당하여 마지막 바이트에 '\0'을 입력하는 것이 필요합니다.
 * 
 \code{.c}
 int token_ids[5] = {10, 15, 20, 13, 8};
 int text_size = kiwi_swt_decode(handle, token_ids, 5, NULL, 0);
 if (text_size < 0) exit(1); // failure
 char* text_buf = malloc(text_size + 1); // + 1 for null character
 int result = kiwi_swt_decode(handle, token_ids, 5, text, text_size);
 if (result < 0) exit(1); // failure
 text_buf[text_size] = 0; // set the last byte as null
 \endcode 
 */
DECL_DLL int kiwi_swt_decode(kiwi_swtokenizer_h handle, const int* token_ids, int token_size, char* text, int text_buf_size);

/**
 * @brief 사용이 끝난 SwTokenizer 객체를 해제합니다.
 * 
 * @param handle 해제할 SwTokenizer의 핸들
 * @return 성공 시 0, 실패 시 0이 아닌 값을 반환합니다.
 * 
 * @note kiwi_swt_init
 */
DECL_DLL int kiwi_swt_close(kiwi_swtokenizer_h handle);

/**
 * @brief 새로운 Pretokenzation 객체를 생성합니다.
 *
 * @return 성공 시 Pretokenzation의 핸들을 반환합니다. 실패 시 null를 반환합니다.
 *
 * @note 이 객체는 kiwi_analyze 계열 함수의 `pretokenized` 인자로 사용됩니다.
 */
DECL_DLL kiwi_pretokenized_h kiwi_pt_init();

/**
 * @brief Pretokenization 객체에 새 구간을 추가합니다.
 *
 * @param handle Pretokenization의 핸들
 * @param begin 구간의 시작 지점
 * @param end 구간의 끝 지점
 * @return 성공 시 새 구간의 id, 실패시 음수를 반환합니다.
 * 
 * @note begin, end로 지정하는 시작/끝 지점의 단위는 이 객체가 kiwi_analyze에 사용되는지, kiwi_analyze_w에 사용되는지에 따라 달라집니다.
 *     kiwi_analyze에 사용되는 경우 utf-8 문자열의 바이트 단위에 따라 시작/끝 지점이 처리되고,
 *     kiwi_analyze_w에 사용되는 경우 utf-16 문자열의 글자 단위에 따라 시작/끝 지점이 처리됩니다.
 * 
 * @see kiwi_analyze, kiwi_analyze_w
 */
DECL_DLL int kiwi_pt_add_span(kiwi_pretokenized_h handle, int begin, int end);

/**
 * @brief Pretokenization 객체의 구간에 새 분석 결과를 추가합니다.
 *
 * @param handle Pretokenization의 핸들
 * @param span_id 구간의 id
 * @param form 분석 결과의 형태
 * @param tag 분석 결과의 품사 태그
 * @param begin 분석 결과의 시작 지점
 * @param end 분석 결와의 끝 지점
 * @return 성공 시 0, 실패 시 0이 아닌 값을 반환합니다.
 *
 * @note begin, end로 지정하는 시작/끝 지점의 단위는 kiwi_pt_add_span와 마찬가지로 Pretokenization객체가 사용되는 곳이 kiwi_analyze인지 kiwi_analyze_w인지에 따라 달라집니다.
 */
DECL_DLL int kiwi_pt_add_token_to_span(kiwi_pretokenized_h handle, int span_id, const char* form, const char* tag, int begin, int end);

/**
 * @brief Pretokenization 객체의 구간에 새 분석 결과를 추가합니다.
 *
 * @param handle Pretokenization의 핸들
 * @param span_id 구간의 id
 * @param form 분석 결과의 형태
 * @param tag 분석 결과의 품사 태그
 * @param begin 분석 결과의 시작 지점
 * @param end 분석 결와의 끝 지점
 * @return 성공 시 0, 실패 시 0이 아닌 값을 반환합니다.
 *
 * @note begin, end로 지정하는 시작/끝 지점의 단위는 kiwi_pt_add_span와 마찬가지로 Pretokenization객체가 사용되는 곳이 kiwi_analyze인지 kiwi_analyze_w인지에 따라 달라집니다.
 */
DECL_DLL int kiwi_pt_add_token_to_span_w(kiwi_pretokenized_h handle, int span_id, const kchar16_t* form, const char* tag, int begin, int end);

/**
 * @brief 사용이 끝난 Pretokenzation 객체를 해제합니다.
 *
 * @param handle 해제할 Pretokenzation의 핸들
 * @return 성공 시 0, 실패 시 0이 아닌 값을 반환합니다.
 *
 * @note kiwi_pt_init
 */
DECL_DLL int kiwi_pt_close(kiwi_pretokenized_h handle);

/**
 * @brief `kiwi_token_info_t`의 `script`가 가리키는 문자 영역의 유니코드 상 이름을 반환합니다.
 *
 * @param script `kiwi_token_info_t`의 `script` 필드 값
 * @return 유니코드 영역의 이름을 반환합니다. 알 수 없을 경우 "Unknown"을 반환합니다.
 *
 * @note 이 함수가 반환하는 값은 string literal이므로 별도로 해제할 필요가 없습니다.
 */
DECL_DLL const char* kiwi_get_script_name(uint8_t script);

#ifdef __cplusplus  
}
#endif 
