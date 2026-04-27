/* test_interactive.c — tests for interactive mode components */
#include "test.h"
#include "tui/linestore.h"
#include "tui/md_render.h"
#include "tui/lantern.h"
#include "tui/lantern_render.h"
#include <stdlib.h>
#include <string.h>

TEST(linestore_reflow_consistency) {
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    linestore_begin_message(ls, 1);
    const char *md = "This is a long paragraph that should wrap across lines nicely.\n\n## Heading\n\n- item one\n- item two\n";
    md_render_to_linestore(ls, md, (int)strlen(md), LINE_ASSISTANT_TEXT);
    int rows1 = ls->total_screen_rows;
    linestore_reflow(ls);
    int rows2 = ls->total_screen_rows;
    ASSERT_EQ(rows1, rows2);
    linestore_free(ls);
}

TEST(linestore_bookmark_truncate) {
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    linestore_add_user_text(ls, "hello");
    linestore_add_blank(ls);
    ASSERT_EQ(ls->count, 2);
    linestore_begin_message(ls, 1);
    linestore_append_assistant_text(ls, "response line 1\nresponse line 2\n");
    ASSERT_TRUE(ls->count > 2);
    linestore_flush_stream(ls);
    ASSERT_TRUE(ls->count > 2);
    ASSERT_EQ(ls->lines[0].type, LINE_USER_TEXT);
    linestore_free(ls);
}

TEST(lantern_renderer_create_free) {
    Lantern *l = lantern_create(NULL);
    LineStore *ls = linestore_create();
    LanternRenderer *r = lantern_renderer_create(l, ls);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->auto_scroll);
    lantern_renderer_free(r);
    linestore_free(ls);
    lantern_free(l);
}

TEST(lantern_renderer_scroll) {
    Lantern *l = lantern_create(NULL);
    LineStore *ls = linestore_create();
    linestore_set_width(ls, 80);
    for (int i = 0; i < 100; i++) linestore_add_blank(ls);
    LanternRenderer *r = lantern_renderer_create(l, ls);
    lantern_renderer_resize(r, 80, 30);
    lantern_renderer_scroll_up(r, 10);
    ASSERT_FALSE(r->auto_scroll);
    lantern_renderer_scroll_to_bottom(r);
    ASSERT_TRUE(r->auto_scroll);
    lantern_renderer_free(r);
    linestore_free(ls);
    lantern_free(l);
}

int main(void) {
    TEST_SUITE("Interactive");
    RUN_TEST(linestore_reflow_consistency);
    RUN_TEST(linestore_bookmark_truncate);
    RUN_TEST(lantern_renderer_create_free);
    RUN_TEST(lantern_renderer_scroll);

    TEST_REPORT();
}
