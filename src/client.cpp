/******************************************************************************\
 * Copyright (c) 2004-2026
 *
 * Author(s):
 *  Volker Fischer
 *
 * As of Jamulus 3.12.1dev (commit eb172d47): All new source code contributions must be licensed
 * under AGPL 3.0 or any later version.
 *
 * Existing code: Code contributed before 3.12.1dev (commit eb172d47) was licensed under GPL 2.0+.
 * This code will be licensed under GPL 3.0 (or any later version) from
 * 3.12.1dev (commit eb172d47).  When distributed as part of Jamulus, the AGPL 3.0 terms govern
 * the combined work, including network use provisions.
 *
 ******************************************************************************
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
 * ---------------------------------------------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
\******************************************************************************/

#include "client.h"
#include "settings.h"
#include "util.h"

/* Implementation *************************************************************/
CClient::CClient ( const quint16  iPortNumber,
                   const quint16  iQosNumber,
                   const bool     bNoAutoJackConnect,
                   const QString& strNClientName,
                   const bool     bNDisableIPv6,
                   const bool     bNMuteMeInPersonalMix ) :
    ChannelInfo(),
    strClientName ( strNClientName ),
    pSignalHandler ( CSignalHandler::getSingletonP() ),
    pSettings ( nullptr ),
    PrimaryAudioChannel ( iPortNumber, iQosNumber, bNDisableIPv6 ),
    Channel ( PrimaryAudioChannel.GetChannel() ),
    ConnLessProtocol ( PrimaryAudioChannel.GetConnLessProtocol() ),
    Socket ( PrimaryAudioChannel.GetSocket() ),
    CurOpusDecoder ( nullptr ),
    eAudioCompressionType ( CT_OPUS ),
    iCeltNumCodedBytes ( OPUS_NUM_BYTES_MONO_LOW_QUALITY ),
    iOPUSFrameSizeSamples ( DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES ),
    eAudioQuality ( AQ_NORMAL ),
    eAudioChannelConf ( CC_MONO ),
    iNumAudioChannels ( 1 ),
    bIsInitializationPhase ( true ),
    bMuteOutStream ( false ),
    fMuteOutStreamGain ( 1.0f ),
    Sound ( AudioCallback, this, bNoAutoJackConnect, strNClientName ),
    iAudioInFader ( AUD_FADER_IN_MIDDLE ),
    bReverbOnLeftChan ( false ),
    iReverbLevel ( 0 ),
    iInputBoost ( 1 ),
    iSndCrdPrefFrameSizeFactor ( FRAME_SIZE_FACTOR_DEFAULT ),
    iSndCrdFrameSizeFactor ( FRAME_SIZE_FACTOR_DEFAULT ),
    bSndCrdConversionBufferRequired ( false ),
    iSndCardMonoBlockSizeSamConvBuff ( 0 ),
    iCaptureInputChannels ( 2 ),
    bFraSiFactPrefSupported ( false ),
    bFraSiFactDefSupported ( false ),
    bFraSiFactSafeSupported ( false ),
    eGUIDesign ( GD_DEFAULT ),
    eMeterStyle ( MT_LED_STRIPE ),
    bEnableAudioAlerts ( false ),
    bEnableOPUS64 ( false ),
    bJitterBufferOK ( true ),
    bMuteMeInPersonalMix ( bNMuteMeInPersonalMix ),
    iSocketQosNumber ( iQosNumber ),
    bDisableIPv6 ( bNDisableIPv6 ),
    iServerSockBufNumFrames ( DEF_NET_BUF_SIZE_NUM_BL ),
    bRawAudioIsSupported ( false )
{
    int iOpusError;

    OpusMode = opus_custom_mode_create ( SYSTEM_SAMPLE_RATE_HZ, DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES, &iOpusError );

    Opus64Mode = opus_custom_mode_create ( SYSTEM_SAMPLE_RATE_HZ, SYSTEM_FRAME_SIZE_SAMPLES, &iOpusError );

    // init audio encoders and decoders
    OpusDecoderMono     = opus_custom_decoder_create ( OpusMode, 1, &iOpusError );   // mono decoder legacy
    OpusDecoderStereo   = opus_custom_decoder_create ( OpusMode, 2, &iOpusError );   // stereo decoder legacy
    Opus64DecoderMono   = opus_custom_decoder_create ( Opus64Mode, 1, &iOpusError ); // mono decoder OPUS64
    Opus64DecoderStereo = opus_custom_decoder_create ( Opus64Mode, 2, &iOpusError ); // stereo decoder OPUS64

    vecAdvancedAudioChannels.append ( CAdvancedAudioChannelConfig ( ChannelInfo.strName, 0, 1 ) );

    // Connections -------------------------------------------------------------
    // connections for the protocol mechanism
    QObject::connect ( &Channel, &CChannel::JittBufSizeChanged, this, &CClient::OnJittBufSizeChanged );

    // The first ConClientListMesReceived handler performs the necessary cleanup and has to run first:
    QObject::connect ( &Channel, &CChannel::ConClientListMesReceived, this, &CClient::OnConClientListMesReceived );

    QObject::connect ( &Channel, &CChannel::Disconnected, this, &CClient::Disconnected );

    QObject::connect ( &Channel, &CChannel::NewConnection, this, &CClient::OnNewConnection );

    QObject::connect ( &Channel, &CChannel::ChatTextReceived, this, &CClient::ChatTextReceived );

    QObject::connect ( &Channel, &CChannel::ClientIDReceived, this, &CClient::OnClientIDReceived );

    QObject::connect ( &Channel, &CChannel::RawAudioSupported, this, &CClient::OnRawAudioSupported );

    QObject::connect ( &Channel, &CChannel::MuteStateHasChangedReceived, this, &CClient::OnMuteStateHasChangedReceived );

    QObject::connect ( &Channel, &CChannel::LicenceRequired, this, &CClient::LicenceRequired );

    QObject::connect ( &Channel, &CChannel::VersionAndOSReceived, this, &CClient::VersionAndOSReceived );

    QObject::connect ( &Channel, &CChannel::RecorderStateReceived, this, &CClient::RecorderStateReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLServerListReceived, this, &CClient::CLServerListReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLRedServerListReceived, this, &CClient::CLRedServerListReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLConnClientsListMesReceived, this, &CClient::CLConnClientsListMesReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLPingReceived, this, &CClient::OnCLPingReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLPingWithNumClientsReceived, this, &CClient::OnCLPingWithNumClientsReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLDisconnection, this, &CClient::OnCLDisconnection );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLVersionAndOSReceived, this, &CClient::CLVersionAndOSReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLChannelLevelListReceived, this, &CClient::OnCLChannelLevelListReceived );

    // other
    QObject::connect ( &Sound, &CSound::ReinitRequest, this, &CClient::OnSndCrdReinitRequest );

    QObject::connect ( &Sound, &CSound::ControllerInFaderLevel, this, &CClient::OnControllerInFaderLevel );

    QObject::connect ( &Sound, &CSound::ControllerInPanValue, this, &CClient::OnControllerInPanValue );

    QObject::connect ( &Sound, &CSound::ControllerInFaderIsSolo, this, &CClient::OnControllerInFaderIsSolo );

    QObject::connect ( &Sound, &CSound::ControllerInFaderIsMute, this, &CClient::OnControllerInFaderIsMute );

    QObject::connect ( &Sound, &CSound::ControllerInMuteMyself, this, &CClient::OnControllerInMuteMyself );

    QObject::connect ( &Socket, &CHighPrioSocket::InvalidPacketReceived, this, &CClient::OnInvalidPacketReceived );

    QObject::connect ( pSignalHandler, &CSignalHandler::HandledSignal, this, &CClient::OnHandledSignal );

    QObject::connect ( &Sound, &CSoundBase::MidiCCReceived, this, [this] ( int ccNumber ) { emit MidiCCReceived ( ccNumber ); } );

    // start timer so that elapsed time works
    PreciseTime.start();

    // set gain delay timer to single-shot and connect handler function
    TimerGainOrPan.setSingleShot ( true );

    QObject::connect ( &TimerGainOrPan, &QTimer::timeout, this, &CClient::OnTimerRemoteChanGainOrPan );

    // Start the primary socket only after all CClient signal connections exist.
    PrimaryAudioChannel.StartSocket();
}

// MIDI setup will be handled after settings are assigned
void CClient::SetSettings ( CClientSettings* settings )
{
    pSettings = settings;

    // Apply MIDI settings
    Sound.SetCtrlMIDIChannel ( pSettings->iMidiChannel );
    Sound.SetMIDIControllerMapping ( pSettings->iMidiFaderOffset,
                                     pSettings->bMidiFaderEnabled ? pSettings->iMidiFaderCount : 0,
                                     pSettings->iMidiPanOffset,
                                     pSettings->bMidiPanEnabled ? pSettings->iMidiPanCount : 0,
                                     pSettings->iMidiSoloOffset,
                                     pSettings->bMidiSoloEnabled ? pSettings->iMidiSoloCount : 0,
                                     pSettings->iMidiMuteOffset,
                                     pSettings->bMidiMuteEnabled ? pSettings->iMidiMuteCount : 0,
                                     pSettings->bMidiMuteMyselfEnabled ? pSettings->iMidiMuteMyself : -1 );
    if ( !pSettings->strMidiDevice.isEmpty() )
    {
        Sound.SetMIDIDevice ( pSettings->strMidiDevice );
    }
    Sound.EnableMIDI ( pSettings->bUseMIDIController );
}

CClient::~CClient()
{
    // if we were running, stop sound device
    if ( Sound.IsRunning() )
    {
        Sound.Stop();
    }

    StopAndDeleteAdditionalAudioChannels();

    // free audio decoders
    opus_custom_decoder_destroy ( OpusDecoderMono );
    opus_custom_decoder_destroy ( OpusDecoderStereo );
    opus_custom_decoder_destroy ( Opus64DecoderMono );
    opus_custom_decoder_destroy ( Opus64DecoderStereo );

    // free audio modes
    opus_custom_mode_destroy ( OpusMode );
    opus_custom_mode_destroy ( Opus64Mode );
}

void CClient::OnInvalidPacketReceived ( CHostAddress RecHostAddr )
{
    // message could not be parsed, check if the packet comes
    // from the server we just connected -> if yes, send
    // disconnect message since the server may not know that we
    // are not connected anymore
    if ( Channel.GetAddress() == RecHostAddr )
    {
        ConnLessProtocol.CreateCLDisconnection ( RecHostAddr );
    }
}

void CClient::OnJittBufSizeChanged ( int iNewJitBufSize )
{
    // we received a jitter buffer size changed message from the server,
    // only apply this value if auto jitter buffer size is enabled
    if ( GetDoAutoSockBufSize() )
    {
        // Note: Do not use the "SetServerSockBufNumFrames" function for setting
        // the new server jitter buffer size since then a message would be sent
        // to the server which is incorrect.
        iServerSockBufNumFrames = iNewJitBufSize;
    }
}

void CClient::OnNewConnection()
{
    // The reusable primary audio channel already sent its info and jitter
    // buffer settings. CClient additionally asks for mixer state.

    // We have to send a connected clients list request since it can happen
    // that we just had connected to the server and then disconnected but
    // the server still thinks that we are connected (the server is still
    // waiting for the channel time-out). If we now connect again, we would
    // not get the list because the server does not know about a new connection.
    // Same problem is with the jitter buffer message.
    Channel.CreateReqConnClientsList();

    //### TODO: BEGIN ###//
    // needed for compatibility to old servers >= 3.4.6 and <= 3.5.12
    Channel.CreateReqChannelLevelListMes();
    //### TODO: END ###//
}

void CClient::OnMuteStateHasChangedReceived ( int iServerChanID, bool bIsMuted )
{
    // map iChanID from server channel ID to client channel ID
    int iChanID = FindClientChannel ( iServerChanID, false );

    if ( iChanID != INVALID_INDEX )
    {
        emit MuteStateHasChangedReceived ( iChanID, bIsMuted );
    }
}

void CClient::OnCLChannelLevelListReceived ( CHostAddress InetAddr, CVector<uint16_t> vecLevelList )
{
    // reorder levels from server channel order to client channel order

    if ( ReorderLevelList ( vecLevelList ) )
    {
        emit CLChannelLevelListReceived ( InetAddr, vecLevelList );
    }
}

void CClient::OnConClientListMesReceived ( CVector<CChannelInfo> vecChanInfo )
{
    // translate from server channel IDs to client channel IDs
    // ALSO here is where we allocate and free client channels as required

    const int iNumConnectedClients = vecChanInfo.Size();
    int       i, iSrvIdx;

    // on a new connection, a server sends an empty channel list before sending
    // the real channel list (see #1010). To avoid this discarding "our" channel
    // that we have just created, we skip this processing and just pass the empty
    // list to the emitted signal.

    if ( iNumConnectedClients != 0 )
    {
        // this relies on the received client list being in order of server channel ID

        for ( i = 0, iSrvIdx = 0; i < iNumConnectedClients && iSrvIdx < MAX_NUM_CHANNELS; )
        {
            // server channel ID of this entry
            const int iServerChannelID = vecChanInfo[i].iChanID;

            // find matching client channel ID, creating new if necessary,
            // update channel number to be client-side
            vecChanInfo[i].iChanID = FindClientChannel ( iServerChannelID, true );

            // discard any lower server channels that are no longer in our local list
            while ( iSrvIdx < iServerChannelID )
            {
                const int iId = FindClientChannel ( iSrvIdx, false );

                if ( iId != INVALID_INDEX )
                {
                    // iSrvIdx contains a server channel number that has now gone
                    FreeClientChannel ( iSrvIdx );
                }
                iSrvIdx++;
            }

            i++;       // next list entry
            iSrvIdx++; // next local server channel
        }

        // have now run out of active channels, discard any remaining from our local list
        // note that iActiveChannels will reduce as remaining channels are freed

        while ( iActiveChannels > iNumConnectedClients && iSrvIdx < MAX_NUM_CHANNELS )
        {
            const int iId = FindClientChannel ( iSrvIdx, false );

            if ( iId != INVALID_INDEX )
            {
                // iSrvIdx contains a server channel number that has now gone
                FreeClientChannel ( iSrvIdx );
            }
            iSrvIdx++;
        }

        Q_ASSERT ( iActiveChannels == iNumConnectedClients );
    }

    // pass the received list onwards, now containing client channel IDs

    emit ConClientListMesReceived ( vecChanInfo );
}

void CClient::CreateServerJitterBufferMessage()
{
    PrimaryAudioChannel.CreateServerJitterBufferMessage();
}

void CClient::OnCLPingReceived ( CHostAddress InetAddr, int iMs )
{
    // make sure we are running and the server address is correct
    if ( IsRunning() && ( InetAddr == Channel.GetAddress() ) )
    {
        // take care of wrap arounds (if wrapping, do not use result)
        const int iCurDiff = EvaluatePingMessage ( iMs );
        if ( iCurDiff >= 0 )
        {
            iCurPingTime = iCurDiff; // store for use by gain message sending

            emit PingTimeReceived ( iCurDiff );
        }
    }
}

void CClient::OnCLPingWithNumClientsReceived ( CHostAddress InetAddr, int iMs, int iNumClients )
{
    // take care of wrap arounds (if wrapping, do not use result)
    const int iCurDiff = EvaluatePingMessage ( iMs );
    if ( iCurDiff >= 0 )
    {
        emit CLPingTimeWithNumClientsReceived ( InetAddr, iCurDiff, iNumClients );
    }
}

int CClient::PreparePingMessage()
{
    // transmit the current precise time (in ms)
    return PreciseTime.elapsed();
}

int CClient::EvaluatePingMessage ( const int iMs )
{
    // calculate difference between received time in ms and current time in ms
    return PreciseTime.elapsed() - iMs;
}

void CClient::SetDoAutoSockBufSize ( const bool bValue )
{
    Channel.SetDoAutoSockBufSize ( bValue );
    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        pAudioChannel->SetSocketBufferSettings ( pAudioChannel->GetChannel().GetSockBufNumFrames(),
                                                  iServerSockBufNumFrames,
                                                  bValue );
    }

    CreateServerJitterBufferMessage();
}

void CClient::SetSockBufNumFrames ( const int iNumBlocks, const bool bPreserve )
{
    Channel.SetSockBufNumFrames ( iNumBlocks, bPreserve );
    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        pAudioChannel->SetSocketBufferSettings ( iNumBlocks, iServerSockBufNumFrames, GetDoAutoSockBufSize() );
    }
}

void CClient::SetServerSockBufNumFrames ( const int iNumBlocks )
{
    iServerSockBufNumFrames = iNumBlocks;
    if ( !GetDoAutoSockBufSize() )
    {
        CreateServerJitterBufferMessage();
    }

    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        pAudioChannel->SetSocketBufferSettings ( pAudioChannel->GetChannel().GetSockBufNumFrames(),
                                                  iServerSockBufNumFrames,
                                                  GetDoAutoSockBufSize() );
    }
}

int CClient::GetUploadRateKbps()
{
    int iUploadRateKbps = PrimaryAudioChannel.GetUploadRateKbps();
    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        iUploadRateKbps += pAudioChannel->GetUploadRateKbps();
    }
    return iUploadRateKbps;
}

void CClient::StopAndDeleteAdditionalAudioChannels()
{
    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        pAudioChannel->Disconnect();
        delete pAudioChannel;
    }
    vecAdditionalAudioChannels.clear();
}

void CClient::RebuildAdditionalAudioChannels()
{
    const int iRequiredAdditionalChannels = eAudioChannelConf == CC_ADVANCED ? qMax ( 0, vecAdvancedAudioChannels.size() - 1 ) : 0;

    while ( vecAdditionalAudioChannels.size() > iRequiredAdditionalChannels )
    {
        CClientAudioChannel* pAudioChannel = vecAdditionalAudioChannels.takeLast();
        pAudioChannel->Disconnect();
        delete pAudioChannel;
    }

    while ( vecAdditionalAudioChannels.size() < iRequiredAdditionalChannels )
    {
        CClientAudioChannel* pAudioChannel = new CClientAudioChannel ( 0, iSocketQosNumber, bDisableIPv6 );
        pAudioChannel->GetChannel().SetAddress ( Channel.GetAddress() );
        QObject::connect ( &pAudioChannel->GetChannel(), &CChannel::RawAudioSupported, this, &CClient::OnRawAudioSupported );
        pAudioChannel->StartSocket();
        vecAdditionalAudioChannels.append ( pAudioChannel );
    }
}

int CClient::GetCodedBytesForAudioChannels ( const int iNumChannels, bool& bUseRawAudio ) const
{
    bUseRawAudio = ( eAudioQuality == AQ_RAW ) && bRawAudioIsSupported;
    if ( bUseRawAudio )
    {
        return static_cast<int> ( sizeof ( int16_t ) ) * iNumChannels * iOPUSFrameSizeSamples;
    }

    const EAudioQuality eEffectiveQuality = eAudioQuality == AQ_RAW ? AQ_HIGH : eAudioQuality;
    const bool bDoubleFrame = eAudioCompressionType == CT_OPUS;

    if ( iNumChannels == 1 )
    {
        switch ( eEffectiveQuality )
        {
        case AQ_LOW:
            return bDoubleFrame ? OPUS_NUM_BYTES_MONO_LOW_QUALITY_DBLE_FRAMESIZE : OPUS_NUM_BYTES_MONO_LOW_QUALITY;
        case AQ_NORMAL:
            return bDoubleFrame ? OPUS_NUM_BYTES_MONO_NORMAL_QUALITY_DBLE_FRAMESIZE : OPUS_NUM_BYTES_MONO_NORMAL_QUALITY;
        case AQ_HIGH:
        case AQ_RAW:
            return bDoubleFrame ? OPUS_NUM_BYTES_MONO_HIGH_QUALITY_DBLE_FRAMESIZE : OPUS_NUM_BYTES_MONO_HIGH_QUALITY;
        }
    }

    switch ( eEffectiveQuality )
    {
    case AQ_LOW:
        return bDoubleFrame ? OPUS_NUM_BYTES_STEREO_LOW_QUALITY_DBLE_FRAMESIZE : OPUS_NUM_BYTES_STEREO_LOW_QUALITY;
    case AQ_NORMAL:
        return bDoubleFrame ? OPUS_NUM_BYTES_STEREO_NORMAL_QUALITY_DBLE_FRAMESIZE : OPUS_NUM_BYTES_STEREO_NORMAL_QUALITY;
    case AQ_HIGH:
    case AQ_RAW:
        return bDoubleFrame ? OPUS_NUM_BYTES_STEREO_HIGH_QUALITY_DBLE_FRAMESIZE : OPUS_NUM_BYTES_STEREO_HIGH_QUALITY;
    }

    return OPUS_NUM_BYTES_MONO_NORMAL_QUALITY;
}

void CClient::ConfigureAudioChannels()
{
    RebuildAdditionalAudioChannels();

    OpusCustomMode* pOpusMode = eAudioCompressionType == CT_OPUS ? OpusMode : Opus64Mode;
    bool bUseRawAudio = false;
    iCeltNumCodedBytes = GetCodedBytesForAudioChannels ( iNumAudioChannels, bUseRawAudio );

    PrimaryAudioChannel.Configure ( eAudioCompressionType,
                                    iCeltNumCodedBytes,
                                    iOPUSFrameSizeSamples,
                                    iSndCrdFrameSizeFactor,
                                    iNumAudioChannels,
                                    pOpusMode,
                                    bUseRawAudio,
                                    Channel.GetSockBufNumFrames(),
                                    iServerSockBufNumFrames,
                                    GetDoAutoSockBufSize() );
    PrimaryAudioChannel.SetChannelInfo ( GetChannelInfoForAdvancedSource ( 0 ) );

    for ( int i = 0; i < vecAdditionalAudioChannels.size(); ++i )
    {
        const CAdvancedAudioChannelConfig& Source = vecAdvancedAudioChannels[i + 1];
        const int iSourceNumAudioChannels = Source.iInputChannel2 == INVALID_INDEX ? 1 : 2;
        bool bSourceUseRawAudio = false;
        const int iSourceCodedBytes = GetCodedBytesForAudioChannels ( iSourceNumAudioChannels, bSourceUseRawAudio );
        CClientAudioChannel* pAudioChannel = vecAdditionalAudioChannels[i];

        pAudioChannel->GetChannel().SetAddress ( Channel.GetAddress() );
        pAudioChannel->Configure ( eAudioCompressionType,
                                   iSourceCodedBytes,
                                   iOPUSFrameSizeSamples,
                                   iSndCrdFrameSizeFactor,
                                   iSourceNumAudioChannels,
                                   pOpusMode,
                                   bSourceUseRawAudio,
                                   Channel.GetSockBufNumFrames(),
                                   iServerSockBufNumFrames,
                                   GetDoAutoSockBufSize() );
        pAudioChannel->SetChannelInfo ( GetChannelInfoForAdvancedSource ( i + 1 ) );
        if ( Channel.IsEnabled() )
        {
            pAudioChannel->SetEnabled ( true );
        }
    }
}

void CClient::SetRemoteInfo()
{
    PrimaryAudioChannel.SetChannelInfo ( GetChannelInfoForAdvancedSource ( 0 ) );
    UpdateAdvancedChannelInfos();
}

CChannelCoreInfo CClient::GetChannelInfoForAdvancedSource ( const int iSourceIndex ) const
{
    CChannelCoreInfo ChannelInfoForSource = ChannelInfo;
    if ( ( eAudioChannelConf == CC_ADVANCED ) && ( iSourceIndex >= 0 ) && ( iSourceIndex < vecAdvancedAudioChannels.size() ) &&
         !vecAdvancedAudioChannels[iSourceIndex].strFaderTag.isEmpty() )
    {
        ChannelInfoForSource.strName = TruncateString ( vecAdvancedAudioChannels[iSourceIndex].strFaderTag, MAX_LEN_FADER_TAG );
    }
    return ChannelInfoForSource;
}

void CClient::UpdateAdvancedChannelInfos()
{
    if ( eAudioChannelConf != CC_ADVANCED )
    {
        return;
    }

    for ( int i = 0; i < vecAdditionalAudioChannels.size(); ++i )
    {
        vecAdditionalAudioChannels[i]->SetChannelInfo ( GetChannelInfoForAdvancedSource ( i + 1 ) );
    }
}

void CClient::SetAdvancedAudioChannels ( const QVector<CAdvancedAudioChannelConfig>& vecNewChannels )
{
    QVector<CAdvancedAudioChannelConfig> vecChannels;
    for ( const CAdvancedAudioChannelConfig& ChannelConfig : vecNewChannels )
    {
        if ( vecChannels.size() >= MAX_NUM_CHANNELS )
        {
            break;
        }
        if ( ( ChannelConfig.iInputChannel1 < 0 ) || ( ChannelConfig.iInputChannel1 >= MAX_NUM_IN_OUT_CHANNELS ) ||
             ( ( ChannelConfig.iInputChannel2 != INVALID_INDEX ) &&
               ( ( ChannelConfig.iInputChannel2 < 0 ) || ( ChannelConfig.iInputChannel2 >= MAX_NUM_IN_OUT_CHANNELS ) ||
                 ( ChannelConfig.iInputChannel2 == ChannelConfig.iInputChannel1 ) ) ) )
        {
            continue;
        }
        QString strFaderTag = ChannelConfig.strFaderTag.trimmed();
        if ( strFaderTag.isEmpty() )
        {
            strFaderTag = ChannelInfo.strName;
            if ( !vecChannels.isEmpty() )
            {
                strFaderTag += QString ( " %1" ).arg ( vecChannels.size() + 1 );
            }
        }
        vecChannels.append ( CAdvancedAudioChannelConfig ( TruncateString ( strFaderTag, MAX_LEN_FADER_TAG ),
                                                           ChannelConfig.iInputChannel1,
                                                           ChannelConfig.iInputChannel2 ) );
    }

    if ( vecChannels.isEmpty() )
    {
        vecChannels.append ( CAdvancedAudioChannelConfig ( ChannelInfo.strName, Sound.GetLeftInputChannel(), Sound.GetRightInputChannel() ) );
    }

    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    vecAdvancedAudioChannels = vecChannels;
    eAudioChannelConf        = CC_ADVANCED;
    Init();

    if ( bWasRunning )
    {
        Sound.Start();
    }
}

// In order not to flood the server with gain or pan change messages, particularly when using
// a MIDI controller, a timer is used to limit the rate at which such messages are generated.
// This avoids a potential long backlog of messages, since each must be ACKed before the next
// can be sent, and this ACK is subject to the latency of the server connection.
//
// When the first gain or pan change message is requested after an idle period (i.e. the timer is not
// running), it will be sent immediately, and a timer started. The timer period is dependent on
// the current ping time to the remote server.
//
// If a gain or pan change message is requested while the timer is still running, the new value is not sent,
// but just stored in newGain or newPan within clientChannels[iId], and the minGainOrPanId and maxGainOrPanId
// updated to note the range of IDs that must be checked when the time expires (this will usually be a single
// channel unless channel grouping is being used). This avoids having to check all possible channels.
//
// When the timer fires, the channels minGainOrPanId <= iId < maxGainOrPanId are checked by comparing the
// last sent values in oldGain or oldPan with any pending values in newGain or newPan, and if they differ,
// the new value is sent, updating oldGain or oldPan with the sent value. If any new values are sent,
// the timer is restarted so that further immediate updates will be pended.

void CClient::SetRemoteChanGain ( const int iId, const float fGain, const bool bIsMyOwnFader )
{
    QMutexLocker locker ( &MutexGainOrPan );

    CClientChannel* clientChan = &clientChannels[iId];

    // if this gain is for my own channel, apply the value for the Mute Myself function
    if ( bIsMyOwnFader )
    {
        fMuteOutStreamGain = fGain;
    }

    if ( TimerGainOrPan.isActive() )
    {
        // just update the new value for sending later;
        // will compare with oldGain when the timer fires
        clientChan->newGain = fGain;

        // update range of channel IDs to check in the timer
        if ( iId < minGainOrPanId )
            minGainOrPanId = iId; // first value to check
        if ( iId >= maxGainOrPanId )
            maxGainOrPanId = iId + 1; // first value NOT to check

        return;
    }

    // here the timer was not active:
    // send the actual gain and reset the range of channel IDs to empty
    clientChan->oldGain = clientChan->newGain = fGain;
    Channel.SetRemoteChanGain ( clientChan->iServerChannelID, fGain ); // translate client channel to server channel ID

    StartTimerGainOrPan();
}

void CClient::OnTimerRemoteChanGainOrPan()
{
    QMutexLocker locker ( &MutexGainOrPan );
    bool         bSent = false;

    for ( int iId = minGainOrPanId; iId < maxGainOrPanId; iId++ )
    {
        CClientChannel* clientChan = &clientChannels[iId];

        if ( clientChan->newGain != clientChan->oldGain )
        {
            // send new gain and record as old gain
            float fGain = clientChan->oldGain = clientChan->newGain;
            Channel.SetRemoteChanGain ( clientChan->iServerChannelID, fGain ); // translate client channel to server channel ID
            bSent = true;
        }

        if ( clientChan->newPan != clientChan->oldPan )
        {
            // send new pan and record as old pan
            float fPan = clientChan->oldPan = clientChan->newPan;
            Channel.SetRemoteChanPan ( clientChan->iServerChannelID, fPan ); // translate client channel to server channel ID
            bSent = true;
        }
    }

    // if a new gain or pan has been sent, reset the range of channel IDs to empty and start timer
    if ( bSent )
    {
        StartTimerGainOrPan();
    }
}

// reset the range of channel IDs to check and start the delay timer
void CClient::StartTimerGainOrPan()
{
    maxGainOrPanId = 0;
    minGainOrPanId = MAX_NUM_CHANNELS;

    // start timer to delay sending further updates
    // use longer delay when connected to server with higher ping time,
    // double the ping time in order to allow a bit of overhead for other messages
    if ( iCurPingTime < DEFAULT_GAIN_DELAY_PERIOD_MS / 2 )
    {
        TimerGainOrPan.start ( DEFAULT_GAIN_DELAY_PERIOD_MS );
    }
    else
    {
        TimerGainOrPan.start ( iCurPingTime * 2 );
    }
}

void CClient::SetRemoteChanPan ( const int iId, const float fPan )
{
    QMutexLocker locker ( &MutexGainOrPan );

    CClientChannel* clientChan = &clientChannels[iId];

    if ( TimerGainOrPan.isActive() )
    {
        // just update the new value for sending later;
        // will compare with oldPan when the timer fires
        clientChan->newPan = fPan;

        // update range of channel IDs to check in the timer
        if ( iId < minGainOrPanId )
            minGainOrPanId = iId; // first value to check
        if ( iId >= maxGainOrPanId )
            maxGainOrPanId = iId + 1; // first value NOT to check

        return;
    }

    // here the timer was not active:
    // send the actual gain and reset the range of channel IDs to empty
    clientChan->oldPan = clientChan->newPan = fPan;
    Channel.SetRemoteChanPan ( clientChan->iServerChannelID, fPan ); // translate client channel to server channel ID

    StartTimerGainOrPan();
}

bool CClient::SetServerAddr ( QString strNAddr )
{
    CHostAddress HostAddress;
    if ( NetworkUtil::ParseNetworkAddress ( strNAddr, HostAddress, PrimaryAudioChannel.IPv6AvailabilityReference() ) )
    {
        Channel.SetAddress ( HostAddress );
        for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
        {
            pAudioChannel->GetChannel().SetAddress ( HostAddress );
        }
        return true;
    }

    return false; // invalid address
}

bool CClient::GetAndResetbJitterBufferOKFlag()
{
    // get the socket buffer put status flag and reset it
    const bool bSocketJitBufOKFlag = Socket.GetAndResetbJitterBufferOKFlag();

    if ( !bJitterBufferOK )
    {
        // our jitter buffer get status is not OK so the overall status of the
        // jitter buffer is also not OK (we do not have to consider the status
        // of the socket buffer put status flag)

        // reset flag before returning the function
        bJitterBufferOK = true;
        return false;
    }

    // the jitter buffer get (our own status flag) is OK, the final status
    // now depends on the jitter buffer put status flag from the socket
    // since per definition the jitter buffer status is OK if both the
    // put and get status are OK
    return bSocketJitBufOKFlag;
}

void CClient::SetSndCrdPrefFrameSizeFactor ( const int iNewFactor )
{
    // first check new input parameter
    if ( ( iNewFactor == FRAME_SIZE_FACTOR_PREFERRED ) || ( iNewFactor == FRAME_SIZE_FACTOR_DEFAULT ) || ( iNewFactor == FRAME_SIZE_FACTOR_SAFE ) )
    {
        // init with new parameter, if client was running then first
        // stop it and restart again after new initialization
        const bool bWasRunning = Sound.IsRunning();
        if ( bWasRunning )
        {
            Sound.Stop();
        }

        // set new parameter
        iSndCrdPrefFrameSizeFactor = iNewFactor;

        // init with new block size index parameter
        Init();

        if ( bWasRunning )
        {
            // restart client
            Sound.Start();
        }
    }
}

void CClient::SetEnableOPUS64 ( const bool eNEnableOPUS64 )
{
    // init with new parameter, if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    // set new parameter
    bEnableOPUS64 = eNEnableOPUS64;
    Init();

    if ( bWasRunning )
    {
        Sound.Start();
    }
}

void CClient::SetAudioQuality ( const EAudioQuality eNAudioQuality )
{
    // init with new parameter, if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    // set new parameter
    eAudioQuality = eNAudioQuality;
    Init();

    if ( bWasRunning )
    {
        Sound.Start();
    }
}

void CClient::SetAudioChannels ( const EAudChanConf eNAudChanConf )
{
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    eAudioChannelConf = eNAudChanConf;
    if ( ( eAudioChannelConf == CC_ADVANCED ) && vecAdvancedAudioChannels.isEmpty() )
    {
        vecAdvancedAudioChannels.append ( CAdvancedAudioChannelConfig ( ChannelInfo.strName,
                                                                          Sound.GetLeftInputChannel(),
                                                                          Sound.GetRightInputChannel() ) );
    }
    Init();

    if ( bWasRunning )
    {
        Sound.Start();
    }
}

QString CClient::SetSndCrdDev ( const QString strNewDev )
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    const QString strError = Sound.SetDev ( strNewDev );

    // init again because the sound card actual buffer size might
    // be changed on new device
    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }

    // in case of an error inform the GUI about it
    if ( !strError.isEmpty() )
    {
        emit SoundDeviceChanged ( strError );
    }

    return strError;
}

void CClient::SetSndCrdLeftInputChannel ( const int iNewChan )
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    Sound.SetLeftInputChannel ( iNewChan );
    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }
}

void CClient::SetSndCrdRightInputChannel ( const int iNewChan )
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    Sound.SetRightInputChannel ( iNewChan );
    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }
}

void CClient::SetSndCrdLeftOutputChannel ( const int iNewChan )
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    Sound.SetLeftOutputChannel ( iNewChan );
    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }
}

void CClient::SetSndCrdRightOutputChannel ( const int iNewChan )
{
    // if client was running then first
    // stop it and restart again after new initialization
    const bool bWasRunning = Sound.IsRunning();
    if ( bWasRunning )
    {
        Sound.Stop();
    }

    Sound.SetRightOutputChannel ( iNewChan );
    Init();

    if ( bWasRunning )
    {
        // restart client
        Sound.Start();
    }
}

void CClient::OnSndCrdReinitRequest ( int iSndCrdResetType )
{
    QString strError = "";

    // audio device notifications can come at any time and they are in a
    // different thread, therefore we need a mutex here
    MutexDriverReinit.lock();
    {
        // in older QT versions, enums cannot easily be used in signals without
        // registering them -> workaroud: we use the int type and cast to the enum
        const ESndCrdResetType eSndCrdResetType = static_cast<ESndCrdResetType> ( iSndCrdResetType );

        // if client was running then first
        // stop it and restart again after new initialization
        const bool bWasRunning = Sound.IsRunning();
        if ( bWasRunning )
        {
            Sound.Stop();
        }

        // perform reinit request as indicated by the request type parameter
        if ( eSndCrdResetType != RS_ONLY_RESTART )
        {
            if ( eSndCrdResetType != RS_ONLY_RESTART_AND_INIT )
            {
                // reinit the driver if requested
                // (we use the currently selected driver)
                strError = Sound.SetDev ( Sound.GetDev() );
            }

            // init client object (must always be performed if the driver
            // was changed)
            Init();
        }

        if ( bWasRunning )
        {
            // restart client
            Sound.Start();
        }
    }
    MutexDriverReinit.unlock();

    // inform GUI about the sound card device change
    emit SoundDeviceChanged ( strError );
}

void CClient::OnHandledSignal ( int sigNum )
{
#ifdef _WIN32
    // Windows does not actually get OnHandledSignal triggered
    QCoreApplication::instance()->exit();
    Q_UNUSED ( sigNum )
#else
    switch ( sigNum )
    {
    case SIGINT:
    case SIGTERM:
        // if connected, terminate connection (needed for headless mode)
        if ( IsRunning() )
        {
            Stop();
        }

        // this should trigger OnAboutToQuit
        QCoreApplication::instance()->exit();
        break;

    default:
        break;
    }
#endif
}

void CClient::OnControllerInFaderLevel ( int iChannelIdx, int iValue )
{
    // in case of a headless client the faders cannot be moved so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // only apply new fader level if channel index is valid
    if ( ( iChannelIdx >= 0 ) && ( iChannelIdx < MAX_NUM_CHANNELS ) )
    {
        SetRemoteChanGain ( iChannelIdx, MathUtils::CalcFaderGain ( iValue ), false );
    }
#endif

    emit ControllerInFaderLevel ( iChannelIdx, iValue );
}

void CClient::OnControllerInPanValue ( int iChannelIdx, int iValue )
{
    // in case of a headless client the panners cannot be moved so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // channel index is valid
    SetRemoteChanPan ( iChannelIdx, static_cast<float> ( iValue ) / AUD_MIX_PAN_MAX );
#endif

    emit ControllerInPanValue ( iChannelIdx, iValue );
}

void CClient::OnControllerInFaderIsSolo ( int iChannelIdx, bool bIsSolo )
{
    // in case of a headless client the buttons are not displayed so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // FIXME: no idea what to do here.
#endif

    emit ControllerInFaderIsSolo ( iChannelIdx, bIsSolo );
}

void CClient::OnControllerInFaderIsMute ( int iChannelIdx, bool bIsMute )
{
    // in case of a headless client the buttons are not displayed so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // FIXME: no idea what to do here.
#endif

    emit ControllerInFaderIsMute ( iChannelIdx, bIsMute );
}

void CClient::OnControllerInMuteMyself ( bool bMute )
{
    // in case of a headless client the buttons are not displayed so we need
    // to send the controller information directly to the server
#ifdef HEADLESS
    // FIXME: no idea what to do here.
#endif

    emit ControllerInMuteMyself ( bMute );
}

void CClient::OnClientIDReceived ( int iServerChanID )
{
    // if we have just connected to a running server, iActiveChannels will be 0
    // if iActiveChannels is not 0, the server must have been restarted on the fly
    // in that case, channels might have changed, so clear our list to get it afresh.
    if ( iActiveChannels != 0 )
    {
        qInfo() << "> Server restarted?";
        ClearClientChannels();
    }

    // allocate and map client-side channel 0
    int iChanID = FindClientChannel ( iServerChanID, true ); // should always return channel 0

    // for headless mode we support to mute our own signal in the personal mix
    // (note that the check for headless is done in the main.cpp and must not
    // be checked here)
    if ( bMuteMeInPersonalMix )
    {
        SetRemoteChanGain ( iChanID, 0, false );
    }

    emit ClientIDReceived ( iChanID );
}

void CClient::OnRawAudioSupported()
{
    if ( !bRawAudioIsSupported )
    {
        const bool bWasRunning = Sound.IsRunning();

        if ( bWasRunning )
        {
            Sound.Stop();
        }

        bRawAudioIsSupported = true;
        Init();

        if ( bWasRunning )
        {
            Sound.Start();
        }
    }
}

void CClient::Start()
{
    Init();
    ClearClientChannels();
    SetRemoteInfo();

    PrimaryAudioChannel.SetEnabled ( true );
    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        pAudioChannel->SetEnabled ( true );
    }

    Sound.Start();

#if defined( Q_OS_WINDOWS )
    SetThreadExecutionState ( ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED );
#endif
}

void CClient::Stop()
{
    Sound.Stop();

    // Each Advanced source is a separate server participant and must receive
    // its own connectionless disconnection message.
    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        pAudioChannel->Disconnect();
    }
    PrimaryAudioChannel.SetEnabled ( false );

    // Fall back to Opus in case Raw was used.  Keep the route configuration so
    // a subsequent connection restores the same table.
    bRawAudioIsSupported = false;
    Init();

    QTime DieTime = QTime::currentTime().addMSecs ( 100 );
    while ( QTime::currentTime() < DieTime )
    {
        QCoreApplication::processEvents ( QEventLoop::ExcludeUserInputEvents, 100 );
    }

    ConnLessProtocol.CreateCLDisconnection ( Channel.GetAddress() );

    bJitterBufferOK = true;
    SignalLevelMeter.Reset();

#if defined( Q_OS_WINDOWS )
    SetThreadExecutionState ( ES_CONTINUOUS );
#endif
}

void CClient::Init()
{
    // check if possible frame size factors are supported
    const int iFraSizePreffered = SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_PREFERRED;
    const int iFraSizeDefault   = SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_DEFAULT;
    const int iFraSizeSafe      = SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_SAFE;

#if defined( Q_OS_IOS )
    bFraSiFactPrefSupported = true; // to reduce sound init time, because we know it's supported in iOS
    bFraSiFactDefSupported  = true;
    bFraSiFactSafeSupported = true;
#else
    bFraSiFactPrefSupported = ( Sound.Init ( iFraSizePreffered ) == iFraSizePreffered );
    bFraSiFactDefSupported  = ( Sound.Init ( iFraSizeDefault ) == iFraSizeDefault );
    bFraSiFactSafeSupported = ( Sound.Init ( iFraSizeSafe ) == iFraSizeSafe );
#endif

    // translate block size index in actual block size
    const int iPrefMonoFrameSize = iSndCrdPrefFrameSizeFactor * SYSTEM_FRAME_SIZE_SAMPLES;

    // get actual sound card buffer size using preferred size
    // TODO - iOS needs 1 init only, now: 9 inits at launch <- slow
    // Initially, I tried to fix this as follows (inside #ifdef ios tag):
    //    if ( Sound.isInitialized )
    //      iMonoBlockSizeSam = iPrefMonoFrameSize;
    //    else
    //      iMonoBlockSizeSam = Sound.Init ( iPrefMonoFrameSize );
    // Problem is legitimate setting changes (buffer size for example).
    // so the condition should be something like "if ( Sound.isInitialized and APP_IS_INIALIZING)"
    iMonoBlockSizeSam = Sound.Init ( iPrefMonoFrameSize );

    // Calculate the current sound card frame size factor. In case
    // the current mono block size is not a multiple of the system
    // frame size, we have to use a sound card conversion buffer.
    if ( ( ( iMonoBlockSizeSam == ( SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_PREFERRED ) ) && bEnableOPUS64 ) ||
         ( iMonoBlockSizeSam == ( SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_DEFAULT ) ) ||
         ( iMonoBlockSizeSam == ( SYSTEM_FRAME_SIZE_SAMPLES * FRAME_SIZE_FACTOR_SAFE ) ) )
    {
        // regular case: one of our predefined buffer sizes is available
        iSndCrdFrameSizeFactor = iMonoBlockSizeSam / SYSTEM_FRAME_SIZE_SAMPLES;

        // no sound card conversion buffer required
        bSndCrdConversionBufferRequired = false;
    }
    else
    {
        // An unsupported sound card buffer size is currently used -> we have
        // to use a conversion buffer. Per definition we use the smallest buffer
        // size as the current frame size.

        // store actual sound card buffer size (stereo)
        bSndCrdConversionBufferRequired  = true;
        iSndCardMonoBlockSizeSamConvBuff = iMonoBlockSizeSam;

        // overwrite block size factor by using one frame
        iSndCrdFrameSizeFactor = 1;
    }

    // select the OPUS frame size mode depending on current mono block size samples
    if ( bSndCrdConversionBufferRequired )
    {
        if ( ( iSndCardMonoBlockSizeSamConvBuff < DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES ) && bEnableOPUS64 )
        {
            iMonoBlockSizeSam     = SYSTEM_FRAME_SIZE_SAMPLES;
            eAudioCompressionType = CT_OPUS64;
        }
        else
        {
            iMonoBlockSizeSam     = DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES;
            eAudioCompressionType = CT_OPUS;
        }
    }
    else
    {
        if ( iMonoBlockSizeSam < DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES )
        {
            eAudioCompressionType = CT_OPUS64;
        }
        else
        {
            // since we use double size frame size for OPUS, we have to adjust the frame size factor
            iSndCrdFrameSizeFactor /= 2;
            eAudioCompressionType = CT_OPUS;
        }
    }

    // Configure the primary receive stream. In Advanced mode the first row is
    // the primary stream; it receives the personal mix and may itself be mono.
    const bool bPrimaryMono = ( eAudioChannelConf == CC_MONO ) ||
                              ( ( eAudioChannelConf == CC_ADVANCED ) && !vecAdvancedAudioChannels.isEmpty() &&
                                ( vecAdvancedAudioChannels[0].iInputChannel2 == INVALID_INDEX ) );
    iNumAudioChannels = bPrimaryMono ? 1 : 2;

    if ( eAudioCompressionType == CT_OPUS )
    {
        iOPUSFrameSizeSamples = DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES;
        CurOpusDecoder        = bPrimaryMono ? OpusDecoderMono : OpusDecoderStereo;
    }
    else // CT_OPUS64
    {
        iOPUSFrameSizeSamples = SYSTEM_FRAME_SIZE_SAMPLES;
        CurOpusDecoder        = bPrimaryMono ? Opus64DecoderMono : Opus64DecoderStereo;
    }

    bool bUseRawAudio = false;
    iCeltNumCodedBytes = GetCodedBytesForAudioChannels ( iNumAudioChannels, bUseRawAudio );
    if ( bUseRawAudio )
    {
        CurOpusDecoder = nullptr;
    }

    // calculate stereo (two channels) buffer size
    iStereoBlockSizeSam = 2 * iMonoBlockSizeSam;

    vecZeros.Init ( iStereoBlockSizeSam, 0 );
    vecsStereoSndCrdMuteStream.Init ( iStereoBlockSizeSam );
    vecPrimaryInputAudio.Init ( iStereoBlockSizeSam );
    vecbyNetwData.Init ( iCeltNumCodedBytes );

    // Configure the common transport/codec implementation for the primary
    // stream and every independently advertised Advanced row.
    ConfigureAudioChannels();

    // Advanced routing intentionally bypasses the legacy input pan/reverb
    // stage: rows are independent faders at the server, not a local mix.
    AudioReverb.Init ( eAudioChannelConf == CC_ADVANCED ? CC_STEREO : eAudioChannelConf,
                       iStereoBlockSizeSam,
                       SYSTEM_SAMPLE_RATE_HZ );

    // The sound driver supplies every selectable capture source. Keep a
    // separate conversion queue for it because the output path is always
    // stereo whereas the capture path can contain many channels.
    iCaptureInputChannels = qMax ( 2, Sound.GetNumInputChannels() );

    if ( bSndCrdConversionBufferRequired )
    {
        // The conversion buffers retain the same number of input frames as
        // output frames, but the input queue is interleaved by all sources.
        const int iConBufInputSize = ( iMonoBlockSizeSam + iSndCardMonoBlockSizeSamConvBuff ) * iCaptureInputChannels;
        const int iConBufOutputSize = iStereoBlockSizeSam + 2 * iSndCardMonoBlockSizeSamConvBuff;

        SndCrdConversionBufferIn.Init ( iConBufInputSize );
        SndCrdConversionBufferOut.Init ( iConBufOutputSize );
        vecInputDataConvBuf.Init ( iMonoBlockSizeSam * iCaptureInputChannels );
        vecCapturedInputFallback.Init ( iConBufInputSize );
        vecDataConvBuf.Init ( iStereoBlockSizeSam );

        // The output conversion buffer must be filled with the inner block
        // size for initialization to avoid an underrun.
        SndCrdConversionBufferOut.Put ( vecZeros, iStereoBlockSizeSam );
    }

    // reset initialization phase flag and mute flag
    bIsInitializationPhase = true;
}

void CClient::AudioCallback ( CVector<int16_t>& psData, void* arg )
{
    // get the pointer to the object
    CClient* pMyClientObj = static_cast<CClient*> ( arg );

    // process audio data
    pMyClientObj->ProcessSndCrdAudioData ( psData );

    //### TEST: BEGIN ###//
    // do a soundcard jitter measurement
    /*
    static CTimingMeas JitterMeas ( 1000, "test2.dat" );
    JitterMeas.Measure();
    */
    //### TEST: END ###//
}

void CClient::ProcessSndCrdAudioData ( CVector<int16_t>& vecsStereoSndCrd )
{
    const CVector<int16_t>& vecCapturedInput = Sound.GetCapturedInputAudio();
    const int iCapturedChannels = Sound.GetCapturedInputChannels() > 0 ? Sound.GetCapturedInputChannels() : 2;

    if ( bSndCrdConversionBufferRequired )
    {
        // The capture queue is wider than the stereo return queue.  Drivers
        // provide a complete interleaved capture for the current callback.
        if ( vecCapturedInput.Size() == vecsStereoSndCrd.Size() / 2 * iCaptureInputChannels )
        {
            SndCrdConversionBufferIn.Put ( vecCapturedInput, vecCapturedInput.Size() );
        }
        else
        {
            // A driver reporting an incompatible capture layout should not
            // corrupt audio. Expand the legacy stereo input into the complete
            // capture frame width before placing it in the conversion queue.
            const int iNumInputFrames = vecsStereoSndCrd.Size() / 2;
            const int iFallbackSize   = iNumInputFrames * iCaptureInputChannels;
            vecCapturedInputFallback.Reset ( 0 );
            for ( int i = 0; i < iNumInputFrames; ++i )
            {
                vecCapturedInputFallback[i * iCaptureInputChannels] = vecsStereoSndCrd[2 * i];
                if ( iCaptureInputChannels > 1 )
                {
                    vecCapturedInputFallback[i * iCaptureInputChannels + 1] = vecsStereoSndCrd[2 * i + 1];
                }
            }
            SndCrdConversionBufferIn.Put ( vecCapturedInputFallback, iFallbackSize );
        }

        const int iInputBlockSize = iMonoBlockSizeSam * iCaptureInputChannels;
        while ( SndCrdConversionBufferIn.GetAvailData() >= iInputBlockSize )
        {
            SndCrdConversionBufferIn.Get ( vecInputDataConvBuf, iInputBlockSize );
            vecDataConvBuf.Reset ( 0 );
            ProcessAudioDataIntern ( vecDataConvBuf, vecInputDataConvBuf, iCaptureInputChannels );
            SndCrdConversionBufferOut.Put ( vecDataConvBuf, iStereoBlockSizeSam );
        }

        SndCrdConversionBufferOut.Get ( vecsStereoSndCrd, vecsStereoSndCrd.Size() );
    }
    else
    {
        ProcessAudioDataIntern ( vecsStereoSndCrd, vecCapturedInput, iCapturedChannels );
    }
}

void CClient::FillAudioSourceBuffer ( CClientAudioChannel&                  AudioChannel,
                                      const CAdvancedAudioChannelConfig&     Source,
                                      const CVector<int16_t>&                vecInputAudio,
                                      const int                               iInputChannels )
{
    CVector<int16_t>& vecAudioData = AudioChannel.GetAudioData();
    const int iSourceChannels = Source.iInputChannel2 == INVALID_INDEX ? 1 : 2;

    for ( int i = 0; i < iMonoBlockSizeSam; ++i )
    {
        const int iInputOffset = i * iInputChannels;
        int16_t iFirst = 0;
        int16_t iSecond = 0;

        if ( ( Source.iInputChannel1 >= 0 ) && ( Source.iInputChannel1 < iInputChannels ) &&
             ( iInputOffset + Source.iInputChannel1 < vecInputAudio.Size() ) )
        {
            iFirst = vecInputAudio[iInputOffset + Source.iInputChannel1];
        }
        if ( ( iSourceChannels == 2 ) && ( Source.iInputChannel2 >= 0 ) && ( Source.iInputChannel2 < iInputChannels ) &&
             ( iInputOffset + Source.iInputChannel2 < vecInputAudio.Size() ) )
        {
            iSecond = vecInputAudio[iInputOffset + Source.iInputChannel2];
        }

        if ( iInputBoost != 1 )
        {
            iFirst = Float2Short ( static_cast<float> ( iFirst ) * iInputBoost );
            if ( iSourceChannels == 2 )
            {
                iSecond = Float2Short ( static_cast<float> ( iSecond ) * iInputBoost );
            }
        }

        if ( iSourceChannels == 1 )
        {
            vecAudioData[i] = iFirst;
        }
        else
        {
            vecAudioData[2 * i]     = iFirst;
            vecAudioData[2 * i + 1] = iSecond;
        }
    }
}

void CClient::ProcessAudioDataIntern ( CVector<int16_t>&       vecsStereoSndCrd,
                                       const CVector<int16_t>& vecInputAudio,
                                       const int               iInputChannels )
{
    int            i = 0;
    int            j = 0;
    int            iUnused = 0;
    unsigned char* pCurCodedData = nullptr;

    // Transmit signal ---------------------------------------------------------
    if ( eAudioChannelConf == CC_ADVANCED )
    {
        // SetAdvancedAudioChannels() and SetAudioChannels() always install a
        // primary row before Init(). Keep this guard allocation-free because
        // it runs in the real-time audio callback.
        Q_ASSERT ( !vecAdvancedAudioChannels.isEmpty() );
        if ( vecAdvancedAudioChannels.isEmpty() )
        {
            PrimaryAudioChannel.GetAudioData().Reset ( 0 );
        }
        else
        {
            FillAudioSourceBuffer ( PrimaryAudioChannel, vecAdvancedAudioChannels[0], vecInputAudio, iInputChannels );
            for ( int iSource = 0; iSource < vecAdditionalAudioChannels.size(); ++iSource )
            {
                FillAudioSourceBuffer ( *vecAdditionalAudioChannels[iSource],
                                        vecAdvancedAudioChannels[iSource + 1],
                                        vecInputAudio,
                                        iInputChannels );
            }

#ifndef HEADLESS
            SignalLevelMeter.Update ( PrimaryAudioChannel.GetAudioData(),
                                      iMonoBlockSizeSam,
                                      vecAdvancedAudioChannels[0].iInputChannel2 != INVALID_INDEX );
#endif
        }
    }
    else
    {
        // Legacy routes use the driver's selected stereo pair exactly as
        // before; advanced mode is the only route that reads the full capture.
        vecPrimaryInputAudio = vecsStereoSndCrd;

        if ( iInputBoost != 1 )
        {
            for ( i = 0, j = 0; i < iMonoBlockSizeSam; ++i, j += 2 )
            {
                vecPrimaryInputAudio[j]     = Float2Short ( static_cast<float> ( vecPrimaryInputAudio[j] ) * iInputBoost );
                vecPrimaryInputAudio[j + 1] = Float2Short ( static_cast<float> ( vecPrimaryInputAudio[j + 1] ) * iInputBoost );
            }
        }

#ifndef HEADLESS
        SignalLevelMeter.Update ( vecPrimaryInputAudio, iMonoBlockSizeSam, true );
#endif

        if ( iReverbLevel != 0 )
        {
            AudioReverb.Process ( vecPrimaryInputAudio,
                                  bReverbOnLeftChan,
                                  static_cast<float> ( iReverbLevel ) / AUD_REVERB_MAX / 4 );
        }

        if ( !( ( iAudioInFader == AUD_FADER_IN_MIDDLE ) && ( eAudioChannelConf == CC_STEREO ) ) )
        {
            const float fPan = static_cast<float> ( iAudioInFader ) / AUD_FADER_IN_MAX;
            if ( eAudioChannelConf == CC_STEREO )
            {
                const float fGainL = MathUtils::GetLeftPan ( fPan, false );
                const float fGainR = MathUtils::GetRightPan ( fPan, false );
                for ( i = 0, j = 0; i < iMonoBlockSizeSam; ++i, j += 2 )
                {
                    vecPrimaryInputAudio[j]     = static_cast<int16_t> ( fGainL * vecPrimaryInputAudio[j] );
                    vecPrimaryInputAudio[j + 1] = static_cast<int16_t> ( fGainR * vecPrimaryInputAudio[j + 1] );
                }
            }
            else
            {
                const float fGainL = MathUtils::GetLeftPan ( fPan, eAudioChannelConf != CC_MONO_IN_STEREO_OUT );
                const float fGainR = MathUtils::GetRightPan ( fPan, eAudioChannelConf != CC_MONO_IN_STEREO_OUT );
                for ( i = 0, j = 0; i < iMonoBlockSizeSam; ++i, j += 2 )
                {
                    vecPrimaryInputAudio[i] = Float2Short ( fGainL * vecPrimaryInputAudio[j] + fGainR * vecPrimaryInputAudio[j + 1] );
                }
            }
        }

        if ( eAudioChannelConf == CC_MONO_IN_STEREO_OUT )
        {
            for ( i = iMonoBlockSizeSam - 1, j = iStereoBlockSizeSam - 2; i >= 0; --i, j -= 2 )
            {
                vecPrimaryInputAudio[j] = vecPrimaryInputAudio[j + 1] = vecPrimaryInputAudio[i];
            }
        }

        // The primary stream uses the same reusable implementation as every
        // extra Advanced fader.
        PrimaryAudioChannel.GetAudioData() = vecPrimaryInputAudio;
    }

    PrimaryAudioChannel.Process ( PrimaryAudioChannel.GetAudioData(), bMuteOutStream );
    for ( CClientAudioChannel* pAudioChannel : vecAdditionalAudioChannels )
    {
        pAudioChannel->Process ( pAudioChannel->GetAudioData(), bMuteOutStream );
        // Auxiliary shards are transmit-only from the UI perspective. Drain
        // their server return packets so their jitter buffers do not fill.
        pAudioChannel->DiscardReceivedAudio();
    }

    // Receive signal ----------------------------------------------------------
    if ( bMuteOutStream )
    {
        const CVector<int16_t>& vecPrimaryAudio = PrimaryAudioChannel.GetAudioData();
        if ( iNumAudioChannels == 1 )
        {
            for ( i = 0; i < iMonoBlockSizeSam; ++i )
            {
                vecsStereoSndCrdMuteStream[2 * i] = vecsStereoSndCrdMuteStream[2 * i + 1] = vecPrimaryAudio[i];
            }
        }
        else
        {
            vecsStereoSndCrdMuteStream = vecPrimaryAudio;
        }
    }

    for ( i = 0, j = 0; i < iSndCrdFrameSizeFactor; ++i, j += iNumAudioChannels * iOPUSFrameSizeSamples )
    {
        const bool bReceiveDataOk = ( Channel.GetData ( vecbyNetwData, iCeltNumCodedBytes ) == GS_BUFFER_OK );
        if ( bReceiveDataOk )
        {
            pCurCodedData = &vecbyNetwData[0];
            bIsInitializationPhase = false;
        }
        else
        {
            pCurCodedData = nullptr;
            bJitterBufferOK = false;
        }

        if ( CurOpusDecoder != nullptr )
        {
            iUnused = opus_custom_decode ( CurOpusDecoder, pCurCodedData, iCeltNumCodedBytes, &vecsStereoSndCrd[j], iOPUSFrameSizeSamples );
        }
        else if ( ( eAudioQuality == AQ_RAW ) && bRawAudioIsSupported )
        {
            if ( pCurCodedData != nullptr )
            {
                memcpy ( &vecsStereoSndCrd[j], pCurCodedData, iCeltNumCodedBytes );
            }
            else
            {
                memset ( &vecsStereoSndCrd[j], 0, iCeltNumCodedBytes );
            }
        }
    }

    if ( bMuteOutStream )
    {
        for ( i = 0; i < iStereoBlockSizeSam; ++i )
        {
            vecsStereoSndCrd[i] = Float2Short ( vecsStereoSndCrd[i] + vecsStereoSndCrdMuteStream[i] * fMuteOutStreamGain );
        }
    }

    if ( Channel.IsConnected() && !bIsInitializationPhase )
    {
        if ( iNumAudioChannels == 1 )
        {
            for ( i = iMonoBlockSizeSam - 1, j = iStereoBlockSizeSam - 2; i >= 0; --i, j -= 2 )
            {
                vecsStereoSndCrd[j] = vecsStereoSndCrd[j + 1] = vecsStereoSndCrd[i];
            }
        }
    }
    else
    {
        vecsStereoSndCrd.Reset ( 0 );
    }

    Q_UNUSED ( iUnused )
}

int CClient::EstimatedOverallDelay ( const int iPingTimeMs )
{
    const float fSystemBlockDurationMs = static_cast<float> ( iOPUSFrameSizeSamples ) / SYSTEM_SAMPLE_RATE_HZ * 1000;

    // If the jitter buffers are set effectively, i.e. they are exactly the
    // size of the network jitter, then the delay of the buffer is the buffer
    // length. Since that is usually not the case but the buffers are usually
    // a bit larger than necessary, we introduce some factor for compensation.
    // Consider the jitter buffer on the client and on the server side, too.
    const float fTotalJitterBufferDelayMs = fSystemBlockDurationMs * ( GetSockBufNumFrames() + GetServerSockBufNumFrames() ) * JITTBUF_COMP_FACTOR;

    // consider delay introduced by the sound card conversion buffer by using
    // "GetSndCrdConvBufAdditionalDelayMonoBlSize()"
    float fTotalSoundCardDelayMs = GetSndCrdConvBufAdditionalDelayMonoBlSize() * 1000.0f / SYSTEM_SAMPLE_RATE_HZ;

    // try to get the actual input/output sound card delay from the audio
    // interface, per definition it is not available if a 0 is returned
    const float fSoundCardInputOutputLatencyMs = Sound.GetInOutLatencyMs();

    if ( fSoundCardInputOutputLatencyMs == 0.0f )
    {
        // use an alternative approach for estimating the sound card delay:
        //
        // we assume that we have two period sizes for the input and one for the
        // output, therefore we have "3 *" instead of "2 *" (for input and output)
        // the actual sound card buffer size
        // "GetSndCrdConvBufAdditionalDelayMonoBlSize"
        fTotalSoundCardDelayMs += ( 3 * GetSndCrdActualMonoBlSize() ) * 1000.0f / SYSTEM_SAMPLE_RATE_HZ;
    }
    else
    {
        // add the actual sound card latency in ms
        fTotalSoundCardDelayMs += fSoundCardInputOutputLatencyMs;
    }

    // network packets are of the same size as the audio packets per definition
    // if no sound card conversion buffer is used
    const float fDelayToFillNetworkPacketsMs = GetSystemMonoBlSize() * 1000.0f / SYSTEM_SAMPLE_RATE_HZ;

    // OPUS additional delay at small frame sizes is half a frame size
    const float fAdditionalAudioCodecDelayMs = CurOpusDecoder != nullptr ? fSystemBlockDurationMs / 2 : 0.0f;

    const float fTotalBufferDelayMs =
        fDelayToFillNetworkPacketsMs + fTotalJitterBufferDelayMs + fTotalSoundCardDelayMs + fAdditionalAudioCodecDelayMs;

    return MathUtils::round ( fTotalBufferDelayMs + iPingTimeMs );
}

// Management of Client Channels and mapping to/from Server Channels

void CClient::ClearClientChannels()
{
    QMutexLocker locker ( &MutexChannels );

    iActiveChannels = 0;
    iJoinSequence   = 0;

    for ( int i = 0; i < MAX_NUM_CHANNELS; i++ )
    {
        clientChannels[i].iServerChannelID = INVALID_INDEX;
        // all other fields will be initialised on channel allocation

        clientChannelIDs[i] = INVALID_INDEX;
    }

    // qInfo() << "> Client channel list cleared";
}

void CClient::FreeClientChannel ( const int iServerChannelID )
{
    QMutexLocker locker ( &MutexChannels );

    if ( iServerChannelID == INVALID_INDEX || iServerChannelID >= MAX_NUM_CHANNELS )
    {
        return;
    }

    const int iClientChannelID = clientChannelIDs[iServerChannelID];

    Q_ASSERT ( clientChannels[iClientChannelID].iServerChannelID == iServerChannelID );

    clientChannelIDs[iServerChannelID]                = INVALID_INDEX;
    clientChannels[iClientChannelID].iServerChannelID = INVALID_INDEX;

    iActiveChannels -= 1;

    /*
    qInfo() << qUtf8Printable ( QString ( "> Freed client ch %1 for server ch %2; chan count = %3" )
                                    .arg ( iClientChannelID )
                                    .arg ( iServerChannelID )
                                    .arg ( iActiveChannels ) );
     */
}

void CClient::OnMidiCCReceived ( int ccNumber ) { emit MidiCCReceived ( ccNumber ); }

// find, and optionally create, a client channel for the supplied server channel ID
// returns a client channel ID or INVALID_INDEX
int CClient::FindClientChannel ( const int iServerChannelID, const bool bCreateIfNew )
{
    QMutexLocker locker ( &MutexChannels );

    if ( iServerChannelID == INVALID_INDEX || iServerChannelID >= MAX_NUM_CHANNELS )
    {
        return INVALID_INDEX;
    }

    int iClientChannelID = clientChannelIDs[iServerChannelID];

    if ( iClientChannelID != INVALID_INDEX )
    {
        Q_ASSERT ( clientChannels[iClientChannelID].iServerChannelID == iServerChannelID );

        return iClientChannelID;
    }

    // no matching client channel - create new one if requested
    if ( bCreateIfNew )
    {
        // search clientChannels[] for a free client channel
        for ( iClientChannelID = 0; iClientChannelID < MAX_NUM_CHANNELS; iClientChannelID++ )
        {
            CClientChannel* clientChan = &clientChannels[iClientChannelID];

            if ( clientChan->iServerChannelID == INVALID_INDEX )
            {
                clientChan->iServerChannelID = iServerChannelID;
                clientChan->iJoinSequence    = ++iJoinSequence;

                clientChan->oldGain = clientChan->newGain = 1.0f;
                clientChan->oldPan = clientChan->newPan = 0.5f;

                clientChan->level = 0;

                clientChannelIDs[iServerChannelID] = iClientChannelID;

                iActiveChannels += 1;

                /*
                qInfo() << qUtf8Printable ( QString ( "> Alloc client ch %1 for server ch %2; chan count = %3" )
                                                .arg ( iClientChannelID )
                                                .arg ( iServerChannelID )
                                                .arg ( iActiveChannels ) );
                 */

                return iClientChannelID; // new client channel ID
            }
        }
    }

    return INVALID_INDEX;
}

// When the client receives a channel level list from the server, the list contains one value
// for each currently-active channel, ordered by the channel ID assigned by the server.
// The values will correspond to the active channels in the last client list that was sent.
// This list is passed up to the mixer board, which will interpret the values in the order
// of channels that it knows about.
//
// Since CClient is translating server channel IDs to local client channel IDs before
// passing the client list up to the mixer board, it is also necessary to re-order the values
// in the level list so that they are in order of mapped client channel ID.
// This function performs that re-ordering by scanning the server channels in order, once,
// for active channels, and storing the level value in the corresponding client channel.
// Then the function scans the client channels in order, fetching the level values and putting
// them back into vecLevelList in order of client channel. The mixer board will then display
// the levels against the correct channels.
//
// The list size is checked against the current number of active channels to guard against
// any unexpected temporary mismatch in size due to potential out-of-order message delivery.
//
// This function returns true if the list has been processed and should be passed on,
// or false if it was the wrong size and should be discarded.

bool CClient::ReorderLevelList ( CVector<uint16_t>& vecLevelList )
{
    QMutexLocker locker ( &MutexChannels );

    // vecLevelList is sent from server ordered by server channel ID
    // re-order it by client channel ID before passing up to the GUI
    // the list is passed in by reference and modified in situ

    // check it is the right length
    if ( vecLevelList.Size() != iActiveChannels )
    {
        return false; // tell caller to ignore it
    }

    int iClientCh;
    int iServerCh = 0;

    // fetch levels by server channel ID

    for ( int i = 0; i < iActiveChannels; i++ )
    {
        // find next active server channel
        while ( iServerCh < MAX_NUM_CHANNELS )
        {
            iClientCh = clientChannelIDs[iServerCh++];

            if ( iClientCh != INVALID_INDEX )
            {
                clientChannels[iClientCh].level = vecLevelList[i];
                break;
            }
        }
    }

    // store levels by client channel ID

    iClientCh = 0;

    for ( int i = 0; i < iActiveChannels; i++ )
    {
        while ( iClientCh < MAX_NUM_CHANNELS )
        {
            uint16_t level = clientChannels[iClientCh].level;

            iServerCh = clientChannels[iClientCh++].iServerChannelID;

            if ( iServerCh != INVALID_INDEX )
            {
                vecLevelList[i] = level;
                break;
            }
        }
    }

    return true; // tell caller to emit signal with new list
}
