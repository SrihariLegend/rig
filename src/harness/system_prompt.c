#include "system_prompt.h"
#include "util/str.h"
#include <time.h>
#include <stdio.h>

char *system_prompt_build(const Tool *tools, int tool_count, const char *cwd) {
    Str s = str_new(4096);

    str_append(&s, "You are an AI coding assistant. You help users with software engineering tasks.\n\n");
    str_append(&s, "# Environment\n");
    if (cwd) {
        str_appendf(&s, "- Working directory: %s\n", cwd);
    }

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char date[32];
    strftime(date, sizeof(date), "%Y-%m-%d", tm);
    str_appendf(&s, "- Current date: %s\n", date);

    if (tools && tool_count > 0) {
        str_append(&s, "\n# Available Tools\n\n");
        for (int i = 0; i < tool_count; i++) {
            str_appendf(&s, "- **%s**", tools[i].name);
            if (tools[i].description) {
                str_appendf(&s, ": %s", tools[i].description);
            }
            str_append(&s, "\n");
        }
    }

    str_append(&s, "\n# Instructions\n");
    str_append(&s, "- Use tools to help the user accomplish their tasks\n");
    str_append(&s, "- Be concise and direct\n");
    str_append(&s, "- When editing files, prefer the edit tool over write for existing files\n");
    str_append(&s, "- Always verify your changes work correctly\n");

    return str_take(&s);
}
