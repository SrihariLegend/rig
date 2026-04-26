#ifndef PI_HARNESS_SKILLS_H
#define PI_HARNESS_SKILLS_H

#include <stdbool.h>

typedef struct {
    char *name;
    char *description;
    char *content;          // full markdown body (after frontmatter)
    char *path;             // source file path
    bool disable_model_invocation;
} Skill;

// Discover skills from multiple directory paths
// Looks for: SKILL.md files in subdirectories, and .md files directly in skill dirs
Skill *skills_discover(const char **paths, int path_count, int *out_count);

// Format skills as XML for system prompt injection
char *skills_format_xml(const Skill *skills, int count);

// Free skills array
void skills_free(Skill *skills, int count);

#endif
