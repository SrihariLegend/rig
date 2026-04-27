#ifndef RIG_AI_JSON_PARSE_H
#define RIG_AI_JSON_PARSE_H

#include "cjson/cJSON.h"

cJSON *json_parse_repair(const char *json);
cJSON *json_parse_streaming(const char *partial_json);

#endif
