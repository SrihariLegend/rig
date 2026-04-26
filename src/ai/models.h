#ifndef PI_AI_MODELS_H
#define PI_AI_MODELS_H

#include "types.h"

void models_init(void);
const Model *models_get(const char *provider, const char *model_id);
const Model **models_get_all(const char *provider, int *count);
const char **models_get_providers(int *count);

#endif
