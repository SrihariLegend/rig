#include "skills.h"
#include "util/fs.h"
#include "util/str.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>

// Validate skill name: 1-64 chars, lowercase a-z, 0-9, hyphens only
static bool validate_skill_name(const char *name) {
    if (!name) return false;
    size_t len = strlen(name);
    if (len == 0 || len > 64) return false;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!(islower(c) || isdigit(c) || c == '-')) {
            return false;
        }
    }
    return true;
}

// Parse frontmatter and extract metadata
// Returns pointer to content after frontmatter (or start if no frontmatter)
static const char *parse_frontmatter(const char *content, char **out_name,
                                      char **out_desc, bool *out_disable) {
    *out_name = NULL;
    *out_desc = NULL;
    *out_disable = false;

    // Check for frontmatter start
    if (strncmp(content, "---\n", 4) != 0 && strncmp(content, "---\r\n", 5) != 0) {
        return content;
    }

    const char *fm_start = content + (strncmp(content, "---\n", 4) == 0 ? 4 : 5);
    const char *fm_end = strstr(fm_start, "\n---\n");
    if (!fm_end) {
        fm_end = strstr(fm_start, "\n---\r\n");
        if (!fm_end) return content;
    }

    // Parse frontmatter line by line
    const char *line_start = fm_start;
    while (line_start < fm_end) {
        const char *line_end = strchr(line_start, '\n');
        if (!line_end || line_end > fm_end) line_end = fm_end;

        // Extract line
        size_t line_len = line_end - line_start;
        char line[1024];
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        // Parse key: value
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *key = line;
            char *value = colon + 1;

            // Trim whitespace from key and value
            while (*key && isspace(*key)) key++;
            char *key_end = key + strlen(key) - 1;
            while (key_end > key && isspace(*key_end)) *key_end-- = '\0';

            while (*value && isspace(*value)) value++;
            char *value_end = value + strlen(value) - 1;
            while (value_end > value && isspace(*value_end)) *value_end-- = '\0';

            if (strcmp(key, "name") == 0) {
                free(*out_name);
                *out_name = strdup(value);
            } else if (strcmp(key, "description") == 0) {
                free(*out_desc);
                *out_desc = strdup(value);
            } else if (strcmp(key, "disable-model-invocation") == 0) {
                *out_disable = (strcmp(value, "true") == 0);
            }
        }

        line_start = line_end + 1;
    }

    // Skip past the closing ---
    const char *body = fm_end;
    while (*body && (*body == '\n' || *body == '\r' || *body == '-')) body++;
    return body;
}

// Extract first line as description (fallback for .md files without frontmatter)
static char *extract_first_line(const char *content) {
    if (!content) return strdup("");

    const char *line_end = strchr(content, '\n');
    if (!line_end) return strdup(content);

    size_t len = line_end - content;
    char *line = malloc(len + 1);
    memcpy(line, content, len);
    line[len] = '\0';

    // Trim leading # and whitespace for markdown headers
    char *start = line;
    while (*start && (*start == '#' || isspace(*start))) start++;

    char *result = strdup(start);
    free(line);
    return result;
}

// Process a single skill file
static Skill *process_skill_file(const char *path, const char *filename, int *count) {
    *count = 0;

    size_t content_len;
    char *content = fs_read_file(path, &content_len);
    if (!content) return NULL;

    Skill *skill = calloc(1, sizeof(Skill));
    skill->path = strdup(path);

    // Check if this is a SKILL.md or regular .md file
    bool is_skill_md = (strcmp(filename, "SKILL.md") == 0);

    char *fm_name = NULL;
    char *fm_desc = NULL;
    bool fm_disable = false;
    const char *body = parse_frontmatter(content, &fm_name, &fm_desc, &fm_disable);

    // Determine name
    if (fm_name && validate_skill_name(fm_name)) {
        skill->name = fm_name;
    } else if (is_skill_md) {
        // For SKILL.md, use parent directory name
        char *dir_copy = strdup(path);
        char *last_slash = strrchr(dir_copy, '/');
        if (last_slash) *last_slash = '\0';
        last_slash = strrchr(dir_copy, '/');
        if (last_slash) {
            skill->name = strdup(last_slash + 1);
        }
        free(dir_copy);
        free(fm_name);
    } else {
        // For .md files, use filename without extension
        char *name = strdup(filename);
        char *ext = strrchr(name, '.');
        if (ext) *ext = '\0';
        skill->name = name;
        free(fm_name);
    }

    // Validate name
    if (!skill->name || !validate_skill_name(skill->name)) {
        free(skill->name);
        free(skill->path);
        free(skill);
        free(fm_desc);
        free(content);
        return NULL;
    }

    // Set description
    if (fm_desc && strlen(fm_desc) > 0) {
        skill->description = fm_desc;
    } else {
        skill->description = extract_first_line(body);
        free(fm_desc);
    }

    skill->content = strdup(body);
    skill->disable_model_invocation = fm_disable;

    free(content);
    *count = 1;
    return skill;
}

// Directory scanning context
typedef struct {
    Skill *skills;
    int count;
    int capacity;
} SkillList;

static void add_skill(SkillList *list, Skill *skill) {
    // Check for duplicate names (first wins)
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->skills[i].name, skill->name) == 0) {
            // Duplicate, free the new one
            free(skill->name);
            free(skill->description);
            free(skill->content);
            free(skill->path);
            free(skill);
            return;
        }
    }

    // Grow array if needed
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->skills = realloc(list->skills, list->capacity * sizeof(Skill));
    }

    list->skills[list->count++] = *skill;
    free(skill);
}

static void scan_dir_callback(const char *dir, const char *name, bool is_dir, void *ctx) {
    SkillList *list = (SkillList *)ctx;

    if (!is_dir) {
        // Check for .md files in the directory
        if (strlen(name) > 3 && strcmp(name + strlen(name) - 3, ".md") == 0) {
            char *full_path = fs_join(dir, name);
            int count;
            Skill *skill = process_skill_file(full_path, name, &count);
            free(full_path);

            if (skill && count > 0) {
                add_skill(list, skill);
            }
        }
    } else {
        // Check for SKILL.md in subdirectories
        char *subdir = fs_join(dir, name);
        char *skill_file = fs_join(subdir, "SKILL.md");

        if (fs_exists(skill_file)) {
            int count;
            Skill *skill = process_skill_file(skill_file, "SKILL.md", &count);
            if (skill && count > 0) {
                add_skill(list, skill);
            }
        }

        free(skill_file);
        free(subdir);
    }
}

Skill *skills_discover(const char **paths, int path_count, int *out_count) {
    SkillList list = {0};

    for (int i = 0; i < path_count; i++) {
        const char *path = paths[i];
        if (!fs_exists(path)) continue;
        if (!fs_is_dir(path)) continue;

        fs_readdir(path, scan_dir_callback, &list);
    }

    *out_count = list.count;
    return list.skills;
}

char *skills_format_xml(const Skill *skills, int count) {
    Str xml = str_new(256);
    str_append(&xml, "<available_skills>\n");

    for (int i = 0; i < count; i++) {
        if (skills[i].disable_model_invocation) continue;

        str_append(&xml, "<skill name=\"");
        str_append(&xml, skills[i].name);
        str_append(&xml, "\">");

        // Escape XML special characters in description
        const char *desc = skills[i].description;
        for (const char *p = desc; *p; p++) {
            switch (*p) {
                case '&':  str_append(&xml, "&amp;"); break;
                case '<':  str_append(&xml, "&lt;"); break;
                case '>':  str_append(&xml, "&gt;"); break;
                case '"':  str_append(&xml, "&quot;"); break;
                case '\'': str_append(&xml, "&apos;"); break;
                default:   str_append_char(&xml, *p); break;
            }
        }

        str_append(&xml, "</skill>\n");
    }

    str_append(&xml, "</available_skills>");
    return str_take(&xml);
}

void skills_free(Skill *skills, int count) {
    for (int i = 0; i < count; i++) {
        free(skills[i].name);
        free(skills[i].description);
        free(skills[i].content);
        free(skills[i].path);
    }
    free(skills);
}
