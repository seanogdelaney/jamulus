/******************************************************************************\
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Decode one stock media frame. No allocation, no jitter or session state.
\******************************************************************************/
#pragma once
#include "../libs/opus/include/opus_custom.h"
#include <cstdint>
#include <cstring>
namespace AudioFrameDecode
{
inline int Decode(OpusCustomDecoder* decoder, const unsigned char* data,
                  int codedBytes, int16_t* pcm, int samples, int channels, bool raw)
{
    if (!raw) return decoder ? opus_custom_decode(decoder, data, codedBytes, pcm, samples) : 0;
    if (data) std::memcpy(pcm, data, codedBytes);
    else std::memset(pcm, 0, codedBytes);
    return samples * channels;
}
}
