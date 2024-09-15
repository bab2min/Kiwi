#include "gtest/gtest.h"
#include <cstring>
#include <kiwi/capi.h>
#include "common.h"

kiwi_h reuse_kiwi_instance()
{
	static kiwi_h kw = kiwi_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	return kw;
}

TEST(KiwiC, InitClose) 
{
	kiwi_h kw = kiwi_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_close(kw), 0);
}

TEST(KiwiC, BuilderInitClose)
{
	kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kb, nullptr);
	kiwi_h kw = kiwi_builder_build(kb, nullptr, 0);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_builder_close(kb), 0);
	EXPECT_EQ(kiwi_close(kw), 0);
}

TEST(KiwiC, BuilderAddWords)
{
	kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kb, nullptr);
	EXPECT_EQ(kiwi_builder_add_word(kb, KWORD8, "NNP", 0.0), 0);
	kiwi_h kw = kiwi_builder_build(kb, nullptr, 0);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_builder_close(kb), 0);
	
	kiwi_res_h res = kiwi_analyze(kw, KWORD8, 1, KIWI_MATCH_ALL, nullptr, nullptr);
	EXPECT_NE(res, nullptr);
	const char* word = kiwi_res_form(res, 0, 0);
	EXPECT_NE(word, nullptr);
	EXPECT_EQ(strcmp(word, KWORD8), 0);
	EXPECT_EQ(kiwi_res_close(res), 0);
	EXPECT_EQ(kiwi_close(kw), 0);
}

int mt_reader(int idx, char* buf, void* user)
{
	auto& data = *(std::vector<std::string>*)user;
	if (idx >= data.size()) return 0;
	if (buf == nullptr) return data[idx].size() + 1;
	std::memcpy(buf, data[idx].c_str(), data[idx].size() + 1);
	return 0;
}

int mt_receiver(int idx, kiwi_res_h res, void* user)
{
	kiwi_res_close(res);
	return 0;
}

TEST(KiwiC, AnalyzeMultithread)
{
	auto data = loadTestCorpus();
	kiwi_h kw = kiwi_init(MODEL_PATH, 2, KIWI_BUILD_DEFAULT);
	EXPECT_NE(kw, nullptr);
	EXPECT_EQ(kiwi_analyze_m(kw, mt_reader, mt_receiver, &data, 1, KIWI_MATCH_ALL, nullptr), data.size());
	EXPECT_EQ(kiwi_close(kw), 0);
}

TEST(KiwiC, Issue71_SentenceSplit_u16)
{
	kiwi_h kw = reuse_kiwi_instance();

	const char16_t str[] = u"다녀온 후기\n\n<강남 토끼정에 다녀왔습니다.> 음식도 맛있었어요 다만 역시 토끼정 본점 답죠?ㅎㅅㅎ 그 맛이 크으.. 아주 맛있었음...! ^^";
	const char16_t* ref[] = {
		u"다녀온 후기",
		u"<강남 토끼정에 다녀왔습니다.>",
		u"음식도 맛있었어요",
		u"다만 역시 토끼정 본점 답죠?ㅎㅅㅎ",
		u"그 맛이 크으..",
		u"아주 맛있었음...! ^^",
	};
	const int ref_len = sizeof(ref) / sizeof(ref[0]);

	kiwi_ss_h res = kiwi_split_into_sents_w(kw, (const kchar16_t*)str, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr);
	EXPECT_NE(res, nullptr);
	EXPECT_EQ(kiwi_ss_size(res), ref_len);
	
	for (int i = 0; i < ref_len; ++i)
	{
		std::u16string sent{ str + kiwi_ss_begin_position(res, i), str + kiwi_ss_end_position(res, i) };
		EXPECT_EQ(sent, ref[i]);
	}

	EXPECT_EQ(kiwi_ss_close(res), 0);
}

TEST(KiwiC, Issue71_SentenceSplit_u8)
{
	kiwi_h kw = reuse_kiwi_instance();

	const char str[] = u8"다녀온 후기\n\n<강남 토끼정에 다녀왔습니다.> 음식도 맛있었어요 다만 역시 토끼정 본점 답죠?ㅎㅅㅎ 그 맛이 크으.. 아주 맛있었음...! ^^";
	const char* ref[] = {
		u8"다녀온 후기",
		u8"<강남 토끼정에 다녀왔습니다.>",
		u8"음식도 맛있었어요",
		u8"다만 역시 토끼정 본점 답죠?ㅎㅅㅎ",
		u8"그 맛이 크으..",
		u8"아주 맛있었음...! ^^",
	};
	const int ref_len = sizeof(ref) / sizeof(ref[0]);

	kiwi_ss_h res = kiwi_split_into_sents(kw, str, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr);
	EXPECT_NE(res, nullptr);
	EXPECT_EQ(kiwi_ss_size(res), ref_len);

	for (int i = 0; i < ref_len; ++i)
	{
		std::string sent{ str + kiwi_ss_begin_position(res, i), str + kiwi_ss_end_position(res, i) };
		EXPECT_EQ(sent, ref[i]);
	}

	EXPECT_EQ(kiwi_ss_close(res), 0);
}

int kb_replacer(const char* input, int size, char* output, void* user_data)
{
	if (!output) return size + 1; // add one for null-terminating character

	strncpy(output, input, size);
	if (strcmp(input + size - 3, u8"요") == 0)
	{
		strncpy(output + size - 3, u8"용", 3);
	}
	return 0;
}

TEST(KiwiC, AddRule)
{
	kiwi_h okw = reuse_kiwi_instance();
	kiwi_res_h ores = kiwi_analyze(okw, u8"했어요! 하잖아요! 할까요? 좋아요!", 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);

	{
		kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT & ~KIWI_BUILD_LOAD_TYPO_DICT);

		EXPECT_GT(kiwi_builder_add_rule(kb, "ef", kb_replacer, nullptr, 0), 0);
		
		kiwi_h kw = kiwi_builder_build(kb, nullptr, 0);
		kiwi_res_h res = kiwi_analyze(kw, u8"했어용! 하잖아용! 할까용? 좋아용!", 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
		EXPECT_EQ(kiwi_res_prob(ores, 0), kiwi_res_prob(res, 0));
		kiwi_res_close(res);
		kiwi_close(kw);
		kiwi_builder_close(kb);
	}

	{
		kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT & ~KIWI_BUILD_LOAD_TYPO_DICT);

		EXPECT_GT(kiwi_builder_add_rule(kb, "ef", kb_replacer, nullptr, -1), 0);

		kiwi_h kw = kiwi_builder_build(kb, nullptr, 0);
		kiwi_res_h res = kiwi_analyze(kw, u8"했어용! 하잖아용! 할까용? 좋아용!", 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
		EXPECT_FLOAT_EQ(kiwi_res_prob(ores, 0) - 4, kiwi_res_prob(res, 0));
		kiwi_res_close(res);
		kiwi_close(kw);
		kiwi_builder_close(kb);
	}

	kiwi_res_close(ores);
}

TEST(KiwiC, AddPreAnalyzedWord)
{
	kiwi_builder_h kb = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	const char* morphs[] = {
		u8"팅기", u8"었", u8"어",
	};
	const char* pos[] = {
		"vv", "ep", "ef",
	};

	EXPECT_NE(kiwi_builder_add_pre_analyzed_word(kb, u8"팅겼어", 3, morphs, pos, 0, nullptr), 0);

	kiwi_builder_add_alias_word(kb, u8"팅기", "vv", -1, u8"튕기");
	EXPECT_EQ(kiwi_builder_add_pre_analyzed_word(kb, u8"팅겼어", 3, morphs, pos, 0, nullptr), 0);

	kiwi_h kw = kiwi_builder_build(kb, nullptr, 0);
	kiwi_res_h res = kiwi_analyze(kw, u8"팅겼어...", 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
	
	ASSERT_GE(kiwi_res_word_num(res, 0), 4);
	EXPECT_STREQ(kiwi_res_form(res, 0, 0), u8"팅기");
	EXPECT_STREQ(kiwi_res_tag(res, 0, 0), "VV");
	EXPECT_STREQ(kiwi_res_form(res, 0, 1), u8"었");
	EXPECT_STREQ(kiwi_res_tag(res, 0, 1), "EP");
	EXPECT_STREQ(kiwi_res_form(res, 0, 2), u8"어");
	EXPECT_STREQ(kiwi_res_tag(res, 0, 2), "EF");
	EXPECT_STREQ(kiwi_res_form(res, 0, 3), u8"...");
	EXPECT_STREQ(kiwi_res_tag(res, 0, 3), "SF");
	
	kiwi_res_close(res);
	kiwi_close(kw);
	kiwi_builder_close(kb);
}

TEST(KiwiC, Joiner)
{
	kiwi_h okw = reuse_kiwi_instance();
	kiwi_joiner_h joiner;

	joiner = kiwi_new_joiner(okw, 1);
	EXPECT_EQ(kiwi_joiner_add(joiner, "시동", "NNG", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "를", "JKO", 1), 0);
	EXPECT_EQ(kiwi_joiner_get(joiner), std::string{ u8"시동을" });
	EXPECT_EQ(kiwi_joiner_close(joiner), 0);

	joiner = kiwi_new_joiner(okw, 1);
	EXPECT_EQ(kiwi_joiner_add(joiner, "시도", "NNG", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "를", "JKO", 1), 0);
	EXPECT_EQ(kiwi_joiner_get(joiner), std::string{ u8"시도를" });
	EXPECT_EQ(kiwi_joiner_close(joiner), 0);

	joiner = kiwi_new_joiner(okw, 1);
	EXPECT_EQ(kiwi_joiner_add(joiner, "길", "NNG", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "을", "JKO", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "걷", "VV", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "어요", "EF", 1), 0);
	EXPECT_EQ(kiwi_joiner_get(joiner), std::string{ u8"길을 걸어요" });
	EXPECT_EQ(kiwi_joiner_close(joiner), 0);

	joiner = kiwi_new_joiner(okw, 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "길", "NNG", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "을", "JKO", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "걷", "VV", 1), 0);
	EXPECT_EQ(kiwi_joiner_add(joiner, "어요", "EF", 1), 0);
	EXPECT_EQ(kiwi_joiner_get(joiner), std::string{ u8"길을 걷어요" });
	EXPECT_EQ(kiwi_joiner_close(joiner), 0);
}

TEST(KiwiC, Regularity)
{
	kiwi_h okw = reuse_kiwi_instance();
	kiwi_res_h r = kiwi_analyze(okw, "걷었다", 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);

	EXPECT_STREQ(kiwi_res_tag(r, 0, 0), "VV-R");

	EXPECT_EQ(kiwi_res_close(r), 0);
}

TEST(KiwiC, AnalyzeBasicTypoSet)
{
	kiwi_h okw = reuse_kiwi_instance(), typo_kw;
	kiwi_builder_h builder = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	typo_kw = kiwi_builder_build(builder, kiwi_typo_get_default(KIWI_TYPO_BASIC_TYPO_SET), 2.5f);
	kiwi_set_option_f(typo_kw, KIWI_TYPO_COST_WEIGHT, 5);

	kiwi_res_h o, c;
	for (const char* s : { u8"외않됀데?", u8"나 죰 도와죠.", u8"잘했따", u8"외구거 공부", u8"맗은 믈을 마셧다!" })
	{
		o = kiwi_analyze(okw, s, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
		c = kiwi_analyze(typo_kw, s, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
		EXPECT_TRUE(kiwi_res_prob(o, 0) < kiwi_res_prob(c, 0));
		EXPECT_EQ(kiwi_res_close(o), 0);
		EXPECT_EQ(kiwi_res_close(c), 0);
	}

	EXPECT_EQ(kiwi_builder_close(builder), 0);
	EXPECT_EQ(kiwi_close(typo_kw), 0);
}

TEST(KiwiC, CustomTypoSet)
{
	kiwi_h okw = reuse_kiwi_instance(), typo_kw;
	kiwi_builder_h builder = kiwi_builder_init(MODEL_PATH, 0, KIWI_BUILD_DEFAULT);
	kiwi_typo_h basic_typo = kiwi_typo_get_default(KIWI_TYPO_BASIC_TYPO_SET),
		continual_typo = kiwi_typo_get_default(KIWI_TYPO_CONTINUAL_TYPO_SET),
		lengthening_typo = kiwi_typo_get_default(KIWI_TYPO_LENGTHENING_TYPO_SET),
		custom_typo = kiwi_typo_init();

	kiwi_typo_update(custom_typo, basic_typo);
	kiwi_typo_update(custom_typo, continual_typo);
	kiwi_typo_update(custom_typo, lengthening_typo);

	typo_kw = kiwi_builder_build(builder, custom_typo, 2.5f);
	kiwi_set_option_f(typo_kw, KIWI_TYPO_COST_WEIGHT, 5);

	kiwi_res_h o, c;
	for (const char* s : { u8"외않됀데?", u8"나 죰 도와죠.", u8"자알했따", u8"외구거 공부", u8"맗은 믈을 마셧다!" })
	{
		o = kiwi_analyze(okw, s, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
		c = kiwi_analyze(typo_kw, s, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
		EXPECT_TRUE(kiwi_res_prob(o, 0) < kiwi_res_prob(c, 0));
		EXPECT_EQ(kiwi_res_close(o), 0);
		EXPECT_EQ(kiwi_res_close(c), 0);
	}

	EXPECT_EQ(kiwi_typo_close(custom_typo), 0);
	EXPECT_EQ(kiwi_builder_close(builder), 0);
	EXPECT_EQ(kiwi_close(typo_kw), 0);
}

TEST(KiwiC, Tokenizer)
{
	kiwi_h okw = reuse_kiwi_instance();
	kiwi_swtokenizer_h swt = kiwi_swt_init("test/written.tokenizer.json", okw);
	EXPECT_NE(swt, nullptr);
	for (auto c : {
		u8"",
		u8"한국어에 특화된 토크나이저입니다.",
		u8"감사히 먹겠습니당!",
		u8"노래진 손톱을 봤던걸요.",
		u8"제임스웹우주천체망원경",
		u8"그만해여~",
		})
	{
		int token_size = kiwi_swt_encode(swt, c, -1, nullptr, 0, nullptr, 0);
		EXPECT_GE(token_size, 0);
		int* token_ids_buf = (int*)malloc(token_size * sizeof(int));
		EXPECT_GE(kiwi_swt_encode(swt, c, -1, token_ids_buf, token_size, nullptr, 0), 0);

		int char_size = kiwi_swt_decode(swt, token_ids_buf, token_size, nullptr, 0);
		EXPECT_GE(char_size, 0);
		char* char_buf = (char*)malloc(char_size + 1);
		EXPECT_GE(kiwi_swt_decode(swt, token_ids_buf, token_size, char_buf, char_size), 0);
		char_buf[char_size] = 0;

		EXPECT_STREQ(c, char_buf);

		free(char_buf);
		free(token_ids_buf);
	}
	

	EXPECT_EQ(kiwi_swt_close(swt), 0);
}

TEST(KiwiC, Blocklist)
{
	kiwi_h okw = reuse_kiwi_instance();
	auto str = u"좋아하다.";
	{
		kiwi_res_h o = kiwi_analyze_w(okw, (const kchar16_t*)str, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, nullptr);
		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 0), std::u16string{ u"좋아하" });
		EXPECT_EQ(kiwi_res_close(o), 0);
	}

	{
		kiwi_morphset_h ms = kiwi_new_morphset(okw);
		EXPECT_GT(kiwi_morphset_add_w(ms, (const kchar16_t*)u"좋아하", nullptr), 0);
		kiwi_res_h o = kiwi_analyze_w(okw, (const kchar16_t*)str, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, ms, nullptr);
		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 0), std::u16string{ u"좋" });
		EXPECT_EQ(kiwi_res_close(o), 0);
		EXPECT_EQ(kiwi_morphset_close(ms), 0);
	}
}

TEST(KiwiC, Pretokenized)
{
	kiwi_h okw = reuse_kiwi_instance();
	auto str = u"드디어패트와 매트가 2017년에 국내 개봉했다. 패트와매트는 2016년...";
	auto u8str = u8"드디어패트와 매트가 2017년에 국내 개봉했다. 패트와매트는 2016년...";

	{
		auto pretokenized = kiwi_pt_init();
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 9, 25), 0);
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 29, 36), 1);
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 80, 87), 2);
		
		kiwi_res_h o = kiwi_analyze(okw, u8str, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, pretokenized);

		EXPECT_EQ(kiwi_res_form(o, 0, 1), std::string{ u8"패트와 매트" });
		EXPECT_EQ(kiwi_res_form(o, 0, 3), std::string{ u8"2017년" });
		EXPECT_EQ(kiwi_res_form(o, 0, 13), std::string{ u8"2016년" });

		EXPECT_EQ(kiwi_res_close(o), 0);
		EXPECT_EQ(kiwi_pt_close(pretokenized), 0);
	}

	{
		auto pretokenized = kiwi_pt_init();
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 3, 9), 0);
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 11, 16), 1);
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 34, 39), 2);

		kiwi_res_h o = kiwi_analyze_w(okw, (const kchar16_t*)str, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, pretokenized);

		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 1), std::u16string{ u"패트와 매트" });
		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 3), std::u16string{ u"2017년" });
		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 13), std::u16string{ u"2016년" });

		EXPECT_EQ(kiwi_res_close(o), 0);
		EXPECT_EQ(kiwi_pt_close(pretokenized), 0);
	}

	{
		auto pretokenized = kiwi_pt_init();
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 27, 29), 0);
		EXPECT_EQ(kiwi_pt_add_token_to_span_w(pretokenized, 0, (const kchar16_t*)u"페트", "nnb", 0, 2), 0);

		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 30, 32), 1);
		EXPECT_EQ(kiwi_pt_add_span(pretokenized, 21, 24), 2);
		EXPECT_EQ(kiwi_pt_add_token_to_span_w(pretokenized, 2, (const kchar16_t*)u"개봉하", "vv", 0, 3), 0);
		EXPECT_EQ(kiwi_pt_add_token_to_span_w(pretokenized, 2, (const kchar16_t*)u"었", "ep", 2, 3), 0);

		kiwi_res_h o = kiwi_analyze_w(okw, (const kchar16_t*)str, 1, KIWI_MATCH_ALL_WITH_NORMALIZING, nullptr, pretokenized);

		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 7), std::u16string{ u"개봉하" });
		EXPECT_STREQ(kiwi_res_tag(o, 0, 7), "VV");
		EXPECT_EQ(kiwi_res_token_info(o, 0, 7)->chr_position, 21);
		EXPECT_EQ(kiwi_res_token_info(o, 0, 7)->length, 3);
		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 8), std::u16string{ u"었" });
		EXPECT_STREQ(kiwi_res_tag(o, 0, 8), "EP");
		EXPECT_EQ(kiwi_res_token_info(o, 0, 8)->chr_position, 23);
		EXPECT_EQ(kiwi_res_token_info(o, 0, 8)->length, 1);
		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 11), std::u16string{ u"페트" });
		EXPECT_STREQ(kiwi_res_tag(o, 0, 11), "NNB");
		EXPECT_EQ((const char16_t*)kiwi_res_form_w(o, 0, 13), std::u16string{ u"매트" });
		EXPECT_STREQ(kiwi_res_tag(o, 0, 13), "NNG");

		EXPECT_EQ(kiwi_res_close(o), 0);
		EXPECT_EQ(kiwi_pt_close(pretokenized), 0);
	}
}

TEST(KiwiC, ScriptName)
{
	EXPECT_STREQ(kiwi_get_script_name(0), "Unknown");
	EXPECT_STREQ(kiwi_get_script_name(1), "Latin");
	EXPECT_STREQ(kiwi_get_script_name(30), "Hangul");
}
