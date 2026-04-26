#ifndef PI_WIDGET_EDITOR_H
#define PI_WIDGET_EDITOR_H

#include "tui/tui.h"

Component *widget_editor_create(int visible_lines);
const char *widget_editor_get_text(Component *comp);
void widget_editor_set_text(Component *comp, const char *text);
void widget_editor_clear(Component *comp);
int widget_editor_get_cursor_line(Component *comp);
int widget_editor_get_cursor_col(Component *comp);
int widget_editor_get_line_count(Component *comp);

#endif
