/* test_tui.c — tests for keys, ansi, linestore, lantern */
#include "test.h"
#include "tui/keys.h"
#include "tui/ansi.h"
#include "tui/linestore.h"
#include "tui/lantern.h"
#include "tui/md_render.h"
#include "util/str.h"
#include <stdlib.h>
#include <string.h>

/* ========== Keys ========== */

TEST(key_parse_letter) {
    ParsedKey k = key_parse("a", 1);
    ASSERT_STR_EQ(k.id, "a");
    ASSERT_STR_EQ(k.printable, "a");
    ASSERT_FALSE(k.is_release);
}

TEST(key_parse_enter) {
    ParsedKey k = key_parse("\r", 1);
    ASSERT_STR_EQ(k.id, "enter");
}

TEST(key_parse_tab) {
    ParsedKey k = key_parse("\t", 1);
    ASSERT_STR_EQ(k.id, "tab");
}

TEST(key_parse_backspace) {
    ParsedKey k = key_parse("\x7f", 1);
    ASSERT_STR_EQ(k.id, "backspace");
}

TEST(key_parse_ctrl_c) {
    ParsedKey k = key_parse("\x03", 1);
    ASSERT_STR_EQ(k.id, "ctrl+c");
}

TEST(key_parse_ctrl_a) {
    ParsedKey k = key_parse("\x01", 1);
    ASSERT_STR_EQ(k.id, "ctrl+a");
}

TEST(key_parse_arrow_up) {
    ParsedKey k = key_parse("\x1b[A", 3);
    ASSERT_STR_EQ(k.id, "up");
}

TEST(key_parse_arrow_down) {
    ParsedKey k = key_parse("\x1b[B", 3);
    ASSERT_STR_EQ(k.id, "down");
}

TEST(key_parse_arrow_right) {
    ParsedKey k = key_parse("\x1b[C", 3);
    ASSERT_STR_EQ(k.id, "right");
}

TEST(key_parse_arrow_left) {
    ParsedKey k = key_parse("\x1b[D", 3);
    ASSERT_STR_EQ(k.id, "left");
}

TEST(key_parse_home) {
    ParsedKey k = key_parse("\x1b[H", 3);
    ASSERT_STR_EQ(k.id, "home");
}

TEST(key_parse_end) {
    ParsedKey k = key_parse("\x1b[F", 3);
    ASSERT_STR_EQ(k.id, "end");
}

TEST(key_parse_delete) {
    ParsedKey k = key_parse("\x1b[3~", 4);
    ASSERT_STR_EQ(k.id, "delete");
}

TEST(key_parse_pageup) {
    ParsedKey k = key_parse("\x1b[5~", 4);
    ASSERT_STR_EQ(k.id, "pageup");
}

TEST(key_parse_pagedown) {
    ParsedKey k = key_parse("\x1b[6~", 4);
    ASSERT_STR_EQ(k.id, "pagedown");
}

TEST(key_parse_shift_tab) {
    ParsedKey k = key_parse("\x1b[Z", 3);
    ASSERT_STR_EQ(k.id, "shift+tab");
}

TEST(key_parse_alt_key) {
    ParsedKey k = key_parse("\x1b" "f", 2);
    ASSERT_STR_EQ(k.id, "alt+f");
}

TEST(key_parse_null_input) {
    ParsedKey k = key_parse(NULL, 0);
    ASSERT_STR_EQ(k.id, "unknown");
}

TEST(key_parse_paste) {
    const char *paste = "\x1b[200~hello\x1b[201~";
    ParsedKey k = key_parse(paste, (int)strlen(paste));
    ASSERT_TRUE(k.is_paste);
    ASSERT_STR_EQ(k.id, "paste");
    ASSERT_NOT_NULL(k.paste_data);
    ASSERT_STR_EQ(k.paste_data, "hello");
    free(k.paste_data);
}

TEST(key_matches_true) {
    ParsedKey k = key_parse("a", 1);
    ASSERT_TRUE(key_matches(&k, "a"));
}

TEST(key_matches_false) {
    ParsedKey k = key_parse("a", 1);
    ASSERT_FALSE(key_matches(&k, "b"));
}

TEST(key_matches_null) {
    ASSERT_FALSE(key_matches(NULL, "a"));
}

/* ========== ANSI ========== */

TEST(ansi_state_reset_basic) {
    AnsiState s = { .bold = true, .fg_color = 5 };
    ansi_state_reset(&s);
    ASSERT_FALSE(s.bold);
    ASSERT_EQ(s.fg_color, -1);
    ASSERT_EQ(s.bg_color, -1);
}

TEST(ansi_track_bold) {
    AnsiState s;
    ansi_state_reset(&s);
    ansi_track(&s, "\x1b[1m");
    ASSERT_TRUE(s.bold);
}

TEST(ansi_track_fg_color) {
    AnsiState s;
    ansi_state_reset(&s);
    ansi_track(&s, "\x1b[32m");
    ASSERT_EQ(s.fg_color, 2);
}

TEST(ansi_track_reset) {
    AnsiState s;
    ansi_state_reset(&s);
    ansi_track(&s, "\x1b[1m");
    ansi_track(&s, "\x1b[0m");
    ASSERT_FALSE(s.bold);
}

TEST(ansi_strip_len_basic) { ASSERT_EQ(ansi_strip_len("hello"), 5); }
TEST(ansi_strip_len_with_ansi) { ASSERT_EQ(ansi_strip_len("\x1b[31mhello\x1b[0m"), 5); }
TEST(ansi_strip_len_null) { ASSERT_EQ(ansi_strip_len(NULL), 0); }

TEST(ansi_strip_basic) {
    char *s = ansi_strip("\x1b[1;31mhello\x1b[0m");
    ASSERT_STR_EQ(s, "hello");
    free(s);
}

TEST(ansi_strip_null) { ASSERT_NULL(ansi_strip(NULL)); }

TEST(unicode_char_width_ascii) { ASSERT_EQ(unicode_char_width('A'), 1); }
TEST(unicode_char_width_cjk) { ASSERT_EQ(unicode_char_width(0x4E2D), 2); }
TEST(unicode_char_width_null) { ASSERT_EQ(unicode_char_width(0), 0); }
TEST(unicode_display_width_ascii) { ASSERT_EQ(unicode_display_width("hello"), 5); }
TEST(unicode_display_width_with_ansi) { ASSERT_EQ(unicode_display_width("\x1b[31mhi\x1b[0m"), 2); }
TEST(unicode_display_width_null) { ASSERT_EQ(unicode_display_width(NULL), 0); }

TEST(unicode_width_emoji) {
    ASSERT_EQ(unicode_char_width(0x1F600), 2);
    ASSERT_EQ(unicode_display_width("\xF0\x9F\x98\x80"), 2);
}

TEST(unicode_width_cjk) { ASSERT_EQ(unicode_display_width("\xE4\xB8\xAD\xE6\x96\x87"), 4); }
TEST(unicode_width_empty_string) { ASSERT_EQ(unicode_display_width(""), 0); }
TEST(unicode_width_only_ansi) { ASSERT_EQ(unicode_display_width("\x1b[31m\x1b[1m\x1b[0m"), 0); }

/* ========== LineStore ========== */

TEST(linestore_create_free) {
    LineStore *ls = linestore_create();
    ASSERT_NOT_NULL(ls);
    ASSERT_EQ(ls->count, 0);
    linestore_free(ls);
}

TEST(linestore_add_blank) {
    LineStore *ls = linestore_create();
    linestore_add_blank(ls);
    ASSERT_EQ(ls->count, 1);
    ASSERT_EQ(ls->lines[0].type, LINE_BLANK);
    linestore_free(ls);
}

TEST(linestore_add_user_text) {
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    linestore_add_user_text(ls, "hello world");
    ASSERT_EQ(ls->count, 1);
    ASSERT_EQ(ls->lines[0].type, LINE_USER_TEXT);
    ASSERT_STR_EQ(ls->lines[0].raw_text, "hello world");
    linestore_free(ls);
}

TEST(linestore_md4c_heading) {
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    linestore_begin_message(ls, 1);
    md_render_to_linestore(ls, "# Hello\n", 9, LINE_ASSISTANT_TEXT);
    ASSERT_TRUE(ls->count >= 1);
    ASSERT_EQ(ls->lines[0].type, LINE_HEADING);
    ASSERT_STR_EQ(ls->lines[0].raw_text, "Hello");
    linestore_free(ls);
}

TEST(linestore_md4c_bold_spans) {
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    linestore_begin_message(ls, 1);
    md_render_to_linestore(ls, "This is **bold** text.\n", 22, LINE_ASSISTANT_TEXT);
    ASSERT_TRUE(ls->count >= 1);
    ASSERT_EQ(ls->lines[0].type, LINE_ASSISTANT_TEXT);
    ASSERT_TRUE(ls->lines[0].span_count >= 3);
    ASSERT_EQ(ls->lines[0].spans[1].flags & SPAN_BOLD, SPAN_BOLD);
    linestore_free(ls);
}

TEST(linestore_md4c_table) {
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    linestore_begin_message(ls, 1);
    const char *md = "| A | B |\n|---|---|\n| 1 | 2 |\n";
    md_render_to_linestore(ls, md, (int)strlen(md), LINE_ASSISTANT_TEXT);
    int table_rows = 0;
    for (int i = 0; i < ls->count; i++) {
        if (ls->lines[i].type == LINE_TABLE_ROW) table_rows++;
    }
    ASSERT_TRUE(table_rows >= 2);
    linestore_free(ls);
}

TEST(linestore_stream_and_flush) {
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    linestore_begin_message(ls, 1);
    linestore_append_assistant_text(ls, "hello ");
    linestore_append_assistant_text(ls, "world\n");
    ASSERT_TRUE(ls->count >= 1);
    linestore_flush_stream(ls);
    ASSERT_TRUE(ls->count >= 1);
    ASSERT_EQ(ls->lines[0].type, LINE_ASSISTANT_TEXT);
    linestore_free(ls);
}

/* ========== Lantern ========== */

TEST(lantern_create_defaults) {
    Lantern *l = lantern_create(NULL);
    ASSERT_NOT_NULL(l);
    ASSERT_EQ(l->config.accent.r, 212);
    lantern_free(l);
}

TEST(lantern_lut_rebuild) {
    Lantern *l = lantern_create(NULL);
    lantern_rebuild_lut(l, 30);
    ASSERT_EQ(l->lut_size, 30);
    RGB center = lantern_fade_color(l, 0);
    RGB edge = lantern_fade_color(l, 29);
    ASSERT_TRUE(center.r > edge.r);
    lantern_free(l);
}

int main(void) {
    TEST_SUITE("Keys");
    RUN_TEST(key_parse_letter);
    RUN_TEST(key_parse_enter);
    RUN_TEST(key_parse_tab);
    RUN_TEST(key_parse_backspace);
    RUN_TEST(key_parse_ctrl_c);
    RUN_TEST(key_parse_ctrl_a);
    RUN_TEST(key_parse_arrow_up);
    RUN_TEST(key_parse_arrow_down);
    RUN_TEST(key_parse_arrow_right);
    RUN_TEST(key_parse_arrow_left);
    RUN_TEST(key_parse_home);
    RUN_TEST(key_parse_end);
    RUN_TEST(key_parse_delete);
    RUN_TEST(key_parse_pageup);
    RUN_TEST(key_parse_pagedown);
    RUN_TEST(key_parse_shift_tab);
    RUN_TEST(key_parse_alt_key);
    RUN_TEST(key_parse_null_input);
    RUN_TEST(key_parse_paste);
    RUN_TEST(key_matches_true);
    RUN_TEST(key_matches_false);
    RUN_TEST(key_matches_null);

    TEST_SUITE("ANSI");
    RUN_TEST(ansi_state_reset_basic);
    RUN_TEST(ansi_track_bold);
    RUN_TEST(ansi_track_fg_color);
    RUN_TEST(ansi_track_reset);
    RUN_TEST(ansi_strip_len_basic);
    RUN_TEST(ansi_strip_len_with_ansi);
    RUN_TEST(ansi_strip_len_null);
    RUN_TEST(ansi_strip_basic);
    RUN_TEST(ansi_strip_null);
    RUN_TEST(unicode_char_width_ascii);
    RUN_TEST(unicode_char_width_cjk);
    RUN_TEST(unicode_char_width_null);
    RUN_TEST(unicode_display_width_ascii);
    RUN_TEST(unicode_display_width_with_ansi);
    RUN_TEST(unicode_display_width_null);
    RUN_TEST(unicode_width_emoji);
    RUN_TEST(unicode_width_cjk);
    RUN_TEST(unicode_width_empty_string);
    RUN_TEST(unicode_width_only_ansi);

    TEST_SUITE("LineStore");
    RUN_TEST(linestore_create_free);
    RUN_TEST(linestore_add_blank);
    RUN_TEST(linestore_add_user_text);
    RUN_TEST(linestore_md4c_heading);
    RUN_TEST(linestore_md4c_bold_spans);
    RUN_TEST(linestore_md4c_table);
    RUN_TEST(linestore_stream_and_flush);

    TEST_SUITE("Lantern");
    RUN_TEST(lantern_create_defaults);
    RUN_TEST(lantern_lut_rebuild);

    TEST_REPORT();
}
