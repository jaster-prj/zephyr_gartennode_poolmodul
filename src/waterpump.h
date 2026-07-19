#ifndef _WATERPUMP_H
#define _WATERPUMP_H

#include <zephyr/kernel.h>

#if !DT_NODE_EXISTS(DT_NODELABEL(waterpump_sw))
#error "Overlay for waterpump_sw output node not properly defined."
#endif

int waterpump_init(void);
int waterpump_on(void);
int waterpump_off(void);
bool waterpump_is_on(void);
int waterpump_process(void);

#endif /* _WATERPUMP_H */
