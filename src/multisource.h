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

#include "multisourcewire.h"
#include "util.h"

// Protocol-facing metadata for an Advanced capture source.  `key` is local to
// a physical session; `faderId` is the ordinary globally visible server mixer
// ID assigned during promotion.
struct CMultiSourceSourceConfig
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

struct CMultiSourceAcceptMap
{
    uint16_t                          iGeneration = 0;
    CVector<CMultiSourceSourceConfig> vecSources;
};

Q_DECLARE_METATYPE ( CMultiSourceSourceConfig )
Q_DECLARE_METATYPE ( CVector<CMultiSourceSourceConfig> )
Q_DECLARE_METATYPE ( CMultiSourceAcceptMap )

namespace MultiSourceProtocol
{
constexpr uint8_t kProtocolVersion            = MultiSource::kVersion;
constexpr uint8_t kRejectMalformed            = 1;
constexpr uint8_t kRejectCapacity             = 2;
constexpr uint8_t kRejectUnsupported          = 3;
constexpr uint8_t kRejectSplitMessageNotReady = 4;
constexpr uint8_t kRejectInvalidSessionState  = 5;

bool EncodeCaps ( CVector<uint8_t>& out );
bool DecodeCaps ( const CVector<uint8_t>& in );

bool EncodeConfig ( const CVector<CMultiSourceSourceConfig>& config, CVector<uint8_t>& out );
bool DecodeConfig ( const CVector<uint8_t>& in, CVector<CMultiSourceSourceConfig>& config );

bool EncodeAccept ( const CMultiSourceAcceptMap& accept, CVector<uint8_t>& out );
bool DecodeAccept ( const CVector<uint8_t>& in, CMultiSourceAcceptMap& accept );

bool EncodeReject ( uint8_t reason, CVector<uint8_t>& out );
bool DecodeReject ( const CVector<uint8_t>& in, uint8_t& reason );

// Sent only after the first valid Advanced audio packet atomically commits the
// prepared source map on the server. It is a confirmation, not permission to
// start sending; permission remains MULTISOURCE_ACCEPT.
bool EncodeActive ( uint16_t generation, CVector<uint8_t>& out );
bool DecodeActive ( const CVector<uint8_t>& in, uint16_t& generation );

bool ValidateSourceConfig ( const CVector<CMultiSourceSourceConfig>& config, QString* error = nullptr );
} // namespace MultiSourceProtocol
