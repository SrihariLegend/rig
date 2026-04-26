#ifndef PI_WIDGET_MARKDOWN_H
#define PI_WIDGET_MARKDOWN_H

#include "tui/tui.h"

Component *widget_markdown_create(const char *markdown_text);
void widget_markdown_set(Component *comp, const char *text);
void widget_markdown_append(Component *comp, const char *text);

#endif
