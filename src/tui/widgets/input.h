#ifndef PI_WIDGET_INPUT_H
#define PI_WIDGET_INPUT_H

#include "tui/tui.h"

Component *widget_input_create(const char *placeholder);
const char *widget_input_get_text(Component *comp);
void widget_input_set_text(Component *comp, const char *text);
void widget_input_clear(Component *comp);

#endif
