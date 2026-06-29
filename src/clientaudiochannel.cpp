/******************************************************************************\
 * Copyright (c) 2004-2026
 *
 * Author(s):
 *  Sean Ryan
 *
 * As of Jamulus 3.12.1dev (commit eb172d47): All new source code contributions must be licensed
 * under AGPL 3.0 or any later version.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
\******************************************************************************/

#include "clientaudiochannel.h"

CClientAudioChannel::CClientAudioChannel ( const quint16 iPortNumber, const quint16 iQosNumber, const bool bDisableIPv6 ) :
    Channel ( false ),
    bIPv6Available ( false ),
    Socket ( &Channel, iPortNumber, iQosNumber, "", bDisableIPv6, bIPv6Available ),
    pOpusMode ( nullptr ),
    pOpusEncoder ( nullptr ),
    eAudioCompressionType ( CT_NONE ),
    iCodedBytes ( 0 ),
    iFrameSizeSamples ( 0 ),
    iFrameSizeFactor ( 0 ),
    iNumAudioChannels ( 1 ),
    iSockBufNumFrames ( DEF_NET_BUF_SIZE_NUM_BL ),
    iServerSockBufNumFrames ( DEF_NET_BUF_SIZE_NUM_BL ),
    bDoAutoSockBufSize ( false ),
    bUseRawAudio ( false ),
    bSocketStarted ( false )
{
    QObject::connect ( &Channel, &CChannel::MessReadyForSending, &Channel, [this] ( CVector<uint8_t> vecMessage ) {
        Socket.SendPacket ( vecMessage, Channel.GetAddress() );
    } );
    QObject::connect ( &ConnLessProtocol,
                       &CProtocol::CLMessReadyForSending,
                       &Channel,
                       [this] ( CHostAddress InetAddr, CVector<uint8_t> vecMessage ) { Socket.SendPacket ( vecMessage, InetAddr ); } );
    QObject::connect ( &Channel, &CChannel::NewConnection, &Channel, [this] { OnNewConnection(); } );
    QObject::connect ( &Channel, &CChannel::ReqChanInfo, &Channel, [this] { OnReqChanInfo(); } );
    QObject::connect ( &Channel, &CChannel::ReqJittBufSize, &Channel, [this] { CreateServerJitterBufferMessage(); } );
    QObject::connect ( &Channel,
                       &CChannel::DetectedCLMessage,
                       &Channel,
                       [this] ( CVector<uint8_t> vecMessage, int iRecID, CHostAddress RecHostAddr ) {
                           ConnLessProtocol.ParseConnectionLessMessageBody ( vecMessage, iRecID, RecHostAddr );
                       } );
}

CClientAudioChannel::~CClientAudioChannel()
{
    Disconnect();
    DestroyEncoder();
}

void CClientAudioChannel::StartSocket()
{
    if ( !bSocketStarted )
    {
        Socket.Start();
        bSocketStarted = true;
    }
}

void CClientAudioChannel::SetEnabled ( const bool bEnable )
{
    Channel.SetEnable ( bEnable );
}

void CClientAudioChannel::Disconnect()
{
    if ( Channel.IsEnabled() )
    {
        Channel.SetEnable ( false );
        ConnLessProtocol.CreateCLDisconnection ( Channel.GetAddress() );
    }
}

void CClientAudioChannel::SetChannelInfo ( const CChannelCoreInfo& NewChannelInfo )
{
    ChannelInfo = NewChannelInfo;
    if ( Channel.IsEnabled() )
    {
        Channel.SetRemoteInfo ( ChannelInfo );
    }
}

void CClientAudioChannel::Configure ( const EAudComprType  eNewAudioCompressionType,
                                      const int            iNewCodedBytes,
                                      const int            iNewFrameSizeSamples,
                                      const int            iNewFrameSizeFactor,
                                      const int            iNewNumAudioChannels,
                                      OpusCustomMode*      pNewOpusMode,
                                      const bool           bNewUseRawAudio,
                                      const int            iNewSockBufNumFrames,
                                      const int            iNewServerSockBufNumFrames,
                                      const bool           bNewDoAutoSockBufSize )
{
    const bool bCodecChanged = ( pOpusMode != pNewOpusMode ) || ( iNumAudioChannels != iNewNumAudioChannels ) ||
                               ( bUseRawAudio != bNewUseRawAudio ) || ( eAudioCompressionType != eNewAudioCompressionType );

    pOpusMode             = pNewOpusMode;
    eAudioCompressionType = eNewAudioCompressionType;
    iCodedBytes           = iNewCodedBytes;
    iFrameSizeSamples     = iNewFrameSizeSamples;
    iFrameSizeFactor      = iNewFrameSizeFactor;
    iNumAudioChannels     = iNewNumAudioChannels;
    iSockBufNumFrames     = iNewSockBufNumFrames;
    iServerSockBufNumFrames = iNewServerSockBufNumFrames;
    bDoAutoSockBufSize    = bNewDoAutoSockBufSize;
    bUseRawAudio          = bNewUseRawAudio;

    Channel.SetSockBufNumFrames ( iSockBufNumFrames );
    Channel.SetDoAutoSockBufSize ( bDoAutoSockBufSize );
    Channel.SetAudioStreamProperties ( eAudioCompressionType, iCodedBytes, iFrameSizeFactor, iNumAudioChannels );

    if ( bCodecChanged )
    {
        DestroyEncoder();
        if ( !bUseRawAudio )
        {
            CreateEncoder();
        }
    }

    if ( pOpusEncoder != nullptr )
    {
        opus_custom_encoder_ctl ( pOpusEncoder,
                                  OPUS_SET_BITRATE ( CalcBitRateBitsPerSecFromCodedBytes ( iCodedBytes, iFrameSizeSamples ) ) );
    }

    vecCodedData.Init ( iCodedBytes );
    vecDiscardedReceiveData.Init ( iCodedBytes );
    vecZeros.Init ( iFrameSizeFactor * iFrameSizeSamples * iNumAudioChannels, 0 );
    vecAudioData.Init ( iFrameSizeFactor * iFrameSizeSamples * iNumAudioChannels );
}

void CClientAudioChannel::SetSocketBufferSettings ( const int  iNewSockBufNumFrames,
                                                     const int  iNewServerSockBufNumFrames,
                                                     const bool bNewDoAutoSockBufSize )
{
    iSockBufNumFrames       = iNewSockBufNumFrames;
    iServerSockBufNumFrames = iNewServerSockBufNumFrames;
    bDoAutoSockBufSize      = bNewDoAutoSockBufSize;
    Channel.SetSockBufNumFrames ( iSockBufNumFrames );
    Channel.SetDoAutoSockBufSize ( bDoAutoSockBufSize );
    CreateServerJitterBufferMessage();
}

void CClientAudioChannel::CreateServerJitterBufferMessage()
{
    Channel.CreateJitBufMes ( bDoAutoSockBufSize ? AUTO_NET_BUF_SIZE_FOR_PROTOCOL : iServerSockBufNumFrames );
}

void CClientAudioChannel::Process ( const CVector<int16_t>& vecAudio, const bool bMute )
{
    if ( !Channel.IsEnabled() || ( iFrameSizeSamples == 0 ) || ( iFrameSizeFactor == 0 ) )
    {
        return;
    }

    const int iFrameSamples = iFrameSizeSamples * iNumAudioChannels;
    int       iUnused       = 0;
    for ( int i = 0, iOffset = 0; i < iFrameSizeFactor; i++, iOffset += iFrameSamples )
    {
        if ( pOpusEncoder != nullptr )
        {
            iUnused = opus_custom_encode ( pOpusEncoder,
                                           bMute ? &vecZeros[iOffset] : &vecAudio[iOffset],
                                           iFrameSizeSamples,
                                           &vecCodedData[0],
                                           iCodedBytes );
        }
        else if ( bUseRawAudio )
        {
            if ( bMute )
            {
                memset ( &vecCodedData[0], 0, iCodedBytes );
            }
            else
            {
                memcpy ( &vecCodedData[0], &vecAudio[iOffset], iCodedBytes );
            }
        }

        Channel.PrepAndSendPacket ( &Socket, vecCodedData, iCodedBytes );
    }
    Channel.UpdateSocketBufferSize();
    Q_UNUSED ( iUnused )
}

void CClientAudioChannel::DiscardReceivedAudio()
{
    if ( Channel.IsEnabled() && ( iCodedBytes > 0 ) )
    {
        for ( int i = 0; i < iFrameSizeFactor; ++i )
        {
            Channel.GetData ( vecDiscardedReceiveData, iCodedBytes );
        }
    }
}

void CClientAudioChannel::CreateEncoder()
{
    if ( pOpusMode == nullptr )
    {
        return;
    }

    int iOpusError;
    pOpusEncoder = opus_custom_encoder_create ( pOpusMode, iNumAudioChannels, &iOpusError );
    Q_UNUSED ( iOpusError )

    opus_custom_encoder_ctl ( pOpusEncoder, OPUS_SET_VBR ( 0 ) );
    opus_custom_encoder_ctl ( pOpusEncoder, OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
    if ( eAudioCompressionType == CT_OPUS64 )
    {
        opus_custom_encoder_ctl ( pOpusEncoder, OPUS_SET_PACKET_LOSS_PERC ( 35 ) );
    }
    else
    {
        opus_custom_encoder_ctl ( pOpusEncoder, OPUS_SET_COMPLEXITY ( 1 ) );
    }
}

void CClientAudioChannel::DestroyEncoder()
{
    if ( pOpusEncoder != nullptr )
    {
        opus_custom_encoder_destroy ( pOpusEncoder );
        pOpusEncoder = nullptr;
    }
}

void CClientAudioChannel::OnNewConnection()
{
    Channel.SetRemoteInfo ( ChannelInfo );
    CreateServerJitterBufferMessage();
}

void CClientAudioChannel::OnReqChanInfo()
{
    Channel.SetRemoteInfo ( ChannelInfo );
}
