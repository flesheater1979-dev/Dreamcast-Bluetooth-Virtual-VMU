#pragma once
#include <stdbool.h>

enum {
    NICEMCU_SD_STATUS_UNKNOWN = 0,
    NICEMCU_SD_STATUS_OK = 1,
    NICEMCU_SD_STATUS_ERROR = 2,
};

bool nicemcu_stage5_display_init(int sd_status, bool vmu_ready);
