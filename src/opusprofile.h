/******************************************************************************\
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Shared stock encoder-profile setup; keep bitstream and controls unchanged.
\******************************************************************************/
#pragma once
#include "../libs/opus/include/opus_custom.h"
#include "../libs/opus/include/opus_defines.h"
namespace OpusProfile
{
inline void Configure (OpusCustomEncoder* encoder, bool frame64)
{
    opus_custom_encoder_ctl(encoder, OPUS_SET_VBR(0));
    if (frame64) opus_custom_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(35));
    opus_custom_encoder_ctl(encoder, OPUS_SET_APPLICATION(OPUS_APPLICATION_RESTRICTED_LOWDELAY));
    if (!frame64) opus_custom_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(1));
}
}
