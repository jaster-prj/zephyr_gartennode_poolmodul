#ifndef POOL_UPDATE_DIAGNOSTICS_H
#define POOL_UPDATE_DIAGNOSTICS_H

#include <canopennode.h>

#if defined(CONFIG_POOL_UPDATE_DIAGNOSTICS)
void pool_update_diagnostics_attach(CO_SDO_t *sdo);
#endif

#endif
