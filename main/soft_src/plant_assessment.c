#include "plant_assessment.h"

typedef struct {
    const char *name;
    float minimum_c;
    float preferred_low_c;
    float preferred_high_c;
} plant_reference_t;

/* Sources and interpretation: README.md, "Reference profiles". */
static const plant_reference_t s_references[PLANT_COUNT] = {
    [PLANT_POTHOS] = {"pothos", 15.5f, 15.5f, 26.5f},
    [PLANT_SNAKE]  = {"snake plant", 10.0f, 15.0f, 24.0f},
    [PLANT_SPIDER] = {"spider plant", 10.0f, 10.0f, 0.0f},
};

const char *plant_kind_name(plant_kind_t kind)
{
    return kind > PLANT_UNKNOWN && kind < PLANT_COUNT ? s_references[kind].name : "unknown";
}

static plant_temp_status_t assess_one(float temp, const plant_reference_t *reference)
{
    if (temp < reference->minimum_c) return PLANT_TEMP_COLD;
    if (temp < reference->preferred_low_c) return PLANT_TEMP_BELOW_PREFERRED;
    if (reference->preferred_high_c <= 0) return PLANT_TEMP_UNKNOWN;
    if (temp > reference->preferred_high_c)
        return PLANT_TEMP_ABOVE_PREFERRED;
    return PLANT_TEMP_PREFERRED;
}

plant_assessment_t plant_assess(const environment_t *environment, plant_kind_t kind)
{
    plant_assessment_t result = {.temperature = PLANT_TEMP_UNKNOWN, .profile = kind};
    if (!environment || !environment->temperature_valid || kind >= PLANT_COUNT) return result;
    if (kind != PLANT_UNKNOWN) {
        result.reference_count = 1;
        result.temperature = assess_one(environment->temperature, &s_references[kind]);
        result.cold_count = result.temperature == PLANT_TEMP_COLD;
        result.preferred_count = result.temperature == PLANT_TEMP_PREFERRED;
        return result;
    }
    for (plant_kind_t i = PLANT_POTHOS; i < PLANT_COUNT; i++) {
        plant_temp_status_t status = assess_one(environment->temperature, &s_references[i]);
        result.reference_count++;
        result.cold_count += status == PLANT_TEMP_COLD;
        result.preferred_count += status == PLANT_TEMP_PREFERRED;
    }
    if (result.cold_count == result.reference_count) result.temperature = PLANT_TEMP_COLD;
    else if (result.preferred_count == result.reference_count) result.temperature = PLANT_TEMP_PREFERRED;
    return result;
}
