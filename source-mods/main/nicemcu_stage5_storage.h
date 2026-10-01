#ifndef NICEMCU_STAGE5_STORAGE_H
#define NICEMCU_STAGE5_STORAGE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool nicemcu_stage5_storage_init(void);
bool nicemcu_stage5_storage_sd_ready(void);
const char *nicemcu_stage5_storage_mc_path(void);

#ifdef __cplusplus
}
#endif

#endif
