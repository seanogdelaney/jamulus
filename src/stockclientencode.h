/******************************************************************************\
 * Single stock-client encode implementation shared by legacy and poly-in.
 * Copyright (c) 2026. AGPL-3.0-or-later.
\******************************************************************************/
#pragma once
#ifdef USE_OPUS_SHARED_LIB
#    include "opus/opus_custom.h"
#else
#    include "opus_custom.h"
#endif
#include <cstdint>
#include <cstring>

// Keep controls/bitrate and original call semantics in ONE place; the existing
// CClient invokes these functions on its original, unchanged encoder objects.
inline void StockClientEncoderDefaults ( OpusCustomEncoder* encoder, bool size64 )
{
    opus_custom_encoder_ctl ( encoder, OPUS_SET_VBR ( 0 ) );
    if ( size64 )
        opus_custom_encoder_ctl ( encoder, OPUS_SET_PACKET_LOSS_PERC ( 35 ) );
    opus_custom_encoder_ctl ( encoder, OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
    if ( !size64 )
        opus_custom_encoder_ctl ( encoder, OPUS_SET_COMPLEXITY ( 1 ) );
}

inline void StockClientEncoderBitrate ( OpusCustomEncoder* encoder, int bytes, int samples, int sampleRate )
{
    opus_custom_encoder_ctl ( encoder, OPUS_SET_BITRATE ( sampleRate / samples * bytes * 8 ) );
}

inline void StockClientEncodeFrame ( OpusCustomEncoder* encoder,
                                     bool               rawSupported,
                                     bool               muted,
                                     const int16_t*     samples,
                                     const int16_t*     zeros,
                                     int                frameSamples,
                                     uint8_t*           encoded,
                                     int                encodedBytes )
{
    if ( encoder )
    {
        const int iUnused = opus_custom_encode ( encoder, muted ? zeros : samples, frameSamples, encoded, encodedBytes );
        (void) iUnused; // Stock intentionally sends fixed-size frame after encoder error.
    }
    else if ( rawSupported )
    {
        if ( muted )
            memset ( encoded, 0, encodedBytes );
        else
            memcpy ( encoded, samples, encodedBytes );
    }
}
