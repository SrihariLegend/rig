#ifndef PI_MD_RENDER_H
#define PI_MD_RENDER_H

#include "linestore.h"

void md_render_to_linestore(LineStore *ls, const char *markdown, int len, LineType default_type);

#endif
