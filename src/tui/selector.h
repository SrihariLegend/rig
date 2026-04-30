#ifndef RIG_SELECTOR_H
#define RIG_SELECTOR_H

/*
 * Arrow-key navigable selection menu.
 * Renders items in the terminal, returns chosen index or -1 on ESC.
 */
int tui_selector(const char **items, int count, int initial);

#endif
