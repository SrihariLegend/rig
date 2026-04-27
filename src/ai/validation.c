#include "validation.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static ValidationResult ok(void) {
    return (ValidationResult){ .valid = true, .error = NULL, .path = NULL };
}

static ValidationResult fail(const char *path, const char *msg) {
    return (ValidationResult){
        .valid = false,
        .error = strdup(msg),
        .path = strdup(path),
    };
}

void validation_result_free(ValidationResult *r) {
    if (!r) return;
    free(r->error);
    free(r->path);
    r->error = NULL;
    r->path = NULL;
}

static const char *json_type_name(const cJSON *item) {
    if (!item || cJSON_IsNull(item)) return "null";
    if (cJSON_IsBool(item)) return "boolean";
    if (cJSON_IsNumber(item)) return "number";
    if (cJSON_IsString(item)) return "string";
    if (cJSON_IsArray(item)) return "array";
    if (cJSON_IsObject(item)) return "object";
    return "unknown";
}

static ValidationResult validate_value(const cJSON *schema, const cJSON *value, const char *path);

static ValidationResult validate_object(const cJSON *schema, const cJSON *value, const char *path) {
    cJSON *properties = cJSON_GetObjectItem(schema, "properties");
    cJSON *required = cJSON_GetObjectItem(schema, "required");

    if (required && cJSON_IsArray(required)) {
        cJSON *req_item;
        cJSON_ArrayForEach(req_item, required) {
            if (cJSON_IsString(req_item)) {
                cJSON *val = cJSON_GetObjectItem(value, req_item->valuestring);
                if (!val) {
                    char full_path[512];
                    snprintf(full_path, sizeof(full_path), "%s.%s", path, req_item->valuestring);
                    char msg[256];
                    snprintf(msg, sizeof(msg), "required field missing: %s", req_item->valuestring);
                    return fail(full_path, msg);
                }
            }
        }
    }

    if (properties && cJSON_IsObject(properties)) {
        cJSON *prop;
        cJSON_ArrayForEach(prop, properties) {
            cJSON *val = cJSON_GetObjectItem(value, prop->string);
            if (val) {
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "%s.%s", path, prop->string);
                ValidationResult r = validate_value(prop, val, full_path);
                if (!r.valid) return r;
            }
        }
    }

    return ok();
}

static ValidationResult validate_array(const cJSON *schema, const cJSON *value, const char *path) {
    cJSON *items_schema = cJSON_GetObjectItem(schema, "items");
    if (!items_schema) return ok();

    int idx = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, value) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s[%d]", path, idx);
        ValidationResult r = validate_value(items_schema, item, full_path);
        if (!r.valid) return r;
        idx++;
    }

    return ok();
}

static ValidationResult validate_value(const cJSON *schema, const cJSON *value, const char *path) {
    if (!schema || !value) return ok();

    cJSON *type_field = cJSON_GetObjectItem(schema, "type");
    if (!type_field || !cJSON_IsString(type_field)) return ok();

    const char *expected = type_field->valuestring;

    if (strcmp(expected, "string") == 0) {
        if (!cJSON_IsString(value)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "expected string, got %s", json_type_name(value));
            return fail(path, msg);
        }
        cJSON *enum_field = cJSON_GetObjectItem(schema, "enum");
        if (enum_field && cJSON_IsArray(enum_field)) {
            bool found = false;
            cJSON *e;
            cJSON_ArrayForEach(e, enum_field) {
                if (cJSON_IsString(e) && strcmp(e->valuestring, value->valuestring) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                char msg[256];
                snprintf(msg, sizeof(msg), "value \"%s\" not in enum", value->valuestring);
                return fail(path, msg);
            }
        }
    } else if (strcmp(expected, "number") == 0 || strcmp(expected, "integer") == 0) {
        if (!cJSON_IsNumber(value)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "expected %s, got %s", expected, json_type_name(value));
            return fail(path, msg);
        }
        if (strcmp(expected, "integer") == 0 && value->valuedouble != floor(value->valuedouble)) {
            return fail(path, "expected integer, got float");
        }
    } else if (strcmp(expected, "boolean") == 0) {
        if (!cJSON_IsBool(value)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "expected boolean, got %s", json_type_name(value));
            return fail(path, msg);
        }
    } else if (strcmp(expected, "object") == 0) {
        if (!cJSON_IsObject(value)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "expected object, got %s", json_type_name(value));
            return fail(path, msg);
        }
        return validate_object(schema, value, path);
    } else if (strcmp(expected, "array") == 0) {
        if (!cJSON_IsArray(value)) {
            char msg[256];
            snprintf(msg, sizeof(msg), "expected array, got %s", json_type_name(value));
            return fail(path, msg);
        }
        return validate_array(schema, value, path);
    }

    return ok();
}

ValidationResult validate_tool_arguments(const Tool *tool, cJSON *arguments) {
    if (!tool || !tool->parameters) return ok();
    bool owns_args = false;
    if (!arguments) { arguments = cJSON_CreateObject(); owns_args = true; }
    ValidationResult vr = validate_value(tool->parameters, arguments, "$");
    if (owns_args) cJSON_Delete(arguments);
    return vr;
}

cJSON *coerce_tool_arguments(const cJSON *schema, cJSON *arguments) {
    if (!schema || !arguments) return arguments;

    cJSON *properties = cJSON_GetObjectItem(schema, "properties");
    if (!properties) return arguments;

    cJSON *prop;
    cJSON_ArrayForEach(prop, properties) {
        cJSON *type_field = cJSON_GetObjectItem(prop, "type");
        if (!type_field || !cJSON_IsString(type_field)) continue;

        cJSON *val = cJSON_GetObjectItem(arguments, prop->string);
        if (!val) continue;

        const char *expected = type_field->valuestring;

        if (strcmp(expected, "number") == 0 || strcmp(expected, "integer") == 0) {
            if (cJSON_IsString(val)) {
                double d = atof(val->valuestring);
                cJSON_ReplaceItemInObject(arguments, prop->string, cJSON_CreateNumber(d));
            } else if (cJSON_IsBool(val)) {
                cJSON_ReplaceItemInObject(arguments, prop->string,
                                          cJSON_CreateNumber(cJSON_IsTrue(val) ? 1 : 0));
            }
        } else if (strcmp(expected, "boolean") == 0) {
            if (cJSON_IsNumber(val)) {
                cJSON_ReplaceItemInObject(arguments, prop->string,
                                          cJSON_CreateBool(val->valuedouble != 0));
            }
        } else if (strcmp(expected, "string") == 0) {
            if (cJSON_IsNull(val)) {
                cJSON_ReplaceItemInObject(arguments, prop->string, cJSON_CreateString(""));
            }
        }
    }

    return arguments;
}
