/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#pragma once

#include <QString>
#include <QMetaType>

#include "polyinwire.h"
#include "util.h"

// Protocol-facing metadata for a Poly-in capture source.  `key` is local to
// a physical session; `faderId` is the ordinary globally visible server mixer
// ID assigned during promotion.
struct CPolyInSourceConfig
{
    uint8_t       iKey          = 0;
    uint8_t       iNumChannels  = 1;
    EAudComprType eCodec        = CT_NONE;
    bool          bRaw          = false;
    uint16_t      iPayloadBytes = 0;
    int           iInstrument   = CInstPictures::GetNotUsedInstrument();
    QString       strTag;
    int           iFaderID = INVALID_INDEX;
};

// POLY_IN_ACCEPT returns reserved ordinary fader IDs.  Those IDs become
// visible together only after the server promotes the map on first valid audio.
struct CPolyInAcceptMap
{
    uint16_t                     iGeneration = 0;
    CVector<CPolyInSourceConfig> vecSources;
};

Q_DECLARE_METATYPE ( CPolyInSourceConfig )
Q_DECLARE_METATYPE ( CVector<CPolyInSourceConfig> )
Q_DECLARE_METATYPE ( CPolyInAcceptMap )

namespace PolyIn
{
struct ChannelProfiles
{
    EAudChanConf input;
    EAudChanConf transport;
};

// Poly-in keeps the saved standard input profile, but fixes the physical
// connection to stereo before negotiation so every fallback preserves the
// same packet and return-buffer geometry.
inline ChannelProfiles ResolveChannelProfiles ( const EAudChanConf selected, const EAudChanConf savedLegacy )
{
    return selected == CC_POLY_IN ? ChannelProfiles{ savedLegacy, CC_STEREO } : ChannelProfiles{ selected, selected };
}
} // namespace PolyIn

namespace PolyInProtocol
{
constexpr uint8_t kProtocolVersion            = PolyIn::kVersion;
constexpr uint8_t kRejectMalformed            = 1;
constexpr uint8_t kRejectCapacity             = 2;
constexpr uint8_t kRejectUnsupported          = 3;
constexpr uint8_t kRejectSplitMessageNotReady = 4;
constexpr uint8_t kRejectInvalidSessionState  = 5;
constexpr uint8_t kRejectStereoReturnRequired = 6;

bool EncodeCaps ( CVector<uint8_t>& out );
bool DecodeCaps ( const CVector<uint8_t>& in );

bool EncodeConfig ( const CVector<CPolyInSourceConfig>& config, CVector<uint8_t>& out );
bool DecodeConfig ( const CVector<uint8_t>& in, CVector<CPolyInSourceConfig>& config );

bool EncodeAccept ( const CPolyInAcceptMap& accept, CVector<uint8_t>& out );
bool DecodeAccept ( const CVector<uint8_t>& in, CPolyInAcceptMap& accept );

bool EncodeReject ( uint8_t reason, CVector<uint8_t>& out );
bool DecodeReject ( const CVector<uint8_t>& in, uint8_t& reason );

// Sent only after the first valid Poly-in audio packet atomically commits the
// prepared source map on the server. It is a confirmation, not permission to
// start sending; permission remains POLY_IN_ACCEPT.
bool EncodeActive ( uint16_t generation, CVector<uint8_t>& out );
bool DecodeActive ( const CVector<uint8_t>& in, uint16_t& generation );

bool ValidateSourceConfig ( const CVector<CPolyInSourceConfig>& config, QString* error = nullptr );
} // namespace PolyInProtocol
