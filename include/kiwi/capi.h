#pragma once

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
typedef struct kiwi_res* kiwi_res_h;
typedef struct kiwi_ws* kiwi_ws_h;
typedef unsigned short kchar16_t;

/*
int (*kiwi_reader_t)(int id, char* buffer, void* user_data)
id: id number of line to be read. if id == 0, kiwi_reader should roll back file and read lines from the beginning
buffer: buffer where string data should be stored. if buffer == null, kiwi_reader provide the length of string as return value.
user_data: user_data from kiwi_extract~, kiwi_perform, kiwi_analyze_m functions.
*/
typedef int(*kiwi_reader_t)(int, char*, void*);
typedef int(*kiwi_reader_w_t)(int, kchar16_t*, void*);


typedef int(*kiwi_receiver_t)(int, kiwi_res_h, void*);

enum
{
	KIWI_LOAD_DEFAULT_DICT = 1,
	KIWI_INTEGRATE_ALLOMORPH = 2,
};

#ifdef __cplusplus  
extern "C" {
#endif 

DECL_DLL const char* kiwi_version();
DECL_DLL const char* kiwi_error();

DECL_DLL kiwi_h kiwi_init(const char* model_path, int num_threads, int options);
DECL_DLL int kiwi_add_user_word(kiwi_h handle, const char* word, const char* pos);
DECL_DLL int kiwi_load_user_dict(kiwi_h handle, const char* dict_path);
DECL_DLL kiwi_ws_h kiwi_extract_words(kiwi_h handle, kiwi_reader_t reader, void* user_data, int min_cnt, int max_word_len, float min_score);
DECL_DLL kiwi_ws_h kiwi_extract_filter_words(kiwi_h handle, kiwi_reader_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);
DECL_DLL kiwi_ws_h kiwi_extract_add_words(kiwi_h handle, kiwi_reader_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);
DECL_DLL kiwi_ws_h kiwi_extract_words_w(kiwi_h handle, kiwi_reader_w_t reader, void* user_data, int min_cnt, int max_word_len, float min_score);
DECL_DLL kiwi_ws_h kiwi_extract_filter_words_w(kiwi_h handle, kiwi_reader_w_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);
DECL_DLL kiwi_ws_h kiwi_extract_add_words_w(kiwi_h handle, kiwi_reader_w_t reader, void* user_data, int min_cnt, int max_word_len, float min_score, float pos_threshold);
DECL_DLL int kiwi_prepare(kiwi_h handle);
DECL_DLL void kiwi_set_option(kiwi_h handle, int option, int value);
DECL_DLL int kiwi_get_option(kiwi_h handle, int option);
DECL_DLL kiwi_res_h kiwi_analyze_w(kiwi_h handle, const kchar16_t* text, int top_n, int match_options);
DECL_DLL kiwi_res_h kiwi_analyze(kiwi_h handle, const char* text, int top_n, int match_options);
DECL_DLL int kiwi_analyze_mw(kiwi_h handle, kiwi_reader_w_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options);
DECL_DLL int kiwi_analyze_m(kiwi_h handle, kiwi_reader_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options);
DECL_DLL int kiwi_perform_w(kiwi_h handle, kiwi_reader_w_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options, int min_cnt, int max_word_len, float min_score, float pos_threshold);
DECL_DLL int kiwi_perform(kiwi_h handle, kiwi_reader_t reader, kiwi_receiver_t receiver, void* user_data, int top_n, int match_options, int min_cnt, int max_word_len, float min_score, float pos_threshold);
DECL_DLL int kiwi_close(kiwi_h handle);

DECL_DLL int kiwi_res_size(kiwi_res_h result);
DECL_DLL float kiwi_res_prob(kiwi_res_h result, int index);
DECL_DLL int kiwi_res_word_num(kiwi_res_h result, int index);
DECL_DLL const kchar16_t* kiwi_res_form_w(kiwi_res_h result, int index, int num);
DECL_DLL const kchar16_t* kiwi_res_tag_w(kiwi_res_h result, int index, int num);
DECL_DLL const char* kiwi_res_form(kiwi_res_h result, int index, int num);
DECL_DLL const char* kiwi_res_tag(kiwi_res_h result, int index, int num);
DECL_DLL int kiwi_res_position(kiwi_res_h result, int index, int num);
DECL_DLL int kiwi_res_length(kiwi_res_h result, int index, int num);
DECL_DLL int kiwi_res_close(kiwi_res_h result);

DECL_DLL int kiwi_ws_size(kiwi_ws_h result);
DECL_DLL const kchar16_t* kiwi_ws_form_w(kiwi_ws_h result, int index);
DECL_DLL const char* kiwi_ws_form(kiwi_ws_h result, int index);
DECL_DLL float kiwi_ws_score(kiwi_ws_h result, int index);
DECL_DLL int kiwi_ws_freq(kiwi_ws_h result, int index);
DECL_DLL float kiwi_ws_pos_score(kiwi_ws_h result, int index);
DECL_DLL int kiwi_ws_close(kiwi_ws_h result);

#ifdef __cplusplus  
}
#endif 