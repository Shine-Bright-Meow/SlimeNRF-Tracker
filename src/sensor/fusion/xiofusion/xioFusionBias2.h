/**
 * @file FusionBias.h
 * @author Seb Madgwick
 * @brief Gyroscope bias correction algorithm for run-time calibration of the
 * gyroscope bias.
 */

#ifndef FUSION_BIAS2_H
#define FUSION_BIAS2_H

//------------------------------------------------------------------------------
// Includes

#include "FusionBias.h"

//------------------------------------------------------------------------------
// Function declarations

void FusionBiasInitialise2(FusionBias *const bias, const unsigned int sampleRate);

FusionVector FusionBiasUpdate2(FusionBias *const bias, FusionVector gyroscope);

#endif

//------------------------------------------------------------------------------
// End of file
