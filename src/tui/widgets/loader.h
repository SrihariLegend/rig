#ifndef PI_WIDGET_LOADER_H
#define PI_WIDGET_LOADER_H

#include "tui/tui.h"

Component *widget_loader_create(const char *message);
void widget_loader_tick(Component *comp);

#endif
