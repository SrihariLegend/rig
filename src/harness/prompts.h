#ifndef PI_HARNESS_PROMPTS_H
#define PI_HARNESS_PROMPTS_H

typedef struct {
    char *name;             // filename without .md
    char *description;      // from frontmatter
    char *argument_hint;    // from frontmatter, shown in autocomplete
    char *content;          // template body (after frontmatter)
    char *path;             // source file path
} PromptTemplate;

// Discover prompt templates from directories
// Looks for .md files directly in each path (non-recursive)
PromptTemplate *prompts_discover(const char **paths, int path_count, int *out_count);

// Expand template with arguments
// Substitutes: $1, $2, ... for positional args
//              $@ or $ARGUMENTS for all args joined with space
//              ${@:N} for args from Nth onwards (1-indexed)
//              ${@:N:L} for L args starting at N
// Returns malloc'd expanded string
char *prompts_expand(const PromptTemplate *pt, const char **args, int arg_count);

// Free prompt templates array
void prompts_free(PromptTemplate *templates, int count);

#endif
