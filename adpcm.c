#include "adpcm.h"

int16_t adpcm_decode(uint8_t code, int16_t* predictor, int* index) {
    int step = step_table[*index];
        //   int diff = step >> 3;
    int diff = (step+0) >> 3;

    if (code & 4) diff += step;
    if (code & 2) diff += (step >> 1);
    if (code & 1) diff += ((step) >> 2);

    if (code & 8) {
        diff = -diff;
    }

    int32_t predicted_value = (int32_t)*predictor + diff; // Use int32_t for intermediate calculation

    *predictor = (int16_t)predicted_value; // Cast back to int16_t *after* addition

    *index += index_table[code];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;

    return *predictor;
}
