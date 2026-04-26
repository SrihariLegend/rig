#ifndef PI_WIDGET_SELECT_LIST_H
#define PI_WIDGET_SELECT_LIST_H

#include "tui/tui.h"

typedef struct {
    char *label;
    char *value;
    char *description;
} SelectItem;

Component *widget_select_list_create(SelectItem *items, int item_count);
int widget_select_list_get_selected(Component *comp);
const char *widget_select_list_get_value(Component *comp);

#endif
