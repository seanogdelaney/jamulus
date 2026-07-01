/******************************************************************************\
 * Copyright (c) 2026
 *
 * Author(s): Sean Ryan
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
\******************************************************************************/
#include "multisource.h"

namespace
{
void Put16 ( CVector<uint8_t>& out, int& position, uint16_t value )
{
    out[position++] = static_cast<uint8_t> ( value >> 8 );
    out[position++] = static_cast<uint8_t> ( value & 0xff );
}

uint16_t Get16 ( const CVector<uint8_t>& in, int& position )
{
    const uint16_t value = static_cast<uint16_t> ( ( in[position] << 8 ) | in[position + 1] );
    position += 2;
    return value;
}

bool ValidCodec ( const EAudComprType codec ) { return codec == CT_OPUS || codec == CT_OPUS64; }

bool ValidCompressedPayload ( const EAudComprType codec, const uint8_t channels, const uint16_t payloadBytes )
{
    // Jamulus uses fixed-size CBR Opus packets. Keep the negotiated values
    // tied to the existing quality presets so a malformed descriptor cannot
    // make decoder/packetizer storage disagree.
    static constexpr uint16_t kOpus128Mono[]   = { 25, 45, 82 };
    static constexpr uint16_t kOpus128Stereo[] = { 47, 71, 165 };
    static constexpr uint16_t kOpus64Mono[]    = { 12, 22, 36 };
    static constexpr uint16_t kOpus64Stereo[]  = { 24, 35, 73 };
    const uint16_t*           values           = nullptr;
    size_t                    count            = 0;
    if ( codec == CT_OPUS )
    {
        values = channels == 1 ? kOpus128Mono : kOpus128Stereo;
        count  = 3;
    }
    else if ( codec == CT_OPUS64 )
    {
        values = channels == 1 ? kOpus64Mono : kOpus64Stereo;
        count  = 3;
    }
    for ( size_t i = 0; i < count; ++i )
        if ( values[i] == payloadBytes )
            return true;
    return false;
}

bool ValidPayload ( const CMultiSourceSourceConfig& source )
{
    if ( source.bRaw )
    {
        const int frameSamples = source.eCodec == CT_OPUS ? DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES : SYSTEM_FRAME_SIZE_SAMPLES;
        return source.iPayloadBytes == static_cast<uint16_t> ( sizeof ( int16_t ) * source.iNumChannels * frameSamples );
    }
    return ValidCompressedPayload ( source.eCodec, source.iNumChannels, source.iPayloadBytes );
}
} // namespace

namespace MultiSourceProtocol
{
bool EncodeCaps ( CVector<uint8_t>& out )
{
    out.Init ( 2 );
    out[0] = kProtocolVersion;
    out[1] = static_cast<uint8_t> ( MultiSource::kMaxSourceRows );
    return true;
}

bool DecodeCaps ( const CVector<uint8_t>& in )
{
    return in.Size() == 2 && in[0] == kProtocolVersion && in[1] > 0 && in[1] <= MultiSource::kMaxSourceRows;
}

bool ValidateSourceConfig ( const CVector<CMultiSourceSourceConfig>& config, QString* const error )
{
    if ( config.empty() || config.Size() > static_cast<int> ( MultiSource::kMaxSourceRows ) )
    {
        if ( error != nullptr )
            *error = "invalid number of Advanced sources";
        return false;
    }
    const bool          bRaw  = config[0].bRaw;
    const EAudComprType codec = config[0].eCodec;
    for ( int i = 0; i < config.Size(); ++i )
    {
        const CMultiSourceSourceConfig& source = config[i];
        const QByteArray                tag    = source.strTag.trimmed().toUtf8();
        if ( source.iKey == 0 || source.iNumChannels < 1 || source.iNumChannels > 2 || !ValidCodec ( source.eCodec ) || source.iPayloadBytes == 0 ||
             source.iPayloadBytes > MultiSource::kMaxRawStereoPayloadBytes || !ValidPayload ( source ) || tag.isEmpty() || tag.size() > 63 ||
             source.bRaw != bRaw || source.eCodec != codec )
        {
            if ( error != nullptr )
                *error = "invalid Advanced source descriptor";
            return false;
        }
        for ( int earlier = 0; earlier < i; ++earlier )
        {
            if ( config[earlier].iKey == source.iKey || config[earlier].strTag.trimmed() == source.strTag.trimmed() )
            {
                if ( error != nullptr )
                    *error = "Advanced source keys and tags must be unique";
                return false;
            }
        }
    }
    return true;
}

bool EncodeConfig ( const CVector<CMultiSourceSourceConfig>& config, CVector<uint8_t>& out )
{
    if ( !ValidateSourceConfig ( config ) )
    {
        return false;
    }
    int bytes = 3; // version, flags, count
    for ( const CMultiSourceSourceConfig& source : config )
    {
        bytes += 9 + source.strTag.trimmed().toUtf8().size();
    }
    out.Init ( bytes );
    int position    = 0;
    out[position++] = kProtocolVersion;
    out[position++] = config[0].bRaw ? 1 : 0;
    out[position++] = static_cast<uint8_t> ( config.Size() );
    for ( const CMultiSourceSourceConfig& source : config )
    {
        const QByteArray tag = source.strTag.trimmed().toUtf8();
        out[position++]      = source.iKey;
        out[position++]      = source.iNumChannels;
        out[position++]      = static_cast<uint8_t> ( source.eCodec );
        out[position++]      = source.bRaw ? 1 : 0;
        Put16 ( out, position, source.iPayloadBytes );
        Put16 ( out, position, static_cast<uint16_t> ( source.iInstrument ) );
        out[position++] = static_cast<uint8_t> ( tag.size() );
        for ( int i = 0; i < tag.size(); ++i )
            out[position++] = static_cast<uint8_t> ( tag[i] );
    }
    return true;
}

bool DecodeConfig ( const CVector<uint8_t>& in, CVector<CMultiSourceSourceConfig>& config )
{
    config.clear();
    if ( in.Size() < 3 || in[0] != kProtocolVersion || ( in[1] & ~uint8_t ( 1 ) ) != 0 || in[2] == 0 || in[2] > MultiSource::kMaxSourceRows )
    {
        return false;
    }
    const bool bRaw  = ( in[1] & 1 ) != 0;
    const int  count = in[2];
    config.Init ( count );
    int position = 3;
    for ( int i = 0; i < count; ++i )
    {
        if ( position + 9 > in.Size() )
        {
            config.clear();
            return false;
        }
        CMultiSourceSourceConfig& source = config[i];
        source.iKey                      = in[position++];
        source.iNumChannels              = in[position++];
        source.eCodec                    = static_cast<EAudComprType> ( in[position++] );
        source.bRaw                      = in[position++] != 0;
        source.iPayloadBytes             = Get16 ( in, position );
        source.iInstrument               = Get16 ( in, position );
        const int tagLength              = in[position++];
        if ( position + tagLength > in.Size() || tagLength == 0 )
        {
            config.clear();
            return false;
        }
        source.strTag = QString::fromUtf8 ( reinterpret_cast<const char*> ( &in[position] ), tagLength );
        position += tagLength;
        if ( source.bRaw != bRaw )
        {
            config.clear();
            return false;
        }
    }
    if ( position != in.Size() || !ValidateSourceConfig ( config ) )
    {
        config.clear();
        return false;
    }
    return true;
}

bool EncodeAccept ( const CMultiSourceAcceptMap& accept, CVector<uint8_t>& out )
{
    if ( accept.iGeneration == 0 || !ValidateSourceConfig ( accept.vecSources ) )
    {
        return false;
    }
    out.Init ( 4 + 3 * accept.vecSources.Size() );
    int position    = 0;
    out[position++] = kProtocolVersion;
    Put16 ( out, position, accept.iGeneration );
    out[position++] = static_cast<uint8_t> ( accept.vecSources.Size() );
    for ( const CMultiSourceSourceConfig& source : accept.vecSources )
    {
        if ( source.iFaderID < 0 || source.iFaderID >= MAX_NUM_CHANNELS )
            return false;
        out[position++] = source.iKey;
        Put16 ( out, position, static_cast<uint16_t> ( source.iFaderID ) );
    }
    return true;
}

bool DecodeAccept ( const CVector<uint8_t>& in, CMultiSourceAcceptMap& accept )
{
    accept = CMultiSourceAcceptMap();
    if ( in.Size() < 4 || in[0] != kProtocolVersion || in[3] == 0 || in.Size() != 4 + 3 * in[3] )
        return false;
    int position       = 1;
    accept.iGeneration = Get16 ( in, position );
    const int count    = in[position++];
    if ( accept.iGeneration == 0 || count > static_cast<int> ( MultiSource::kMaxSourceRows ) )
        return false;
    accept.vecSources.Init ( count );
    for ( int i = 0; i < count; ++i )
    {
        accept.vecSources[i].iKey     = in[position++];
        accept.vecSources[i].iFaderID = Get16 ( in, position );
        if ( accept.vecSources[i].iKey == 0 || accept.vecSources[i].iFaderID >= MAX_NUM_CHANNELS )
            return false;
        for ( int earlier = 0; earlier < i; ++earlier )
            if ( accept.vecSources[earlier].iKey == accept.vecSources[i].iKey )
                return false;
    }
    return true;
}

bool EncodeReject ( const uint8_t reason, CVector<uint8_t>& out )
{
    out.Init ( 2 );
    out[0] = kProtocolVersion;
    out[1] = reason;
    return true;
}

bool DecodeReject ( const CVector<uint8_t>& in, uint8_t& reason )
{
    if ( in.Size() != 2 || in[0] != kProtocolVersion )
        return false;
    reason = in[1];
    return true;
}

bool EncodeActive ( const uint16_t generation, CVector<uint8_t>& out )
{
    if ( generation == 0 )
        return false;
    out.Init ( 3 );
    int position    = 0;
    out[position++] = kProtocolVersion;
    Put16 ( out, position, generation );
    return true;
}

bool DecodeActive ( const CVector<uint8_t>& in, uint16_t& generation )
{
    if ( in.Size() != 3 || in[0] != kProtocolVersion )
        return false;
    int position = 1;
    generation   = Get16 ( in, position );
    return generation != 0;
}
} // namespace MultiSourceProtocol
