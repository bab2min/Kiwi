/**
 * @file capi.h
 * @author bab2min (bab2min@gmail.com)
 * @brief Kiwi C API를 담고 있는 헤더 파일
 * @version 0.10.0
 * @date 2021-08-31
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
typedef unsigned short kchar16_t;

/*
int (*kiwi_reader_t)(int id, char* buffer, void* user_data)
id: id number of line to be read. if id == 0, kiwi_reader should roll back file and read lines from the beginning
buffer: buffer where string data should be stored. if buffer == null, kiwi_reader provide the length of string as return value.
user_data: user_data from kiwi_extract~, kiwi_perform, kiwi_analyze_m functions.
*/

/**
 * @brief 문자열을 읽어들여 Kiwi에 제공하기 위한 콜백 함수 타입
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

enum
{
	KIWI_BUILD_LOAD_DEFAULT_DICT = 1,
	KIWI_BUILD_INTEGRATE_ALLOMORPH = 2,
	KIWI_BUILD_DEFAULT = 3,
};

enum
{
	KIWI_NUM_THREADS = 0x8001,
};

enum
{
	KIWI_MATCH_URL = 1,
	KIWI_MATCH_EMAIL = 2,
	KIWI_MATCH_HASHTAG = 4,
	KIWI_MATCH_MENTION = 8,
	KIWI_MATCH_ALL = 15,
};

#ifdef __cplusplus  
extern "C" {
#endif 

/**
 * @brief 설치된 Kiwi의 버전을 반환합니다.
 * 
 * @return "major.minor.patch"로 구성되는 버전 문자열
 */
DECL_DLL const char* kiwi_version();

/**
 * @brief 현재 스레드에서 발생한 에러 메세지를 반환합니다. 발생한 에러가 없을 경우 nullptr를 반환합니다.
 * 
 * @return 에러 메세지 혹은 nullptr
 */
DECL_DLL const char* kiwi_error();

/**
 * @brief 현재 스레드의 에러 메세지를 초기화합니다.
 * 
 * @return 
 */
DECL_DLL void kiwi_clear_error();

/**
 * @brief Kiwi Builder를 생성합니다
 * 
 * @param model_path 모델의 경로
 * @param num_threads 사용할 스레드의 개수. 0으로 지정시 가용한 스레드 개수를 자동으로 판단합니다.
 * @param options 생성 옵션. KIWI_BUILD_* 열거형을 참조하십시오.
 * @return 성공 시 Kiwi Builder의 핸들을 반환합니다. 
 * 실패시 nullptr를 반환하고 에러 메세지를 설정합니다. 
 * 에러 메세지는 kiwi_error()를 통해 확인할 수 있습니다.
 */
DECL_DLL kiwi_builder_h kiwi_builder_init(const char* model_path, int num_threads, int options);

/**
 * @brief 
 * 
 * @param handle 
 * @return 
 */
DECL_DLL int kiwi_builder_close(kiwi_builder_h handle);

/**
 * @brief 
 * 
 * @param handle 
 * @param word 
 * @param pos 
 * @param score 
 * @return  
 */
DECL_DLL int kiwi_builder_add_word(kiwi_builder_h handle, const char* word, const char* pos, float score);

/**
 * @brief 
 * 
 * @param handle 
 * @param dict_path 
 * @return  
 */
DECL_DLL int kiwi_builder_load_dict(kiwi_builder_h handle, const char* dict_path);

/**
 * @brief 
 * 
 * @param handle 
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
 * @param handle 
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
 * @param handle 
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
 * @param handle 
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
 * @brief 
 * 
 * @param handle 
 * @return  
 */
DECL_DLL kiwi_h kiwi_builder_build(kiwi_builder_h handle);

/**
 * @brief 
 * 
 * @param model_path 
 * @param num_threads 
 * @param options 
 * @return  
 */
DECL_DLL kiwi_h kiwi_init(const char* model_path, int num_threads, int options);

/**
 * @brief 
 * 
 * @param handle 
 * @param option 
 * @param value 
 * @return  
 */
DECL_DLL void kiwi_set_option(kiwi_h handle, int option, int value);

/**
 * @brief 
 * 
 * @param handle 
 * @param option 
 * @return  
 */
DECL_DLL int kiwi_get_option(kiwi_h handle, int option);

/**
 * @brief 
 * 
 * @param handle 
 * @param text 
 * @param top_n 
 * @param match_options 
 * @return 
 */
DECL_DLL kiwi_res_h kiwi_analyze_w(kiwi_h handle, const kchar16_t* text, int top_n, int match_options);

/**
 * @brief 
 * 
 * @param handle 
 * @param text 
 * @param top_n 
 * @param match_options 
 * @return  
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
 * @brief 
 * 
 * @param handle 
 * @return  
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
 * @return  
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

#ifdef __cplusplus  
}
#endif 
