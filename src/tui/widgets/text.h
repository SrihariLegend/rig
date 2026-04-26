#ifndef PI_WIDGET_TEXT_H
#define PI_WIDGET_TEXT_H

#include "tui/tui.h"

Component *widget_text_create(const char *text);
void widget_text_set(Component *comp, const char *text);

#endif
