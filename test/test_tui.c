/* test_tui.c — tests for keys, ansi, tui core, widgets */
#include "test.h"
#include "tui/keys.h"
#include "tui/ansi.h"
#include "tui/tui.h"
#include "tui/widgets/text.h"
#include "tui/widgets/input.h"
#include "tui/widgets/box.h"
#include "tui/widgets/loader.h"
#include "tui/widgets/select_list.h"
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
    /* Use string concatenation to avoid \x1bf being parsed as single hex */
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

TEST(ansi_strip_len_basic) {
    ASSERT_EQ(ansi_strip_len("hello"), 5);
}

TEST(ansi_strip_len_with_ansi) {
    ASSERT_EQ(ansi_strip_len("\x1b[31mhello\x1b[0m"), 5);
}

TEST(ansi_strip_len_null) {
    ASSERT_EQ(ansi_strip_len(NULL), 0);
}

TEST(ansi_strip_basic) {
    char *s = ansi_strip("\x1b[1;31mhello\x1b[0m");
    ASSERT_STR_EQ(s, "hello");
    free(s);
}

TEST(ansi_strip_null) {
    ASSERT_NULL(ansi_strip(NULL));
}

TEST(unicode_char_width_ascii) {
    ASSERT_EQ(unicode_char_width('A'), 1);
}

TEST(unicode_char_width_cjk) {
    ASSERT_EQ(unicode_char_width(0x4E2D), 2); /* CJK char */
}

TEST(unicode_char_width_null) {
    ASSERT_EQ(unicode_char_width(0), 0);
}

TEST(unicode_display_width_ascii) {
    ASSERT_EQ(unicode_display_width("hello"), 5);
}

TEST(unicode_display_width_with_ansi) {
    ASSERT_EQ(unicode_display_width("\x1b[31mhi\x1b[0m"), 2);
}

TEST(unicode_display_width_null) {
    ASSERT_EQ(unicode_display_width(NULL), 0);
}

/* UNICODE: emoji display width */
TEST(unicode_width_emoji) {
    /* U+1F600 = 😀, encoded as F0 9F 98 80 */
    ASSERT_EQ(unicode_char_width(0x1F600), 2);
    ASSERT_EQ(unicode_display_width("\xF0\x9F\x98\x80"), 2);
}

/* UNICODE: ZWJ sequence doesn't crash */
TEST(unicode_width_zwj_sequence) {
    /* 👨‍👩‍👧 = U+1F468 U+200D U+1F469 U+200D U+1F467 */
    const char *zwj = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
    int w = unicode_display_width(zwj);
    ASSERT_TRUE(w >= 0); /* doesn't crash, returns some value */
}

/* UNICODE: CJK characters width */
TEST(unicode_width_cjk) {
    /* 中文 = U+4E2D U+6587, each width 2 */
    ASSERT_EQ(unicode_display_width("\xE4\xB8\xAD\xE6\x96\x87"), 4);
}

/* UNICODE: Korean characters width */
TEST(unicode_width_korean) {
    /* 한글 = U+D55C U+AE00, each width 2 */
    ASSERT_EQ(unicode_display_width("\xED\x95\x9C\xEA\xB8\x80"), 4);
}

/* UNICODE: mixed ASCII + CJK */
TEST(unicode_width_mixed_ascii_cjk) {
    /* "hello" (5) + 中文 (4) = 9 */
    ASSERT_EQ(unicode_display_width("hello\xE4\xB8\xAD\xE6\x96\x87"), 9);
}

/* UNICODE: control characters width 0 */
TEST(unicode_width_control_chars) {
    ASSERT_EQ(unicode_char_width(0x01), 0);
    ASSERT_EQ(unicode_char_width(0x1F), 0);
    ASSERT_EQ(unicode_char_width(0x7F), 0); /* DEL */
    /* String with control chars embedded */
    ASSERT_EQ(unicode_display_width("\x01\x02\x03"), 0);
}

/* UNICODE: combining marks don't crash */
TEST(unicode_width_combining_marks) {
    /* U+0300 COMBINING GRAVE ACCENT = CC 80 in UTF-8 */
    /* "a" + combining mark */
    const char *s = "a\xCC\x80";
    int w = unicode_display_width(s);
    ASSERT_TRUE(w >= 0); /* doesn't crash */
}

/* UNICODE: RTL characters don't crash */
TEST(unicode_width_rtl) {
    /* U+0627 ARABIC LETTER ALEF = D8 A7 */
    /* U+0628 ARABIC LETTER BA = D8 A8 */
    const char *s = "\xD8\xA7\xD8\xA8";
    int w = unicode_display_width(s);
    ASSERT_TRUE(w >= 0); /* doesn't crash */
}

/* UNICODE: invalid UTF-8 bytes */
TEST(unicode_width_invalid_utf8) {
    /* 0xFF alone */
    const char s1[] = {(char)0xFF, '\0'};
    int w1 = unicode_display_width(s1);
    ASSERT_TRUE(w1 >= 0); /* doesn't crash */

    /* 0xFE alone */
    const char s2[] = {(char)0xFE, '\0'};
    int w2 = unicode_display_width(s2);
    ASSERT_TRUE(w2 >= 0);

    /* 0x80 alone (continuation byte without lead) */
    const char s3[] = {(char)0x80, '\0'};
    int w3 = unicode_display_width(s3);
    ASSERT_TRUE(w3 >= 0);
}

/* UNICODE: overlong UTF-8 encoding */
TEST(unicode_width_overlong_utf8) {
    /* Overlong encoding of '/' (U+002F): C0 AF */
    const char s[] = {(char)0xC0, (char)0xAF, '\0'};
    int w = unicode_display_width(s);
    ASSERT_TRUE(w >= 0); /* doesn't crash */
}

/* UNICODE: 4-byte UTF-8 (emoji) in ansi_strip */
TEST(ansi_strip_with_emoji) {
    /* ANSI color + emoji + ANSI reset */
    char *stripped = ansi_strip("\x1b[31m\xF0\x9F\x98\x80\x1b[0m");
    ASSERT_NOT_NULL(stripped);
    ASSERT_STR_EQ(stripped, "\xF0\x9F\x98\x80");
    free(stripped);
}

/* UNICODE: empty string width 0 */
TEST(unicode_width_empty_string) {
    ASSERT_EQ(unicode_display_width(""), 0);
}

/* UNICODE: string of only ANSI escapes width 0 */
TEST(unicode_width_only_ansi) {
    ASSERT_EQ(unicode_display_width("\x1b[31m\x1b[1m\x1b[0m"), 0);
}

/* ========== TUI Core ========== */

TEST(tui_create_free) {
    TUI *t = tui_create();
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(t->component_count, 0);
    ASSERT_TRUE(t->dirty);
    tui_free(t);
}

TEST(tui_add_remove_component) {
    TUI *t = tui_create();
    Component *c = widget_text_create("test");
    tui_add_component(t, c);
    ASSERT_EQ(t->component_count, 1);
    tui_remove_component(t, c);
    ASSERT_EQ(t->component_count, 0);
    component_free(c);
    tui_free(t);
}

TEST(tui_invalidate) {
    TUI *t = tui_create();
    t->dirty = false;
    tui_invalidate(t);
    ASSERT_TRUE(t->dirty);
    tui_free(t);
}

TEST(tui_quit) {
    TUI *t = tui_create();
    t->running = true;
    tui_quit(t);
    ASSERT_FALSE(t->running);
    tui_free(t);
}

/* ========== Widget: Text ========== */

TEST(widget_text_create_basic) {
    Component *c = widget_text_create("hello");
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c->render);
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_EQ(lines, 1);
    ASSERT_STR_EQ(out[0], "hello");
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
}

TEST(widget_text_set) {
    Component *c = widget_text_create("old");
    widget_text_set(c, "new");
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_STR_EQ(out[0], "new");
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
}

TEST(widget_text_null) {
    Component *c = widget_text_create(NULL);
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_EQ(lines, 0);
    free(out);
    component_free(c);
}

/* ========== Widget: Input ========== */

TEST(widget_input_create) {
    Component *c = widget_input_create("Type here...");
    ASSERT_NOT_NULL(c);
    ASSERT_TRUE(c->focused);
    ASSERT_STR_EQ(widget_input_get_text(c), "");
    component_free(c);
}

TEST(widget_input_set_get_text) {
    Component *c = widget_input_create(NULL);
    widget_input_set_text(c, "hello");
    ASSERT_STR_EQ(widget_input_get_text(c), "hello");
    component_free(c);
}

TEST(widget_input_clear) {
    Component *c = widget_input_create(NULL);
    widget_input_set_text(c, "hello");
    widget_input_clear(c);
    ASSERT_STR_EQ(widget_input_get_text(c), "");
    component_free(c);
}

TEST(widget_input_typing) {
    Component *c = widget_input_create(NULL);
    c->handle_input(c, "h", 1);
    c->handle_input(c, "i", 1);
    ASSERT_STR_EQ(widget_input_get_text(c), "hi");
    component_free(c);
}

/* ========== Widget: Box ========== */

TEST(widget_box_create) {
    Component *child = widget_text_create("inner");
    Component *box = widget_box_create(child, 1);
    ASSERT_NOT_NULL(box);
    int lines = 0;
    char **out = box->render(box, 80, &lines);
    ASSERT_TRUE(lines >= 3);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(child);
    component_free(box);
}

/* ========== Widget: Loader ========== */

TEST(widget_loader_create) {
    Component *c = widget_loader_create("Loading...");
    ASSERT_NOT_NULL(c);
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_EQ(lines, 1);
    ASSERT_TRUE(strstr(out[0], "Loading...") != NULL);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
}

TEST(widget_loader_tick) {
    Component *c = widget_loader_create("msg");
    widget_loader_tick(c);
    widget_loader_tick(c);
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_EQ(lines, 1);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
}

/* ========== Widget: Select List ========== */

TEST(widget_select_list_create) {
    SelectItem items[] = {
        { .label = "Option A", .value = "a", .description = "First" },
        { .label = "Option B", .value = "b", .description = "Second" },
    };
    Component *c = widget_select_list_create(items, 2);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(widget_select_list_get_selected(c), 0);
    ASSERT_STR_EQ(widget_select_list_get_value(c), "a");
    component_free(c);
}

TEST(widget_select_list_navigate) {
    SelectItem items[] = {
        { .label = "A", .value = "a" },
        { .label = "B", .value = "b" },
        { .label = "C", .value = "c" },
    };
    Component *c = widget_select_list_create(items, 3);
    /* Navigate down */
    c->handle_input(c, "\x1b[B", 3); /* down arrow */
    ASSERT_EQ(widget_select_list_get_selected(c), 1);
    ASSERT_STR_EQ(widget_select_list_get_value(c), "b");
    c->handle_input(c, "\x1b[B", 3);
    ASSERT_EQ(widget_select_list_get_selected(c), 2);
    /* Navigate up */
    c->handle_input(c, "\x1b[A", 3);
    ASSERT_EQ(widget_select_list_get_selected(c), 1);
    component_free(c);
}

TEST(widget_select_list_render) {
    SelectItem items[] = {
        { .label = "A", .value = "a" },
    };
    Component *c = widget_select_list_create(items, 1);
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_EQ(lines, 1);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
}

/* RESOURCE EXHAUSTION: text widget with 10,000 line text */
TEST(widget_text_10k_lines) {
    Str big = str_new(256);
    for (int i = 0; i < 10000; i++) {
        str_appendf(&big, "line %d\n", i);
    }
    Component *c = widget_text_create(big.data);
    ASSERT_NOT_NULL(c);
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_TRUE(lines > 0);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
    str_free(&big);
}

/* RESOURCE EXHAUSTION: input widget with 10,000 character string */
TEST(widget_input_10k_chars) {
    Component *c = widget_input_create(NULL);
    Str big = str_new(10001);
    for (int i = 0; i < 10000; i++) {
        str_append_char(&big, 'x');
    }
    widget_input_set_text(c, big.data);
    ASSERT_TRUE(strlen(widget_input_get_text(c)) > 0);
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_TRUE(lines >= 1);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
    str_free(&big);
}

/* RESOURCE EXHAUSTION: select list with 1,000 items */
TEST(widget_select_list_1000_items) {
    int n = 1000;
    SelectItem *items = malloc(sizeof(SelectItem) * n);
    char **labels = malloc(sizeof(char *) * n);
    char **values = malloc(sizeof(char *) * n);
    for (int i = 0; i < n; i++) {
        labels[i] = malloc(16);
        values[i] = malloc(16);
        snprintf(labels[i], 16, "Item %d", i);
        snprintf(values[i], 16, "v%d", i);
        items[i] = (SelectItem){ .label = labels[i], .value = values[i], .description = NULL };
    }
    Component *c = widget_select_list_create(items, n);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(widget_select_list_get_selected(c), 0);
    ASSERT_STR_EQ(widget_select_list_get_value(c), "v0");
    int lines = 0;
    char **out = c->render(c, 80, &lines);
    ASSERT_TRUE(lines > 0);
    for (int i = 0; i < lines; i++) free(out[i]);
    free(out);
    component_free(c);
    for (int i = 0; i < n; i++) { free(labels[i]); free(values[i]); }
    free(labels);
    free(values);
    free(items);
}

/* ========== ADVERSARIAL: Key Parser Fuzzing ========== */

TEST(adv_key_zero_length) {
    ParsedKey k = key_parse("", 0);
    /* Must not crash; should return unknown */
    ASSERT_STR_EQ(k.id, "unknown");
}

TEST(adv_key_single_esc) {
    ParsedKey k = key_parse("\x1b", 1);
    /* Single ESC byte, must not crash */
    ASSERT_TRUE(strlen(k.id) > 0);
}

TEST(adv_key_very_long_escape_sequence) {
    /* Build a 100+ byte escape sequence */
    char buf[120];
    buf[0] = '\x1b';
    buf[1] = '[';
    for (int i = 2; i < 110; i++) buf[i] = '0' + (i % 10);
    buf[110] = '~';
    buf[111] = '\0';
    ParsedKey k = key_parse(buf, 111);
    /* Must not crash */
    ASSERT_TRUE(strlen(k.id) > 0);
}

TEST(adv_key_random_binary) {
    /* Feed all byte values 0x01-0xFF (skip 0x00 as it's string terminator) */
    for (int b = 1; b < 256; b++) {
        char c = (char)b;
        ParsedKey k = key_parse(&c, 1);
        /* Must not crash for any byte */
        ASSERT_TRUE(strlen(k.id) > 0);
        (void)k;
    }
}

TEST(adv_key_incomplete_csi) {
    /* CSI without final byte: ESC [ 1 ; (no terminator) */
    ParsedKey k = key_parse("\x1b[1;", 4);
    /* Must not crash */
    ASSERT_TRUE(strlen(k.id) > 0);
}

TEST(adv_key_malformed_kitty) {
    /* Kitty protocol uses CSI with extra parameters */
    /* ESC [ code ; modifier ; event_type u */
    ParsedKey k = key_parse("\x1b[97;1;1u", 9);
    /* Must not crash */
    ASSERT_TRUE(strlen(k.id) > 0);
}

TEST(adv_key_malformed_kitty_release) {
    /* Kitty release event: ESC [ code ; modifier ; 3 u */
    ParsedKey k = key_parse("\x1b[97;1;3u", 9);
    /* Must not crash */
    ASSERT_TRUE(strlen(k.id) > 0);
}

TEST(adv_key_paste_without_end) {
    /* Paste start without end marker */
    ParsedKey k = key_parse("\x1b[200~some text without end", 27);
    /* Must not crash */
    ASSERT_TRUE(strlen(k.id) > 0);
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

    TEST_SUITE("UNICODE: TUI Edge Cases");
    RUN_TEST(unicode_width_emoji);
    RUN_TEST(unicode_width_zwj_sequence);
    RUN_TEST(unicode_width_cjk);
    RUN_TEST(unicode_width_korean);
    RUN_TEST(unicode_width_mixed_ascii_cjk);
    RUN_TEST(unicode_width_control_chars);
    RUN_TEST(unicode_width_combining_marks);
    RUN_TEST(unicode_width_rtl);
    RUN_TEST(unicode_width_invalid_utf8);
    RUN_TEST(unicode_width_overlong_utf8);
    RUN_TEST(ansi_strip_with_emoji);
    RUN_TEST(unicode_width_empty_string);
    RUN_TEST(unicode_width_only_ansi);

    TEST_SUITE("TUI Core");
    RUN_TEST(tui_create_free);
    RUN_TEST(tui_add_remove_component);
    RUN_TEST(tui_invalidate);
    RUN_TEST(tui_quit);

    TEST_SUITE("Widget: Text");
    RUN_TEST(widget_text_create_basic);
    RUN_TEST(widget_text_set);
    RUN_TEST(widget_text_null);

    TEST_SUITE("Widget: Input");
    RUN_TEST(widget_input_create);
    RUN_TEST(widget_input_set_get_text);
    RUN_TEST(widget_input_clear);
    RUN_TEST(widget_input_typing);

    TEST_SUITE("Widget: Box");
    RUN_TEST(widget_box_create);

    TEST_SUITE("Widget: Loader");
    RUN_TEST(widget_loader_create);
    RUN_TEST(widget_loader_tick);

    TEST_SUITE("Widget: Select List");
    RUN_TEST(widget_select_list_create);
    RUN_TEST(widget_select_list_navigate);
    RUN_TEST(widget_select_list_render);

    TEST_SUITE("RESOURCE EXHAUSTION: TUI");
    RUN_TEST(widget_text_10k_lines);
    RUN_TEST(widget_input_10k_chars);
    RUN_TEST(widget_select_list_1000_items);

    TEST_SUITE("ADVERSARIAL: Key Parser Fuzzing");
    RUN_TEST(adv_key_zero_length);
    RUN_TEST(adv_key_single_esc);
    RUN_TEST(adv_key_very_long_escape_sequence);
    RUN_TEST(adv_key_random_binary);
    RUN_TEST(adv_key_incomplete_csi);
    RUN_TEST(adv_key_malformed_kitty);
    RUN_TEST(adv_key_malformed_kitty_release);
    RUN_TEST(adv_key_paste_without_end);

    TEST_REPORT();
}
