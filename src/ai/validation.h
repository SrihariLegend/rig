#ifndef PI_AI_VALIDATION_H
#define PI_AI_VALIDATION_H

#include "types.h"

typedef struct {
    bool valid;
    char *error;
    char *path;
} ValidationResult;

ValidationResult validate_tool_arguments(const Tool *tool, cJSON *arguments);
void validation_result_free(ValidationResult *r);

cJSON *coerce_tool_arguments(const cJSON *schema, cJSON *arguments);

#endif
