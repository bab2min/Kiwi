/**
 * @file capi.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kiwi C API를 담고 있는 헤더 파일
 * @version 0.11.0
 * @date 2022-03-19
 * 
 * 
 */

#pragma once

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
typedef unsigned short kchar16_t;

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
	KIWI_BUILD_DEFAULT = 3,
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
};

enum
{
	KIWI_MATCH_URL = 1,
	KIWI_MATCH_EMAIL = 2,
	KIWI_MATCH_HASHTAG = 4,
	KIWI_MATCH_MENTION = 8,
	KIWI_MATCH_ALL = KIWI_MATCH_URL | KIWI_MATCH_EMAIL | KIWI_MATCH_HASHTAG | KIWI_MATCH_MENTION,
	KIWI_MATCH_NORMALIZE_CODA = 1 << 16,
	KIWI_MATCH_ALL_WITH_NORMALIZING = KIWI_MATCH_ALL | KIWI_MATCH_NORMALIZE_CODA,

	KIWI_MATCH_JOIN_NOUN_PREFIX = 1 << 17,
	KIWI_MATCH_JOIN_NOUN_SUFFIX = 1 << 18,
	KIWI_MATCH_JOIN_VERB_SUFFIX = 1 << 19,
	KIWI_MATCH_JOIN_ADJ_SUFFIX = 1 << 20,
	KIWI_MATCH_JOIN_V_SUFFIX = KIWI_MATCH_JOIN_VERB_SUFFIX | KIWI_MATCH_JOIN_ADJ_SUFFIX,
	KIWI_MATCH_JOIN_NOUN_AFFIX = KIWI_MATCH_JOIN_NOUN_PREFIX | KIWI_MATCH_JOIN_NOUN_SUFFIX | KIWI_MATCH_JOIN_V_SUFFIX,
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
 * @brief 현재 스레드에서 발생한 에러 메세지를 반환합니다. 발생한 에러가 없을 경우 nullptr를 반환합니다.
 * 
 * @return 에러 메세지 혹은 nullptr.
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
 * 실패시 nullptr를 반환하고 에러 메세지를 설정합니다. 
 * 에러 메세지는 kiwi_error()를 통해 확인할 수 있습니다.
 * 
 * @see kiwi_builder_close
 */
DECL_DLL kiwi_builder_h kiwi_builder_init(const char* model_path, int num_threads, int options);

/**
 * @brief 사용이 끝난 KiwiBuilder를 삭제합니다.
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
 * @param dict_path 사전 파일 경로.
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
 * @return Kiwi의 핸들.
 * 
 * @note kiwi_close, kiwi_init
 */
DECL_DLL kiwi_h kiwi_builder_build(kiwi_builder_h handle);

/**
 * @brief KiwiBuilder를 거치지 않고 바로 Kiwi instance를 생성합니다.
 * 
 * @param model_path 모델이 들어있는 디렉토리 경로 (e.g., ./ModelGenerator).
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
 * @brief 텍스트를 분석해 형태소 결과를 반환합니다.
 *
 * @param handle Kiwi.
 * @param text 분석할 텍스트 (utf-16).
 * @param top_n 반환할 결과물.
 * @param match_options KIWI_MATCH_ALL 등 KIWI_MATCH_* 열거형 참고.
 * @return 형태소 분석 결과의 핸들. kiwi_res_* 함수를 통해 값에 접근가능합니다. 이 핸들은 사용 후 kiwi_res_close를 사용해 반드시 해제되어야 합니다.
 * 
 * @see kiwi_analyze
 */
DECL_DLL kiwi_res_h kiwi_analyze_w(kiwi_h handle, const kchar16_t* text, int top_n, int match_options);

/**
 * @brief 텍스트를 분석해 형태소 결과를 반환합니다.
 * 
 * @param handle Kiwi.
 * @param text 분석할 텍스트 (utf-8).
 * @param top_n 반환할 결과물.
 * @param match_options KIWI_MATCH_ALL 등 KIWI_MATCH_* 열거형 참고.
 * @return 형태소 분석 결과의 핸들. kiwi_res_* 함수를 통해 값에 접근가능합니다. 이 핸들은 사용 후 kiwi_res_close를 사용해 반드시 해제되어야 합니다.
 * 
 * @see kiwi_analyze_w
 */
DECL_DLL kiwi_res_h kiwi_analyze(kiwi_h handle, const char* text, int top_n, int match_options);

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
DECL_DLL int kiwi_analyze_mw(kiwi_h handle, kiwi_reader_w_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options);

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
DECL_DLL int kiwi_analyze_m(kiwi_h handle, kiwi_reader_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options);

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
 * @return 문장 분할 결과의 핸들. kiwi_ss_* 함수를 통해 값에 접근가능합니다.  이 핸들은 사용 후 kiwi_ss_close를 사용해 반드시 해제되어야 합니다.
 * 
 * @see kiwi_split_into_sents_w
 */
DECL_DLL kiwi_ss_h kiwi_split_into_sents(kiwi_h handle, const char* text, int match_options, kiwi_res_h* tokenized_res);


/**
 * @brief 사용이 완료된 Kiwi객체를 삭제합니다.
 * 
 * @param handle Kiwi 핸들
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 * 
 * @note kiwi_builder_build 및 kiwi_init으로 생성된 kiwi_h는 반드시 이 함수로 해제되어야 합니다.
 */
DECL_DLL int kiwi_close(kiwi_h handle);

/**
 * @brief 
 * 
 * @param result 
 * @return  
 */
DECL_DLL int kiwi_res_size(kiwi_res_h result);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @return  
 */
DECL_DLL float kiwi_res_prob(kiwi_res_h result, int index);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @return  
 */
DECL_DLL int kiwi_res_word_num(kiwi_res_h result, int index);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @param num 
 * @return 
 */
DECL_DLL const kchar16_t* kiwi_res_form_w(kiwi_res_h result, int index, int num);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @param num 
 * @return 
 */
DECL_DLL const kchar16_t* kiwi_res_tag_w(kiwi_res_h result, int index, int num);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @param num 
 * @return 
 */
DECL_DLL const char* kiwi_res_form(kiwi_res_h result, int index, int num);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @param num 
 * @return 
 */
DECL_DLL const char* kiwi_res_tag(kiwi_res_h result, int index, int num);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @param num 
 * @return  
 */
DECL_DLL int kiwi_res_position(kiwi_res_h result, int index, int num);

/**
 * @brief 
 * 
 * @param result 
 * @param index 
 * @param num 
 * @return  
 */
DECL_DLL int kiwi_res_length(kiwi_res_h result, int index, int num);

/**
 * @brief
 *
 * @param result
 * @param index
 * @param num
 * @return
 */
DECL_DLL int kiwi_res_word_position(kiwi_res_h result, int index, int num);

/**
 * @brief
 *
 * @param result
 * @param index
 * @param num
 * @return
 */
DECL_DLL int kiwi_res_sent_position(kiwi_res_h result, int index, int num);

/**
 * @brief 사용이 완료된 형태소 분석 결과를 삭제합니다.
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
 * @brief 사용이 완료된 문장 분리 객체를 삭제합니다.
 *
 * @param handle 문장 분리 결과 핸들
 * @return 성공시 0을 반환합니다. 실패시 0이 아닌 값을 반환합니다.
 * 
 * @note kiwi_split_into_sents 계열 함수에서 반환된 kiwi_ss_h는 반드시 이 함수로 해제되어야 합니다.
 */
DECL_DLL int kiwi_ss_close(kiwi_ss_h result);

#ifdef __cplusplus  
}
#endif 
