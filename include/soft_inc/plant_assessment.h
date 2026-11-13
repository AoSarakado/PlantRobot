#ifndef PLANT_ASSESSMENT_H
#define PLANT_ASSESSMENT_H

#include "environment_state.h"

typedef enum {
    PLANT_UNKNOWN = 0,
    PLANT_POTHOS,
    PLANT_SNAKE,
    PLANT_SPIDER,
    PLANT_COUNT
} plant_kind_t;

typedef enum {
    PLANT_TEMP_UNKNOWN,
    PLANT_TEMP_COLD,
    PLANT_TEMP_BELOW_PREFERRED,
    PLANT_TEMP_PREFERRED,
    PLANT_TEMP_ABOVE_PREFERRED
} plant_temp_status_t;

typedef struct {
    plant_temp_status_t temperature;
    unsigned reference_count;
    unsigned cold_count;
    unsigned preferred_count;
    plant_kind_t profile;
} plant_assessment_t;

const char *plant_kind_name(plant_kind_t kind);
plant_assessment_t plant_assess(const environment_t *environment, plant_kind_t kind);

#endif
