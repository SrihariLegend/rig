/* test_skills.c — tests for src/harness/skills.c */
#include "test.h"
#include "harness/skills.h"
#include "util/fs.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *SKILL_DIR = "/tmp/rig_test_skills";

static void setup(void) {
    system("rm -rf /tmp/rig_test_skills");
    fs_mkdir_p(SKILL_DIR);
}

static void teardown(void) {
    system("rm -rf /tmp/rig_test_skills");
}

TEST(skills_discover_empty_dir) {
    setup();
    int count = 0;
    const char *paths[] = { SKILL_DIR };
    Skill *skills = skills_discover(paths, 1, &count);
    ASSERT_EQ(count, 0);
    skills_free(skills, count);
    teardown();
}

TEST(skills_discover_md_file) {
    setup();
    const char *content = "---\nname: test-skill\ndescription: A test\n---\nSkill body here\n";
    fs_write_file("/tmp/rig_test_skills/test-skill.md", content, strlen(content));
    int count = 0;
    const char *paths[] = { SKILL_DIR };
    Skill *skills = skills_discover(paths, 1, &count);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(skills[0].name, "test-skill");
    ASSERT_STR_EQ(skills[0].description, "A test");
    skills_free(skills, count);
    teardown();
}

TEST(skills_discover_skill_md_subdir) {
    setup();
    fs_mkdir_p("/tmp/rig_test_skills/my-skill");
    const char *content = "---\nname: my-skill\ndescription: Subdir skill\n---\nContent\n";
    fs_write_file("/tmp/rig_test_skills/my-skill/SKILL.md", content, strlen(content));
    int count = 0;
    const char *paths[] = { SKILL_DIR };
    Skill *skills = skills_discover(paths, 1, &count);
    ASSERT_EQ(count, 1);
    ASSERT_STR_EQ(skills[0].name, "my-skill");
    skills_free(skills, count);
    teardown();
}

TEST(skills_discover_disable_invocation) {
    setup();
    const char *content = "---\nname: no-invoke\ndescription: hidden\ndisable-model-invocation: true\n---\nbody\n";
    fs_write_file("/tmp/rig_test_skills/no-invoke.md", content, strlen(content));
    int count = 0;
    const char *paths[] = { SKILL_DIR };
    Skill *skills = skills_discover(paths, 1, &count);
    ASSERT_EQ(count, 1);
    ASSERT_TRUE(skills[0].disable_model_invocation);
    skills_free(skills, count);
    teardown();
}

TEST(skills_format_xml_basic) {
    Skill s = { .name = "test", .description = "A <test> skill", .disable_model_invocation = false };
    char *xml = skills_format_xml(&s, 1);
    ASSERT_NOT_NULL(xml);
    ASSERT_TRUE(strstr(xml, "<skill name=\"test\">") != NULL);
    ASSERT_TRUE(strstr(xml, "&lt;test&gt;") != NULL);
    free(xml);
}

TEST(skills_format_xml_disabled_hidden) {
    Skill s = { .name = "hidden", .description = "hidden", .disable_model_invocation = true };
    char *xml = skills_format_xml(&s, 1);
    ASSERT_NOT_NULL(xml);
    ASSERT_TRUE(strstr(xml, "hidden") == NULL || strstr(xml, "<skill") == NULL);
    free(xml);
}

TEST(skills_format_xml_empty) {
    char *xml = skills_format_xml(NULL, 0);
    ASSERT_NOT_NULL(xml);
    ASSERT_TRUE(strstr(xml, "<available_skills>") != NULL);
    free(xml);
}

TEST(skills_free_null) {
    skills_free(NULL, 0);
}

TEST(skills_discover_nonexistent_dir) {
    int count = 0;
    const char *paths[] = { "/tmp/rig_nonexistent_skills" };
    Skill *skills = skills_discover(paths, 1, &count);
    ASSERT_EQ(count, 0);
    skills_free(skills, count);
}

int main(void) {
    TEST_SUITE("Skills");
    RUN_TEST(skills_discover_empty_dir);
    RUN_TEST(skills_discover_md_file);
    RUN_TEST(skills_discover_skill_md_subdir);
    RUN_TEST(skills_discover_disable_invocation);
    RUN_TEST(skills_format_xml_basic);
    RUN_TEST(skills_format_xml_disabled_hidden);
    RUN_TEST(skills_format_xml_empty);
    RUN_TEST(skills_free_null);
    RUN_TEST(skills_discover_nonexistent_dir);

    TEST_REPORT();
}
