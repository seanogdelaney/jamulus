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

#include "server.h"
#include "util.h"

// CServer implementation ******************************************************
CServer::CServer ( const int          iNewMaxNumChan,
                   const QString&     strLoggingFileName,
                   const QString&     strServerBindIP,
                   const quint16      iPortNumber,
                   const quint16      iQosNumber,
                   const QString&     strDirectoryAddress,
                   const QString&     strServerListFileName,
                   const QString&     strServerInfo,
                   const QString&     strServerListFilter,
                   const QString&     strServerPublicIP,
                   const QString&     strNewWelcomeMessage,
                   const QString&     strRecordingDirName,
                   const bool         bNDisconnectAllClientsOnQuit,
                   const bool         bNUseDoubleSystemFrameSize,
                   const bool         bNDisableRaw,
                   const bool         bNUseMultithreading,
                   const bool         bDisableRecording,
                   const bool         bNDelayPan,
                   const bool         bNDisableIPv6,
                   const ELicenceType eNLicenceType ) :
    bUseDoubleSystemFrameSize ( bNUseDoubleSystemFrameSize ),
    bUseMultithreading ( bNUseMultithreading ),
    iMaxNumChannels ( iNewMaxNumChan ),
    iCurNumSessions ( 0 ),
    iCurNumSources ( 0 ),
    bDisableRaw ( bNDisableRaw ),
    bIPv6Available ( false ),
    Socket ( this, iPortNumber, iQosNumber, strServerBindIP, bNDisableIPv6, bIPv6Available ),
    Logging(),
    iFrameCount ( 0 ),
    HighPrecisionTimer ( bNUseDoubleSystemFrameSize ),
    ServerListManager ( this,
                        iPortNumber,
                        strDirectoryAddress,
                        strServerListFileName,
                        strServerInfo,
                        strServerPublicIP,
                        strServerListFilter,
                        iNewMaxNumChan,
                        &ConnLessProtocol ),
    JamController ( this ),
    bDisableRecording ( bDisableRecording ),
    bAutoRunMinimized ( false ),
    bDelayPan ( bNDelayPan ),
    eLicenceType ( eNLicenceType ),
    bDisconnectAllClientsOnQuit ( bNDisconnectAllClientsOnQuit ),
    pSignalHandler ( CSignalHandler::getSingletonP() )
{
    int iOpusError;
    int i;

    // create OPUS encoder/decoder for each channel (must be done before
    // enabling the channels), create a mono and stereo encoder/decoder
    // for each channel
    for ( i = 0; i < MAX_NUM_CHANNELS; i++ )
    {
        // Allocate codec/transport primitives for the fixed session/source
        // pools.  Active visible faders remain capped by iMaxNumChannels.
        // init OPUS -----------------------------------------------------------
        OpusMode[i] = opus_custom_mode_create ( SYSTEM_SAMPLE_RATE_HZ, DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES, &iOpusError );

        Opus64Mode[i] = opus_custom_mode_create ( SYSTEM_SAMPLE_RATE_HZ, SYSTEM_FRAME_SIZE_SAMPLES, &iOpusError );

        // init audio encoders and decoders
        OpusEncoderMono[i]     = opus_custom_encoder_create ( OpusMode[i], 1, &iOpusError );   // mono encoder legacy
        OpusDecoderMono[i]     = opus_custom_decoder_create ( OpusMode[i], 1, &iOpusError );   // mono decoder legacy
        OpusEncoderStereo[i]   = opus_custom_encoder_create ( OpusMode[i], 2, &iOpusError );   // stereo encoder legacy
        OpusDecoderStereo[i]   = opus_custom_decoder_create ( OpusMode[i], 2, &iOpusError );   // stereo decoder legacy
        Opus64EncoderMono[i]   = opus_custom_encoder_create ( Opus64Mode[i], 1, &iOpusError ); // mono encoder OPUS64
        Opus64DecoderMono[i]   = opus_custom_decoder_create ( Opus64Mode[i], 1, &iOpusError ); // mono decoder OPUS64
        Opus64EncoderStereo[i] = opus_custom_encoder_create ( Opus64Mode[i], 2, &iOpusError ); // stereo encoder OPUS64
        Opus64DecoderStereo[i] = opus_custom_decoder_create ( Opus64Mode[i], 2, &iOpusError ); // stereo decoder OPUS64

        // we require a constant bit rate
        opus_custom_encoder_ctl ( OpusEncoderMono[i], OPUS_SET_VBR ( 0 ) );
        opus_custom_encoder_ctl ( OpusEncoderStereo[i], OPUS_SET_VBR ( 0 ) );
        opus_custom_encoder_ctl ( Opus64EncoderMono[i], OPUS_SET_VBR ( 0 ) );
        opus_custom_encoder_ctl ( Opus64EncoderStereo[i], OPUS_SET_VBR ( 0 ) );

        // for 64 samples frame size we have to adjust the PLC behavior to avoid loud artifacts
        opus_custom_encoder_ctl ( Opus64EncoderMono[i], OPUS_SET_PACKET_LOSS_PERC ( 35 ) );
        opus_custom_encoder_ctl ( Opus64EncoderStereo[i], OPUS_SET_PACKET_LOSS_PERC ( 35 ) );

        // we want as low delay as possible
        opus_custom_encoder_ctl ( OpusEncoderMono[i], OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
        opus_custom_encoder_ctl ( OpusEncoderStereo[i], OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
        opus_custom_encoder_ctl ( Opus64EncoderMono[i], OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );
        opus_custom_encoder_ctl ( Opus64EncoderStereo[i], OPUS_SET_APPLICATION ( OPUS_APPLICATION_RESTRICTED_LOWDELAY ) );

        // set encoder low complexity for legacy 128 samples frame size
        opus_custom_encoder_ctl ( OpusEncoderMono[i], OPUS_SET_COMPLEXITY ( 1 ) );
        opus_custom_encoder_ctl ( OpusEncoderStereo[i], OPUS_SET_COMPLEXITY ( 1 ) );

        // init double-to-normal frame size conversion buffers -----------------
        // use worst case memory initialization to avoid allocating memory in
        // the time-critical thread
        DoubleFrameSizeConvBufIn[i].Init ( 2 /* stereo */ * DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES /* worst case buffer size */ );
        DoubleFrameSizeConvBufOut[i].Init ( 2 /* stereo */ * DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES /* worst case buffer size */ );
    }

    // define colors for chat window identifiers
    vstrChatColors.Init ( 6 );
    vstrChatColors[0] = "mediumblue";
    vstrChatColors[1] = "red";
    vstrChatColors[2] = "darkorchid";
    vstrChatColors[3] = "green";
    vstrChatColors[4] = "maroon";
    vstrChatColors[5] = "coral";

    // set the server frame size
    if ( bUseDoubleSystemFrameSize )
    {
        iServerFrameSizeSamples = DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES;
    }
    else
    {
        iServerFrameSizeSamples = SYSTEM_FRAME_SIZE_SAMPLES;
    }

    // To avoid audio clitches, in the entire realtime timer audio processing
    // routine including the ProcessData no memory must be allocated. Since we
    // do not know the required sizes for the vectors, we allocate memory for
    // the worst case here:

    // allocate worst case memory for the temporary vectors
    vecChanIDsCurConChan.Init ( iMaxNumChannels );
    vecSessionIDsCurConSession.Init ( MAX_NUM_CHANNELS );
    vecvecfGains.Init ( iMaxNumChannels );
    vecvecfPannings.Init ( iMaxNumChannels );
    vecvecsData.Init ( iMaxNumChannels );
    vecvecsData2.Init ( iMaxNumChannels );
    vecvecsSendData.Init ( iMaxNumChannels );
    vecvecfIntermediateProcBuf.Init ( iMaxNumChannels );
    vecvecbyCodedData.Init ( iMaxNumChannels );
    vecSourceIngressPayload.Init ( MAX_NUM_CHANNELS );
    vecSourceIngressPresent.Init ( 2 * MAX_NUM_CHANNELS );
    vecNumAudioChannels.Init ( iMaxNumChannels );
    vecNumFrameSizeConvBlocks.Init ( iMaxNumChannels );
    vecUseDoubleSysFraSizeConvBuf.Init ( iMaxNumChannels );
    vecAudioComprType.Init ( iMaxNumChannels );

    for ( i = 0; i < iMaxNumChannels; i++ )
    {
        // init vectors storing information of all channels
        vecvecfGains[i].Init ( iMaxNumChannels );
        vecvecfPannings[i].Init ( iMaxNumChannels );

        // we always use stereo audio buffers (which is the worst case)
        vecvecsData[i].Init ( 2 /* stereo */ * DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES /* worst case buffer size */ );
        vecvecsData2[i].Init ( 2 /* stereo */ * DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES /* worst case buffer size */ );

        // (note that we only allocate iMaxNumChannels buffers for the send
        // and coded data because of the OMP implementation)
        vecvecsSendData[i].Init ( 2 /* stereo */ * DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES /* worst case buffer size */ );

        // allocate worst case memory for intermediate processing buffers in float precision
        vecvecfIntermediateProcBuf[i].Init ( 2 /* stereo */ * DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES /* worst case buffer size */ );

        // allocate worst case memory for the coded data
        vecvecbyCodedData[i].Init ( MAX_SIZE_BYTES_NETW_BUF );
    }
    // These arrays are indexed by actual session/source IDs, which may be
    // above the configured active-fader cap while a map is reserved.
    for ( i = 0; i < MAX_NUM_CHANNELS; ++i )
    {
        vecSourceIngressPayload[i].Init ( 2 * MultiSource::kMaxRawStereoPayloadBytes );
        vecSources[i].Reset();
        vecSessionState[i].Reset();
    }

    // allocate worst case memory for the channel levels
    vecChannelLevels.Init ( iMaxNumChannels );

    // enable logging (if requested)
    if ( !strLoggingFileName.isEmpty() )
    {
        Logging.Start ( strLoggingFileName );
    }

    // manage welcome message: if the welcome message is a valid link to a local
    // file, the content of that file is used as the welcome message (#361)
    SetWelcomeMessage ( strNewWelcomeMessage ); // first use given text, may be overwritten

    if ( QFileInfo ( strNewWelcomeMessage ).exists() )
    {
        QFile file ( strNewWelcomeMessage );

        if ( file.open ( QIODevice::ReadOnly | QIODevice::Text ) )
        {
            // use entire file content for the welcome message
            SetWelcomeMessage ( file.readAll() );
        }
    }

    // enable jam recording (if requested) - kicks off the thread (note
    // that jam recorder needs the frame size which is given to the jam
    // recorder in the SetRecordingDir() function)
    SetRecordingDir ( strRecordingDirName );

    // Enable the complete fixed transport pool.  Source visibility is capped
    // separately by iMaxNumChannels; prepared Advanced maps may occupy source
    // IDs above that active count until promotion replaces the legacy source.
    for ( i = 0; i < MAX_NUM_CHANNELS; i++ )
    {
        vecSessions[i].SetEnable ( true );
        vecSessionOrder[i] = i;
    }

    int iAvailableCores = QThread::idealThreadCount();

    // setup CThreadPool if multithreading is active and possible
    if ( bUseMultithreading )
    {
        if ( iAvailableCores == 1 )
        {
            qDebug() << "found only one core, disabling multithreading";
            bUseMultithreading = false;
        }
        else
        {
            // set maximum thread count to available cores; other threads will share at random
            iMaxNumThreads = iAvailableCores;
            qDebug() << "multithreading enabled, setting thread count to" << iMaxNumThreads;

            pThreadPool = std::unique_ptr<CThreadPool> ( new CThreadPool{ static_cast<size_t> ( iMaxNumThreads ) } );
            Futures.reserve ( iMaxNumThreads );
        }
    }

    // Connections -------------------------------------------------------------
    // connect timer timeout signal
    QObject::connect ( &HighPrecisionTimer, &CHighPrecisionTimer::timeout, this, &CServer::OnTimer );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLMessReadyForSending, this, &CServer::OnSendCLProtMessage );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLPingReceived, this, &CServer::OnCLPingReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLPingWithNumClientsReceived, this, &CServer::OnCLPingWithNumClientsReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLRegisterServerReceived, this, &CServer::OnCLRegisterServerReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLRegisterServerExReceived, this, &CServer::OnCLRegisterServerExReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLUnregisterServerReceived, this, &CServer::OnCLUnregisterServerReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLReqServerList, this, &CServer::OnCLReqServerList );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLRegisterServerResp, this, &CServer::OnCLRegisterServerResp );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLSendEmptyMes, this, &CServer::OnCLSendEmptyMes );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLDisconnection, this, &CServer::OnCLDisconnection );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLReqVersionAndOS, this, &CServer::OnCLReqVersionAndOS );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLVersionAndOSReceived, this, &CServer::CLVersionAndOSReceived );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLReqConnClientsList, this, &CServer::OnCLReqConnClientsList );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLReqServerFeatures, this, &CServer::OnCLReqServerFeatures );

    QObject::connect ( &ConnLessProtocol, &CProtocol::CLReqWelcomeMessage, this, &CServer::OnCLReqWelcomeMessage );

    QObject::connect ( &ServerListManager, &CServerListManager::SvrRegStatusChanged, this, &CServer::SvrRegStatusChanged );

    QObject::connect ( &JamController, &recorder::CJamController::RestartRecorder, this, &CServer::RestartRecorder );

    QObject::connect ( &JamController, &recorder::CJamController::StopRecorder, this, &CServer::StopRecorder );

    QObject::connect ( &JamController, &recorder::CJamController::RecordingSessionStarted, this, &CServer::RecordingSessionStarted );

    QObject::connect ( &JamController, &recorder::CJamController::EndRecorderThread, this, &CServer::EndRecorderThread );

    QObject::connect ( this, &CServer::Stopped, &JamController, &recorder::CJamController::Stopped );

    QObject::connect ( this, &CServer::ClientDisconnected, &JamController, &recorder::CJamController::ClientDisconnected );

    qRegisterMetaType<CVector<int16_t>> ( "CVector<int16_t>" );
    qRegisterMetaType<CVector<CMultiSourceSourceConfig>> ( "CVector<CMultiSourceSourceConfig>" );
    QObject::connect ( this, &CServer::AudioFrame, &JamController, &recorder::CJamController::AudioFrame );

    QObject::connect ( QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &CServer::OnAboutToQuit );

    QObject::connect ( pSignalHandler, &CSignalHandler::HandledSignal, this, &CServer::OnHandledSignal );

    connectChannelSignalsToServerSlots<MAX_NUM_CHANNELS>();

    // start the socket (it is important to start the socket after all
    // initializations and connections)
    Socket.Start();
}

template<unsigned int slotId>
inline void CServer::connectChannelSignalsToServerSlots()
{
    int iCurChanID = slotId - 1;

    void ( CServer::*pOnSendProtMessCh ) ( CVector<uint8_t> ) = &CServerSlots<slotId>::OnSendProtMessCh;

    void ( CServer::*pOnReqConnClientsListCh )() = &CServerSlots<slotId>::OnReqConnClientsListCh;

    void ( CServer::*pOnChatTextReceivedCh ) ( QString ) = &CServerSlots<slotId>::OnChatTextReceivedCh;

    void ( CServer::*pOnMuteStateHasChangedCh ) ( int, bool ) = &CServerSlots<slotId>::OnMuteStateHasChangedCh;

    void ( CServer::*pOnServerAutoSockBufSizeChangeCh ) ( int ) = &CServerSlots<slotId>::OnServerAutoSockBufSizeChangeCh;

    // send message
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::MessReadyForSending, this, pOnSendProtMessCh );

    // request connected clients list
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::ReqConnClientsList, this, pOnReqConnClientsListCh );

    // channel info has changed
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::ChanInfoHasChanged, this, &CServer::CreateAndSendChanListForAllConChannels );

    // chat text received
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::ChatTextReceived, this, pOnChatTextReceivedCh );

    // other mute state has changed
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::MuteStateHasChanged, this, pOnMuteStateHasChangedCh );

    // auto socket buffer size change
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::ServerAutoSockBufSizeChange, this, pOnServerAutoSockBufSizeChangeCh );
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::ServerJitterPolicyChanged, this,
                       [this, iCurChanID] ( int numFrames, bool bAuto ) { OnSessionJitterPolicyChanged ( iCurChanID, numFrames, bAuto ); } );

    // These signals deliberately stay attached to the session slot. The fader
    // source map is negotiated once, while protocol, timeout and return audio
    // remain owned by this single CChannel.
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::ReqMultiSourceCaps, this, [this, iCurChanID]() { OnReqMultiSourceCaps ( iCurChanID ); } );
    QObject::connect ( &vecSessions[iCurChanID], &CChannel::MultiSourceConfigReceived, this,
                       [this, iCurChanID] ( CVector<CMultiSourceSourceConfig> config ) { OnMultiSourceConfig ( iCurChanID, config ); } );

    connectChannelSignalsToServerSlots<slotId - 1>();
}

template<>
inline void CServer::connectChannelSignalsToServerSlots<0>()
{}

void CServer::CreateAndSendJitBufMessage ( const int iCurChanID, const int iNNumFra ) { vecSessions[iCurChanID].CreateJitBufMes ( iNNumFra ); }

CServer::~CServer()
{
    for ( int i = 0; i < MAX_NUM_CHANNELS; i++ )
    {
        // free audio encoders and decoders
        opus_custom_encoder_destroy ( OpusEncoderMono[i] );
        opus_custom_decoder_destroy ( OpusDecoderMono[i] );
        opus_custom_encoder_destroy ( OpusEncoderStereo[i] );
        opus_custom_decoder_destroy ( OpusDecoderStereo[i] );
        opus_custom_encoder_destroy ( Opus64EncoderMono[i] );
        opus_custom_decoder_destroy ( Opus64DecoderMono[i] );
        opus_custom_encoder_destroy ( Opus64EncoderStereo[i] );
        opus_custom_decoder_destroy ( Opus64DecoderStereo[i] );

        // free audio modes
        opus_custom_mode_destroy ( OpusMode[i] );
        opus_custom_mode_destroy ( Opus64Mode[i] );
    }
}

void CServer::SendProtMessage ( int iChID, CVector<uint8_t> vecMessage )
{
    // the protocol queries me to call the function to send the message
    // send it through the network
    Socket.SendPacket ( vecMessage, vecSessions[iChID].GetAddress() );
}

void CServer::OnNewConnection ( int iChID, int iTotChans, CHostAddress RecHostAddr )
{
    QMutexLocker locker ( &Mutex );

    // inform the client about its own ID at the server (note that this
    // must be the first message to be sent for a new connection)
    const int legacySourceID = GetLegacySourceID ( iChID );
    vecSessions[iChID].CreateClientIDMes ( legacySourceID );

    // if not disabled, inform the client that the server supports raw (uncompressed) audio
    if ( !bDisableRaw )
    {
        vecSessions[iChID].CreateRawAudioSupportedMes();
    }

    // Send an empty channel list in order to force clients to reset their
    // audio mixer state. This is required to trigger clients to re-send their
    // gain levels upon reconnecting after server restarts.
    vecSessions[iChID].CreateConClientListMes ( CVector<CChannelInfo> ( 0 ) );

    // query support for split messages in the client
    vecSessions[iChID].CreateReqSplitMessSupportMes();

    // on a new connection we query the network transport properties for the
    // audio packets (to use the correct network block size and audio
    // compression properties, etc.)
    vecSessions[iChID].CreateReqNetwTranspPropsMes();

    // this is a new connection, query the jitter buffer size we shall use
    // for this client (note that at the same time on a new connection the
    // client sends the jitter buffer size by default but maybe we have
    // reached a state where this did not happen because of network trouble,
    // client or server thinks that the connection was still active, etc.)
    vecSessions[iChID].CreateReqJitBufMes();

    // A new client connected to the server, the channel list
    // at all clients have to be updated. This is done by sending
    // a channel name request to the client which causes a channel
    // name message to be transmitted to the server. If the server
    // receives this message, the channel list will be automatically
    // updated (implicitly).
    //
    // Usually it is not required to send the channel list to the
    // client currently connecting since it automatically requests
    // the channel list on a new connection (as a result, he will
    // usually get the list twice which has no impact on functionality
    // but will only increase the network load a tiny little bit). But
    // in case the client thinks he is still connected but the server
    // was restartet, it is important that we send the channel list
    // at this place.
    vecSessions[iChID].CreateReqChanInfoMes();

    // send welcome message (if enabled)
    {
        QMutexLocker locker ( &MutexWelcomeMessage );
        if ( !strWelcomeMessage.isEmpty() )
        {
            // create formatted server welcome message and send it just to
            // the client which just connected to the server
            const QString strWelcomeMessageFormated = WELCOME_MESSAGE_PREFIX + strWelcomeMessage;

            vecSessions[iChID].CreateChatTextMes ( strWelcomeMessageFormated );
        }
    }

    // send licence request message (if enabled)
    if ( eLicenceType != LT_NO_LICENCE )
    {
        vecSessions[iChID].CreateLicReqMes ( eLicenceType );
    }

    // send version info (for, e.g., feature activation in the client)
    vecSessions[iChID].CreateVersionAndOSMes();

    // send recording state message on connection
    vecSessions[iChID].CreateRecorderStateMes ( JamController.GetRecorderState() );

    // reset the conversion buffers
    if ( legacySourceID != INVALID_INDEX ) DoubleFrameSizeConvBufIn[legacySourceID].Reset();
    DoubleFrameSizeConvBufOut[iChID].Reset();

    // logging of new connected channel
    Logging.AddNewConnection ( RecHostAddr.InetAddr, iTotChans );

    // Recorder/RPC client identities are visible source IDs, not the private
    // transport-session slot.
    if ( legacySourceID != INVALID_INDEX ) emit ClientConnected ( legacySourceID, RecHostAddr.InetAddr, iTotChans );
}

void CServer::OnCLReqServerFeatures ( CHostAddress RecHostAddr )
{
    // This is a bitmask of features enabled at the server.
    // EFeatureSet from util.h is used to shift each bool into position
    uint32_t iFeatures = 0;

    // Use 64 samples frame size mode? (argument -F)
    iFeatures |= ( !bUseDoubleSystemFrameSize << FS_FAST_UPDATE );

    // Multithreading enabled? (argument -T)
    iFeatures |= ( bUseMultithreading << FS_MULTITHREADING );

    // Recording directory set? (argument -R)
    // If a recording directory is set a server could potentially record all client audio
    iFeatures |= ( GetRecorderInitialised() << FS_RECORDER_ENABLED );

    // Will an idle server start recording when a client joins or is it already recording an active session?
    // (argument --norecord disables recording by default)
    iFeatures |= ( ( JamController.GetRecorderState() == RS_RECORDING ) << FS_IS_RECORDING );

    // Delay pan enabled? (argument -P)
    iFeatures |= ( bDelayPan << FS_DELAY_PAN );

    // IPv6 available? (argument --noipv6 disables this feature)
    iFeatures |= ( bIPv6Available << FS_IPV6_AVAILABLE );

    // "Max" audio quality setting enabled? (argument --noraw disables this feature)
    iFeatures |= ( !bDisableRaw << FS_RAW_AUDIO );

    // Disconnect all clients on quit? (argument -d)
    iFeatures |= ( bDisconnectAllClientsOnQuit << FS_DISCONONQUIT );

    // Has welcome message? (argument -w)
    iFeatures |= ( !strWelcomeMessage.isEmpty() << FS_HAS_WELCOME_MESSAGE );

    // Logging enabled? (argument -l)
    iFeatures |= ( Logging.IsLogging() << FS_IS_LOGGING );

    // Licence agreement required? (argument -L)
    iFeatures |= ( ( eLicenceType != LT_NO_LICENCE ) << FS_HAS_LICENCE );

    // TODO:
    // Running a GUI? (argument -n disables the GUI)
    // iFeatures |= (  << FS_HAS_GUI );
    //
    // // RPC interface enabled? (argument --jsonrpcport)
    // iFeatures |= (  << FS_RPC_ENABLED );

    // qDebug() << QString::number(iFeatures, 2).rightJustified(32, '0');

    // Create and send the message
    ConnLessProtocol.CreateCLServerFeaturesMes ( RecHostAddr, iFeatures );
}

void CServer::OnCLReqWelcomeMessage ( CHostAddress RecHostAddr )
{
    // Create and send the message
    ConnLessProtocol.CreateCLWelcomeMessageMes ( RecHostAddr, strWelcomeMessage );
}

void CServer::OnServerFull ( CHostAddress RecHostAddr )
{
    // note: no mutex required here

    // inform the calling client that no channel is free
    ConnLessProtocol.CreateCLServerFullMes ( RecHostAddr );
}

void CServer::OnSendCLProtMessage ( CHostAddress InetAddr, CVector<uint8_t> vecMessage )
{
    // the protocol queries me to call the function to send the message
    // send it through the network
    Socket.SendPacket ( vecMessage, InetAddr );
}

void CServer::OnCLDisconnection ( CHostAddress InetAddr )
{
    // check if the given address is actually a client which is connected to
    // this server, if yes, disconnect it
    const int iCurChanID = FindChannel ( InetAddr );

    if ( iCurChanID != INVALID_CHANNEL_ID )
    {
        vecSessions[iCurChanID].Disconnect();
    }
}

void CServer::OnAboutToQuit()
{
    // if enabled, disconnect all clients on quit
    if ( bDisconnectAllClientsOnQuit )
    {
        QMutexLocker locker ( &Mutex );
        for ( int i = 0; i < iMaxNumChannels; i++ )
        {
            if ( vecSessions[i].IsConnected() )
            {
                ConnLessProtocol.CreateCLDisconnection ( vecSessions[i].GetAddress() );
            }
        }
    }

    Stop();
}

void CServer::OnHandledSignal ( int sigNum )
{
    // show the signal number on the console (note that this may not work for Windows)
    qDebug() << qUtf8Printable ( QString ( "OnHandledSignal: %1" ).arg ( sigNum ) );

#ifdef _WIN32
    // Windows does not actually get OnHandledSignal triggered
    QCoreApplication::instance()->exit();
    Q_UNUSED ( sigNum )
#else
    switch ( sigNum )
    {
    case SIGUSR1:
        RequestNewRecording();
        break;

    case SIGUSR2:
        SetEnableRecording ( !JamController.GetRecordingEnabled() );
        break;

    case SIGINT:
    case SIGTERM:
        // This should trigger OnAboutToQuit
        QCoreApplication::instance()->exit();
        break;

    default:
        break;
    }
#endif
}

void CServer::Start()
{
    // only start if not already running
    if ( !IsRunning() )
    {
        // start timer
        HighPrecisionTimer.Start();

        // emit start signal
        emit Started();
    }
}

void CServer::Stop()
{
    // Under Mac we have the problem that the timer shutdown might
    // take some time and therefore we get a lot of "server stopped"
    // entries in the log. The following condition shall prevent this.
    // For the other OSs this should not hurt either.
    if ( IsRunning() )
    {
        // stop timer
        HighPrecisionTimer.Stop();

        // logging (add "server stopped" logging entry)
        Logging.AddServerStopped();

        // emit stopped signal
        emit Stopped();
    }
}

void CServer::OnTimer()
{
    int iNumSources = 0;
    int iNumSessions = 0;
    bool bUseMT = false;
    int iNumBlocks = 0;
    int iMTBlockSize = 0;
    bChannelIsNowDisconnected = false;

    {
        QMutexLocker locker ( &Mutex );

        // Source IDs are the visible fader order. Session IDs remain private
        // transport identities and are built separately below.
        for ( int sourceID = 0; sourceID < iMaxNumChannels; ++sourceID )
        {
            if ( vecSources[sourceID].IsActive() ) vecChanIDsCurConChan[iNumSources++] = sourceID;
        }
        for ( int sessionID = 0; sessionID < MAX_NUM_CHANNELS; ++sessionID )
        {
            if ( vecSessions[sessionID].IsConnected() ) vecSessionIDsCurConSession[iNumSessions++] = sessionID;
        }

        // One advanced ingress ring feeds all source decoders.  The number of
        // logical frames consumed is determined by the codec/server frame pair,
        // never by the sound-card callback or source count.
        for ( int i = 0; i < iNumSessions; ++i )
        {
            const int sessionID = vecSessionIDsCurConSession[i];
            CServerSessionState& state = vecSessionState[sessionID];
            if ( state.eState != CServerSessionState::ST_ACTIVE || state.iNumSources == 0 ) continue;

            const EAudComprType codec = vecSources[state.vecSourceIDs[0]].GetConfig().eCodec;
            int framesToRead = 1;
            if ( !bUseDoubleSystemFrameSize && codec == CT_OPUS )
            {
                framesToRead = state.bAdvancedHalfFramePending ? 0 : 1;
                state.bAdvancedHalfFramePending = !state.bAdvancedHalfFramePending;
            }
            else if ( bUseDoubleSystemFrameSize && codec == CT_OPUS64 )
            {
                framesToRead = 2;
            }
            for ( int block = 0; block < framesToRead; ++block ) ReadAdvancedFrame ( sessionID, block );
        }

        bUseMT = bUseMultithreading && iNumSources > 0;
        if ( !bUseMT )
        {
            DecodeReceiveDataBlocks ( this, 0, iNumSources - 1, iNumSources );
        }
        else
        {
            iNumBlocks = std::min ( iNumSources, iMaxNumThreads );
            iMTBlockSize = ( iNumSources - 1 ) / iNumBlocks + 1;
            for ( int block = 0; block < iNumBlocks; ++block )
            {
                const int start = block * iMTBlockSize;
                const int stop = std::min ( ( block + 1 ) * iMTBlockSize - 1, iNumSources - 1 );
                Futures.push_back ( pThreadPool->enqueue ( CServer::DecodeReceiveDataBlocks, this, start, stop, iNumSources ) );
            }
            for ( auto& future : Futures ) future.wait();
            Futures.clear();
        }

        if ( bChannelIsNowDisconnected ) CreateAndSendChanListForAllConChannels();
    }

    if ( iNumSessions == 0 )
    {
        Stop();
        return;
    }

    const bool bSendChannelLevels = CreateLevelsForAllConChannels ( iNumSources, vecNumAudioChannels, vecvecsData, vecChannelLevels );
    for ( int sourceIndex = 0; sourceIndex < iNumSources; ++sourceIndex )
    {
        const int sourceID = vecChanIDsCurConChan[sourceIndex];
        const int sessionID = vecSources[sourceID].ParentSessionID();
        if ( JamController.GetRecordingEnabled() )
        {
            emit AudioFrame ( sourceID,
                              GetSourceInfo ( sourceID ).strName,
                              vecSessions[sessionID].GetAddress(),
                              vecNumAudioChannels[sourceIndex],
                              vecvecsData[sourceIndex] );
        }
    }

    for ( int sessionIndex = 0; sessionIndex < iNumSessions; ++sessionIndex )
    {
        const int sessionID = vecSessionIDsCurConSession[sessionIndex];
        // For an Advanced session, the ingress ring is deliberately the one
        // server jitter policy; CChannel's legacy SockBuf is no longer read.
        if ( vecSessionState[sessionID].eState != CServerSessionState::ST_ACTIVE ) vecSessions[sessionID].UpdateSocketBufferSize();
        if ( bSendChannelLevels )
        {
            ConnLessProtocol.CreateCLChannelLevelListMes ( vecSessions[sessionID].GetAddress(), vecChannelLevels, iNumSources );
        }
    }

    // Output is indexed by target session, not source. This is the structural
    // invariant which produces exactly one return mix per participant.
    if ( !bUseMT )
    {
        for ( int sessionIndex = 0; sessionIndex < iNumSessions; ++sessionIndex ) MixEncodeTransmitData ( sessionIndex, iNumSources );
    }
    else
    {
        iNumBlocks = std::min ( iNumSessions, iMaxNumThreads );
        iMTBlockSize = ( iNumSessions - 1 ) / iNumBlocks + 1;
        for ( int block = 0; block < iNumBlocks; ++block )
        {
            const int start = block * iMTBlockSize;
            const int stop = std::min ( ( block + 1 ) * iMTBlockSize - 1, iNumSessions - 1 );
            Futures.push_back ( pThreadPool->enqueue ( CServer::MixEncodeTransmitDataBlocks, this, start, stop, iNumSources ) );
        }
        for ( auto& future : Futures ) future.wait();
        Futures.clear();
    }

    if ( bDelayPan )
    {
        for ( int sourceIndex = 0; sourceIndex < iNumSources; ++sourceIndex )
        {
            for ( int sample = 0; sample < 2 * iServerFrameSizeSamples; ++sample ) vecvecsData2[sourceIndex][sample] = vecvecsData[sourceIndex][sample];
        }
    }
}

void CServer::DecodeReceiveDataBlocks ( CServer* pServer, const int startSource, const int stopSource, const int numSources )
{
    for ( int sourceIndex = startSource; sourceIndex <= stopSource; ++sourceIndex ) pServer->DecodeReceiveData ( sourceIndex, numSources );
}

void CServer::MixEncodeTransmitDataBlocks ( CServer* pServer, const int startSession, const int stopSession, const int numSources )
{
    for ( int sessionIndex = startSession; sessionIndex <= stopSession; ++sessionIndex ) pServer->MixEncodeTransmitData ( sessionIndex, numSources );
}

bool CServer::ReadAdvancedFrame ( const int sessionID, const int blockIndex )
{
    CServerSessionState& state = vecSessionState[sessionID];
    if ( state.eState != CServerSessionState::ST_ACTIVE || blockIndex < 0 || blockIndex >= 2 ) return false;

    std::array<MultiSource::RecordView, MultiSource::kMaxSourceRows> records {};
    // Start playout only after the negotiated session-level window is present.
    // This is intentionally shared by all sources; individual missing records
    // remain null and are handled by source-local PLC below.
    if ( !state.bIngressPrimed )
    {
        const uint32_t needed = state.iFirstSequence + static_cast<uint32_t> ( qMax ( 1, state.iIngressTargetFrames ) - 1 );
        if ( !state.Ingress.HasHighestSequence() || MultiSource::SequenceBefore ( state.Ingress.HighestSequence(), needed ) )
        {
            for ( int sourceIndex = 0; sourceIndex < state.iNumSources; ++sourceIndex )
            {
                const int sourceID = state.vecSourceIDs[sourceIndex];
                vecSourceIngressPresent[static_cast<size_t> ( sourceID ) * 2 + static_cast<size_t> ( blockIndex )] = 0;
            }
            return false;
        }
        state.iNextSequence = state.iFirstSequence;
        state.bIngressPrimed = true;
    }
    const bool haveFrame = state.Ingress.Read ( state.iNextSequence, records.data(), records.size() );
    for ( int sourceIndex = 0; sourceIndex < state.iNumSources; ++sourceIndex )
    {
        const int sourceID = state.vecSourceIDs[sourceIndex];
        const size_t presentIndex = static_cast<size_t> ( sourceID ) * 2 + static_cast<size_t> ( blockIndex );
        vecSourceIngressPresent[presentIndex] = 0;
        if ( haveFrame && records[sourceIndex].data != nullptr )
        {
            CVector<uint8_t>& payload = vecSourceIngressPayload[sourceID];
            const size_t offset = static_cast<size_t> ( blockIndex ) * MultiSource::kMaxRawStereoPayloadBytes;
            std::memcpy ( &payload[static_cast<int> ( offset )], records[sourceIndex].data, records[sourceIndex].length );
            vecSourceIngressPresent[presentIndex] = 1;
        }
    }
    ++state.iNextSequence;
    return haveFrame;
}

void CServer::DecodeReceiveData ( const int sourceIndex, const int numSources )
{
    Q_UNUSED ( numSources )
    const int sourceID = vecChanIDsCurConChan[sourceIndex];
    CServerSource& source = vecSources[sourceID];
    const int sessionID = source.ParentSessionID();
    CChannel& session = vecSessions[sessionID];

    vecNumAudioChannels[sourceIndex] = source.IsLegacy() ? session.GetNumAudioChannels() : source.GetConfig().iNumChannels;
    vecAudioComprType[sourceIndex] = source.IsLegacy() ? session.GetAudioCompressionType() : source.GetConfig().eCodec;
    if ( vecAudioComprType[sourceIndex] != CT_OPUS && vecAudioComprType[sourceIndex] != CT_OPUS64 )
    {
        vecvecsData[sourceIndex].Reset ( 0 );
        return;
    }

    const bool useDoubleConversion = !bUseDoubleSystemFrameSize && vecAudioComprType[sourceIndex] == CT_OPUS;
    const int blocks = bUseDoubleSystemFrameSize && vecAudioComprType[sourceIndex] == CT_OPUS64 ? 2 : 1;
    vecUseDoubleSysFraSizeConvBuf[sourceIndex] = useDoubleConversion;
    vecNumFrameSizeConvBlocks[sourceIndex] = blocks;

    CConvBuf<int16_t>& inputConversion = DoubleFrameSizeConvBufIn[sourceID];
    if ( useDoubleConversion )
    {
        inputConversion.SetBufferSize ( DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES * vecNumAudioChannels[sourceIndex] );
        if ( inputConversion.Get ( vecvecsData[sourceIndex], SYSTEM_FRAME_SIZE_SAMPLES * vecNumAudioChannels[sourceIndex] ) )
        {
            source.AdvanceFade();
            return;
        }
    }

    const int frameSamples = vecAudioComprType[sourceIndex] == CT_OPUS ? DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES : SYSTEM_FRAME_SIZE_SAMPLES;
    OpusCustomDecoder* decoder = nullptr;
    if ( vecAudioComprType[sourceIndex] == CT_OPUS ) decoder = vecNumAudioChannels[sourceIndex] == 1 ? OpusDecoderMono[sourceID] : OpusDecoderStereo[sourceID];
    else decoder = vecNumAudioChannels[sourceIndex] == 1 ? Opus64DecoderMono[sourceID] : Opus64DecoderStereo[sourceID];

    for ( int block = 0; block < blocks; ++block )
    {
        const uint8_t* coded = nullptr;
        int codedBytes = 0;
        bool raw = false;
        bool disconnected = false;

        if ( source.IsLegacy() )
        {
            codedBytes = session.GetCeltNumCodedBytes();
            const EGetDataStat status = session.GetData ( vecvecbyCodedData[sourceIndex], codedBytes );
            if ( status == GS_CHAN_NOW_DISCONNECTED ) disconnected = true;
            else if ( status == GS_BUFFER_OK ) coded = &vecvecbyCodedData[sourceIndex][0];
            raw = codedBytes == static_cast<int> ( sizeof ( int16_t ) * frameSamples * vecNumAudioChannels[sourceIndex] );
        }
        else
        {
            const size_t presentIndex = static_cast<size_t> ( sourceID ) * 2 + static_cast<size_t> ( block );
            codedBytes = source.GetConfig().iPayloadBytes;
            if ( vecSourceIngressPresent[presentIndex] != 0 )
            {
                coded = &vecSourceIngressPayload[sourceID][static_cast<int> ( block * MultiSource::kMaxRawStereoPayloadBytes )];
            }
            raw = source.GetConfig().bRaw;
        }

        if ( disconnected )
        {
            FreeChannel ( sessionID );
            bChannelIsNowDisconnected = true;
            return;
        }

        const int offset = block * SYSTEM_FRAME_SIZE_SAMPLES * vecNumAudioChannels[sourceIndex];
        if ( raw )
        {
            if ( coded != nullptr ) std::memcpy ( &vecvecsData[sourceIndex][offset], coded, codedBytes );
            else std::memset ( &vecvecsData[sourceIndex][offset], 0, codedBytes );
        }
        else
        {
            // Null input intentionally invokes Opus PLC for a missing fragment/source.
            opus_custom_decode ( decoder, coded, codedBytes, &vecvecsData[sourceIndex][offset], frameSamples );
        }
    }

    if ( useDoubleConversion )
    {
        inputConversion.PutAll ( vecvecsData[sourceIndex] );
        inputConversion.Get ( vecvecsData[sourceIndex], SYSTEM_FRAME_SIZE_SAMPLES * vecNumAudioChannels[sourceIndex] );
    }
    source.AdvanceFade();
}

void CServer::MixEncodeTransmitData ( const int sessionIndex, const int numSources )
{
    const int sessionID = vecSessionIDsCurConSession[sessionIndex];
    CChannel& target = vecSessions[sessionID];
    const int targetChannels = target.GetNumAudioChannels();
    const EAudComprType codec = target.GetAudioCompressionType();
    if ( ( targetChannels != 1 && targetChannels != 2 ) || ( codec != CT_OPUS && codec != CT_OPUS64 ) ) return;

    CVector<float>& mix = vecvecfIntermediateProcBuf[sessionIndex];
    CVector<int16_t>& out = vecvecsSendData[sessionIndex];
    mix.Reset ( 0 );

    for ( int sourceIndex = 0; sourceIndex < numSources; ++sourceIndex )
    {
        const int sourceID = vecChanIDsCurConChan[sourceIndex];
        const CServerSource& source = vecSources[sourceID];
        const CVector<int16_t>& input = vecvecsData[sourceIndex];
        const int inputChannels = vecNumAudioChannels[sourceIndex];
        float gain = target.GetGain ( sourceID ) * source.FadeInGain();
        // The target session's join fade remains session-scoped, while source
        // fade remains independent for each newly activated Advanced source.
        gain *= target.GetFadeInGain();
        const float pan = target.GetPan ( sourceID );

        if ( targetChannels == 1 )
        {
            for ( int sample = 0; sample < iServerFrameSizeSamples; ++sample )
            {
                const float value = inputChannels == 1 ? input[sample] : ( static_cast<float> ( input[2 * sample] ) + input[2 * sample + 1] ) / 2.0f;
                mix[sample] += value * gain;
            }
        }
        else
        {
            const float gainLeft = MathUtils::GetLeftPan ( pan, false ) * gain;
            const float gainRight = MathUtils::GetRightPan ( pan, false ) * gain;
            for ( int sample = 0; sample < iServerFrameSizeSamples; ++sample )
            {
                if ( inputChannels == 1 )
                {
                    mix[2 * sample] += input[sample] * gainLeft;
                    mix[2 * sample + 1] += input[sample] * gainRight;
                }
                else
                {
                    mix[2 * sample] += input[2 * sample] * gainLeft;
                    mix[2 * sample + 1] += input[2 * sample + 1] * gainRight;
                }
            }
        }
    }

    const int outputSamples = targetChannels * iServerFrameSizeSamples;
    for ( int sample = 0; sample < outputSamples; ++sample ) out[sample] = Float2Short ( mix[sample] );

    const int frameSamples = codec == CT_OPUS ? DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES : SYSTEM_FRAME_SIZE_SAMPLES;
    const int codedBytes = target.GetCeltNumCodedBytes();
    const bool useDoubleConversion = !bUseDoubleSystemFrameSize && codec == CT_OPUS;
    CConvBuf<int16_t>& outputConversion = DoubleFrameSizeConvBufOut[sessionID];
    if ( useDoubleConversion )
    {
        outputConversion.SetBufferSize ( DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES * targetChannels );
        if ( !outputConversion.Put ( out, outputSamples ) ) return;
        outputConversion.GetAll ( out, DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES * targetChannels );
    }

    OpusCustomEncoder* encoder = nullptr;
    if ( codec == CT_OPUS ) encoder = targetChannels == 1 ? OpusEncoderMono[sessionID] : OpusEncoderStereo[sessionID];
    else encoder = targetChannels == 1 ? Opus64EncoderMono[sessionID] : Opus64EncoderStereo[sessionID];
    CVector<uint8_t>& coded = vecvecbyCodedData[sessionIndex];
    const bool raw = codedBytes == static_cast<int> ( sizeof ( int16_t ) * frameSamples * targetChannels );
    if ( raw )
    {
        std::memcpy ( &coded[0], &out[0], codedBytes );
    }
    else
    {
        opus_custom_encoder_ctl ( encoder, OPUS_SET_BITRATE ( CalcBitRateBitsPerSecFromCodedBytes ( codedBytes, frameSamples ) ) );
        opus_custom_encode ( encoder, &out[0], frameSamples, &coded[0], codedBytes );
    }
    target.PrepAndSendPacket ( &Socket, coded, codedBytes );
}


CChannelCoreInfo CServer::GetSourceInfo ( const int sourceID ) const
{
    if ( !MathUtils::InRange<int> ( sourceID, 0, MAX_NUM_CHANNELS - 1 ) || !vecSources[sourceID].IsAllocated() ) return CChannelCoreInfo();
    const int parent = vecSources[sourceID].ParentSessionID();
    if ( !MathUtils::InRange<int> ( parent, 0, MAX_NUM_CHANNELS - 1 ) ) return CChannelCoreInfo();
    return vecSources[sourceID].MakeVisibleInfo ( vecSessions[parent].GetChanInfo() );
}

int CServer::GetLegacySourceID ( const int sessionID ) const
{
    return MathUtils::InRange<int> ( sessionID, 0, MAX_NUM_CHANNELS - 1 ) ? vecSessionState[sessionID].iLegacySourceID : INVALID_INDEX;
}

int CServer::AllocateSource ( const int parentSessionID, const CMultiSourceSourceConfig& sourceConfig, const bool legacy, const bool active )
{
    for ( int sourceID = 0; sourceID < MAX_NUM_CHANNELS; ++sourceID )
    {
        if ( !vecSources[sourceID].IsAllocated() )
        {
            CMultiSourceSourceConfig config = sourceConfig;
            config.iFaderID = sourceID;
            const int fadeFrames = config.eCodec == CT_OPUS64 ? FADE_IN_NUM_FRAMES : FADE_IN_NUM_FRAMES_DBLE_FRAMESIZE;
            vecSources[sourceID].Reserve ( parentSessionID, config, legacy, fadeFrames );
            if ( active ) vecSources[sourceID].Activate();
            ++iCurNumSources;
            for ( int session = 0; session < MAX_NUM_CHANNELS; ++session )
            {
                vecSessions[session].SetGain ( sourceID, 1.0f );
                vecSessions[session].SetPan ( sourceID, 0.5f );
            }
            return sourceID;
        }
    }
    return INVALID_INDEX;
}

void CServer::FreeSource ( const int sourceID )
{
    if ( !MathUtils::InRange<int> ( sourceID, 0, MAX_NUM_CHANNELS - 1 ) || !vecSources[sourceID].IsAllocated() ) return;
    if ( vecSources[sourceID].IsActive() ) emit ClientDisconnected ( sourceID );
    vecSources[sourceID].Reset();
    if ( iCurNumSources > 0 ) --iCurNumSources;
    for ( int session = 0; session < MAX_NUM_CHANNELS; ++session )
    {
        vecSessions[session].SetGain ( sourceID, 1.0f );
        vecSessions[session].SetPan ( sourceID, 0.5f );
    }
}

void CServer::FreeAllSourcesForSession ( const int sessionID )
{
    for ( int sourceID = 0; sourceID < MAX_NUM_CHANNELS; ++sourceID )
    {
        if ( vecSources[sourceID].IsAllocated() && vecSources[sourceID].ParentSessionID() == sessionID ) FreeSource ( sourceID );
    }
    vecSessionState[sessionID].Reset();
}

CVector<CChannelInfo> CServer::CreateChannelList()
{
    CVector<CChannelInfo> list ( 0 );
    for ( int sourceID = 0; sourceID < MAX_NUM_CHANNELS; ++sourceID )
    {
        if ( vecSources[sourceID].IsActive() ) list.Add ( CChannelInfo ( sourceID, GetSourceInfo ( sourceID ) ) );
    }
    return list;
}

void CServer::CreateAndSendChanListForAllConChannels()
{
    const CVector<CChannelInfo> list = CreateChannelList();
    for ( int sessionID = 0; sessionID < MAX_NUM_CHANNELS; ++sessionID )
    {
        if ( vecSessions[sessionID].IsConnected() ) vecSessions[sessionID].CreateConClientListMes ( list );
    }
}

void CServer::CreateAndSendChanListForThisChan ( const int sessionID )
{
    if ( MathUtils::InRange<int> ( sessionID, 0, MAX_NUM_CHANNELS - 1 ) && vecSessions[sessionID].IsConnected() )
    {
        vecSessions[sessionID].CreateConClientListMes ( CreateChannelList() );
    }
}

void CServer::CreateAndSendChatTextForAllConChannels ( const int sessionID, const QString& strChatText )
{
    const int sourceID = GetLegacySourceID ( sessionID );
    const QString name = sourceID != INVALID_INDEX ? GetSourceInfo ( sourceID ).strName : vecSessions[sessionID].GetName();
    const QString colour = vstrChatColors[sessionID % vstrChatColors.Size()];
    const QString text = "<font color=\"" + colour + "\">(" + QTime::currentTime().toString ( "hh:mm:ss AP" ) + ") <b>" +
                         name.toHtmlEscaped() + "</b></font> " + strChatText.toHtmlEscaped();
    SendChatTextToAllConChannels ( sourceID, text );
}

void CServer::SendChatTextToAllConChannels ( const int iSendingChanID, const QString& strChatText )
{
    for ( int sessionID = 0; sessionID < MAX_NUM_CHANNELS; ++sessionID )
    {
        if ( vecSessions[sessionID].IsConnected() ) vecSessions[sessionID].CreateChatTextMes ( strChatText );
    }
    emit sentChatMessage ( iSendingChanID, strChatText );
}

bool CServer::SendChatTextToConChannel ( const int sourceID, const QString& strChatText )
{
    if ( !MathUtils::InRange<int> ( sourceID, 0, MAX_NUM_CHANNELS - 1 ) || !vecSources[sourceID].IsActive() ) return false;
    const int sessionID = vecSources[sourceID].ParentSessionID();
    vecSessions[sessionID].CreateChatTextMes ( strChatText );
    return true;
}

void CServer::CreateAndSendRecorderStateForAllConChannels()
{
    const ERecorderState state = JamController.GetRecorderState();
    for ( int sessionID = 0; sessionID < MAX_NUM_CHANNELS; ++sessionID )
    {
        if ( vecSessions[sessionID].IsConnected() ) vecSessions[sessionID].CreateRecorderStateMes ( state );
    }
}

void CServer::CreateOtherMuteStateChanged ( const int targetSessionID, const int sourceID, const bool isMuted )
{
    Q_UNUSED ( targetSessionID )
    if ( !MathUtils::InRange<int> ( sourceID, 0, MAX_NUM_CHANNELS - 1 ) || !vecSources[sourceID].IsActive() ) return;
    const int ownerSessionID = vecSources[sourceID].ParentSessionID();
    if ( vecSessions[ownerSessionID].IsConnected() ) vecSessions[ownerSessionID].CreateMuteStateHasChangedMes ( sourceID, isMuted );
}

int CServer::GetNumberOfConnectedClients()
{
    QMutexLocker locker ( &MutexChanOrder );
    int count = 0;
    for ( int sourceID = 0; sourceID < MAX_NUM_CHANNELS; ++sourceID ) if ( vecSources[sourceID].IsActive() ) ++count;
    return count;
}

int CServer::GetNumberOfConnectedSessions()
{
    QMutexLocker locker ( &MutexChanOrder );
    return iCurNumSessions;
}

int CServer::FindChannel ( const CHostAddress& address, const bool allowNew )
{
    QMutexLocker locker ( &MutexChanOrder );
    int left = 0;
    int right = iCurNumSessions;
    while ( right > left )
    {
        const int middle = ( left + right ) / 2;
        const int compare = address.Compare ( vecSessions[vecSessionOrder[middle]].GetAddress() );
        if ( compare == 0 ) return vecSessionOrder[middle];
        if ( compare > 0 ) left = middle + 1;
        else right = middle;
    }

    // New legacy sessions always require one visible source. Session capacity is
    // independently bounded by the compile-time pool, while the configured
    // server limit applies to the visible source pool.
    if ( !allowNew || iCurNumSessions >= MAX_NUM_CHANNELS || iCurNumSources >= iMaxNumChannels ) return INVALID_CHANNEL_ID;
    int freeOrderIndex = iCurNumSessions++;
    const int sessionID = vecSessionOrder[freeOrderIndex];
    InitChannel ( sessionID, address );
    while ( freeOrderIndex > right )
    {
        vecSessionOrder[freeOrderIndex] = vecSessionOrder[freeOrderIndex - 1];
        --freeOrderIndex;
    }
    vecSessionOrder[freeOrderIndex] = sessionID;
    return sessionID;
}

void CServer::InitChannel ( const int sessionID, const CHostAddress& address )
{
    vecSessions[sessionID].SetAddress ( address );
    vecSessions[sessionID].ResetInfo();
    vecSessionState[sessionID].Reset();
    CMultiSourceSourceConfig legacy;
    legacy.iKey = 1;
    legacy.iNumChannels = 1;
    legacy.eCodec = CT_OPUS;
    legacy.bRaw = false;
    legacy.iPayloadBytes = CELT_MINIMUM_NUM_BYTES;
    const int sourceID = AllocateSource ( sessionID, legacy, true, true );
    vecSessionState[sessionID].iLegacySourceID = sourceID;
}

void CServer::FreeChannel ( const int sessionID )
{
    FreeAllSourcesForSession ( sessionID );
    QMutexLocker locker ( &MutexChanOrder );
    for ( int index = 0; index < iCurNumSessions; ++index )
    {
        if ( vecSessionOrder[index] != sessionID ) continue;
        --iCurNumSessions;
        while ( index < iCurNumSessions )
        {
            vecSessionOrder[index] = vecSessionOrder[index + 1];
            ++index;
        }
        vecSessionOrder[index] = sessionID;
        return;
    }
}

void CServer::DumpChannels ( const QString& title )
{
    qDebug() << qUtf8Printable ( title );
    for ( int index = 0; index < MAX_NUM_CHANNELS; ++index )
    {
        const int sessionID = vecSessionOrder[index];
        if ( index == iCurNumSessions ) qDebug() << "----------";
        qDebug() << qUtf8Printable ( QString ( "%1: session [%2] %3" ).arg ( index, 3 ).arg ( sessionID ).arg ( vecSessions[sessionID].GetAddress().toString() ) );
    }
}

void CServer::OnProtocolCLMessageReceived ( int iRecID, CVector<uint8_t> data, CHostAddress address )
{
    QMutexLocker locker ( &Mutex );
    ConnLessProtocol.ParseConnectionLessMessageBody ( data, iRecID, address );
}

void CServer::OnProtocolMessageReceived ( int counter, int id, CVector<uint8_t> data, CHostAddress address )
{
    QMutexLocker locker ( &Mutex );
    const int sessionID = FindChannel ( address );
    if ( sessionID != INVALID_CHANNEL_ID ) vecSessions[sessionID].PutProtocolData ( counter, id, data, address );
}

bool CServer::PutAdvancedAudioData ( const CVector<uint8_t>& packet, const int packetBytes, const CHostAddress& address, int& sessionID )
{
    sessionID = FindChannel ( address, false );
    if ( sessionID == INVALID_CHANNEL_ID ) return false;
    CServerSessionState& state = vecSessionState[sessionID];
    if ( state.eState != CServerSessionState::ST_PREPARED && state.eState != CServerSessionState::ST_ACTIVE ) return false;
    MultiSource::ParsedFragment fragment;
    if ( !MultiSource::ParseFragment ( &packet[0], static_cast<size_t> ( packetBytes ), fragment ) || fragment.generation != state.iGeneration ) return false;
    if ( !state.Ingress.Put ( &packet[0], static_cast<size_t> ( packetBytes ) ) ) return false;
    vecSessions[sessionID].ResetTimeOutCounter();
    if ( state.eState == CServerSessionState::ST_PREPARED && !ActivatePreparedSources ( sessionID, fragment.generation, fragment.sequence ) ) return false;
    return false; // an advanced packet never creates a legacy connection
}

bool CServer::PutAudioData ( const CVector<uint8_t>& packet, const int packetBytes, const CHostAddress& address, int& sessionID )
{
    QMutexLocker locker ( &Mutex );
    if ( packetBytes >= 2 && packet[0] == static_cast<uint8_t> ( MultiSource::kMagic >> 8 ) && packet[1] == static_cast<uint8_t> ( MultiSource::kMagic & 0xff ) )
    {
        return PutAdvancedAudioData ( packet, packetBytes, address, sessionID );
    }
    sessionID = FindChannel ( address, true );
    if ( sessionID == INVALID_CHANNEL_ID ) return false;
    if ( vecSessionState[sessionID].eState == CServerSessionState::ST_ACTIVE ) return false;
    return vecSessions[sessionID].PutAudioData ( packet, packetBytes, address ) == PS_NEW_CONNECTION;
}

void CServer::GetConCliParam ( CVector<CHostAddress>& addresses,
                               CVector<QString>& names,
                               CVector<int>& jitterFrames,
                               CVector<int>& networkFactors,
                               CVector<CChannelCoreInfo>& info )
{
    addresses.Init ( iMaxNumChannels );
    names.Init ( iMaxNumChannels );
    jitterFrames.Init ( iMaxNumChannels );
    networkFactors.Init ( iMaxNumChannels );
    info.Init ( iMaxNumChannels );
    for ( int sourceID = 0; sourceID < iMaxNumChannels; ++sourceID )
    {
        if ( !vecSources[sourceID].IsActive() ) continue;
        const int sessionID = vecSources[sourceID].ParentSessionID();
        addresses[sourceID] = vecSessions[sessionID].GetAddress();
        names[sourceID] = GetSourceInfo ( sourceID ).strName;
        jitterFrames[sourceID] = vecSessionState[sessionID].eState == CServerSessionState::ST_ACTIVE
                                     ? vecSessionState[sessionID].iIngressTargetFrames
                                     : vecSessions[sessionID].GetSockBufNumFrames();
        networkFactors[sourceID] = vecSessions[sessionID].GetNetwFrameSizeFact();
        info[sourceID] = GetSourceInfo ( sourceID );
    }
}


void CServer::OnSessionJitterPolicyChanged ( const int sessionID, const int numFrames, const bool bAuto )
{
    if ( !MathUtils::InRange<int> ( sessionID, 0, MAX_NUM_CHANNELS - 1 ) ||
         numFrames < MIN_NET_BUF_SIZE_NUM_BL || numFrames > MAX_NET_BUF_SIZE_NUM_BL )
    {
        return;
    }
    CServerSessionState& state = vecSessionState[sessionID];
    state.iIngressTargetFrames = numFrames;
    state.bIngressAuto = bAuto;
    if ( state.eState == CServerSessionState::ST_ACTIVE )
    {
        // Re-prime at the next unplayed logical sequence.  The packet storage
        // remains preallocated and sources keep their decoder state.
        state.iFirstSequence = state.iNextSequence;
        state.bIngressPrimed = false;
    }
}

bool CServer::PrepareAdvancedSources ( const int sessionID, const CVector<CMultiSourceSourceConfig>& config, uint8_t& rejectReason )
{
    rejectReason = MultiSourceProtocol::kRejectMalformed;
    if ( !MathUtils::InRange<int> ( sessionID, 0, MAX_NUM_CHANNELS - 1 ) || !vecSessions[sessionID].IsConnected() ||
         !vecSessions[sessionID].IsSplitMessageSupported() || vecSessionState[sessionID].eState != CServerSessionState::ST_LEGACY ||
         !MultiSourceProtocol::ValidateSourceConfig ( config ) )
    {
        return false;
    }
    if ( bDisableRaw && config[0].bRaw )
    {
        rejectReason = MultiSourceProtocol::kRejectUnsupported;
        return false;
    }

    // The configured maximum counts visible sources. Reserved faders replace
    // the still-visible legacy source on promotion, so calculate that effective
    // future count rather than pretending the transport session is a fader.
    int effectiveVisibleSources = 0;
    for ( int sourceID = 0; sourceID < MAX_NUM_CHANNELS; ++sourceID ) if ( vecSources[sourceID].IsActive() ) ++effectiveVisibleSources;
    effectiveVisibleSources += config.Size() - 1;
    if ( effectiveVisibleSources > iMaxNumChannels || iCurNumSources + config.Size() > MAX_NUM_CHANNELS )
    {
        rejectReason = MultiSourceProtocol::kRejectCapacity;
        return false;
    }

    CServerSessionState& state = vecSessionState[sessionID];
    std::array<MultiSource::SourceDescriptor, MultiSource::kMaxSourceRows> descriptors {};
    state.iNumSources = 0;
    for ( int index = 0; index < config.Size(); ++index )
    {
        const int sourceID = AllocateSource ( sessionID, config[index], false, false );
        if ( sourceID == INVALID_CHANNEL_ID )
        {
            for ( int rollback = 0; rollback < state.iNumSources; ++rollback ) FreeSource ( state.vecSourceIDs[rollback] );
            state.Reset();
            rejectReason = MultiSourceProtocol::kRejectCapacity;
            return false;
        }
        state.vecSourceIDs[state.iNumSources] = sourceID;
        descriptors[state.iNumSources] = MultiSource::SourceDescriptor { config[index].iKey, config[index].iNumChannels, config[index].iPayloadBytes, config[index].bRaw };
        ++state.iNumSources;
    }

    static uint16_t nextGeneration = 1;
    if ( nextGeneration == 0 ) ++nextGeneration;
    state.iGeneration = nextGeneration++;
    if ( state.iGeneration == 0 ) state.iGeneration = nextGeneration++;
    state.iIngressTargetFrames = vecSessions[sessionID].GetSockBufNumFrames();
    state.bIngressAuto = vecSessions[sessionID].GetDoAutoSockBufSize();
    // Preallocate the largest policy window once at negotiation.  Later
    // manual/automatic policy changes only alter playout state, never resize
    // audio-thread storage.
    if ( !state.Ingress.Configure ( state.iGeneration, config[0].bRaw, descriptors.data(), static_cast<size_t> ( state.iNumSources ),
                                    static_cast<size_t> ( MAX_NET_BUF_SIZE_NUM_BL ) ) )
    {
        for ( int rollback = 0; rollback < state.iNumSources; ++rollback ) FreeSource ( state.vecSourceIDs[rollback] );
        state.Reset();
        return false;
    }
    state.eState = CServerSessionState::ST_PREPARED;
    return true;
}

bool CServer::ActivatePreparedSources ( const int sessionID, const uint16_t generation, const uint32_t firstSequence )
{
    CServerSessionState& state = vecSessionState[sessionID];
    if ( state.eState != CServerSessionState::ST_PREPARED || state.iGeneration != generation ) return false;
    const int legacySourceID = state.iLegacySourceID;
    for ( int index = 0; index < state.iNumSources; ++index )
    {
        const int sourceID = state.vecSourceIDs[index];
        vecSources[sourceID].Activate();
        // Source faders own recorder tracks, while the parent CChannel owns
        // the endpoint and transport lifetime.
        emit ClientConnected ( sourceID, vecSessions[sessionID].GetAddress().InetAddr, GetNumberOfConnectedClients() );
    }
    if ( legacySourceID != INVALID_CHANNEL_ID ) FreeSource ( legacySourceID );
    state.iLegacySourceID = INVALID_INDEX;
    state.eState = CServerSessionState::ST_ACTIVE;
    state.iNextSequence = firstSequence;
    state.iFirstSequence = firstSequence;
    state.bHaveNextSequence = true;
    state.bIngressPrimed = false;
    state.bAdvancedHalfFramePending = false;
    // No source is visible before this point; the next timer sends exactly one
    // ordinary client list replacing the legacy fader with the source map.
    bChannelIsNowDisconnected = true;
    return true;
}

void CServer::OnReqMultiSourceCaps ( const int sessionID )
{
    if ( MathUtils::InRange<int> ( sessionID, 0, MAX_NUM_CHANNELS - 1 ) && vecSessions[sessionID].IsConnected() )
    {
        vecSessions[sessionID].CreateMultiSourceCapsMes();
    }
}

void CServer::OnMultiSourceConfig ( const int sessionID, CVector<CMultiSourceSourceConfig> config )
{
    uint8_t rejectReason = MultiSourceProtocol::kRejectMalformed;
    if ( !PrepareAdvancedSources ( sessionID, config, rejectReason ) )
    {
        vecSessions[sessionID].CreateMultiSourceRejectMes ( rejectReason );
        return;
    }

    CMultiSourceAcceptMap accept;
    accept.iGeneration = vecSessionState[sessionID].iGeneration;
    accept.vecSources.Init ( vecSessionState[sessionID].iNumSources );
    for ( int index = 0; index < vecSessionState[sessionID].iNumSources; ++index )
    {
        accept.vecSources[index] = vecSources[vecSessionState[sessionID].vecSourceIDs[index]].GetConfig();
    }
    vecSessions[sessionID].CreateMultiSourceAcceptMes ( accept );
}


void CServer::SetEnableRecording ( bool bNewEnableRecording )
{
    JamController.SetEnableRecording ( bNewEnableRecording, IsRunning() );

    // not dependent upon JamController state
    bDisableRecording = !bNewEnableRecording;

    // the recording state may have changed, send recording state message
    CreateAndSendRecorderStateForAllConChannels();
}

void CServer::SetWelcomeMessage ( const QString& strNWelcMess )
{
    // we need a mutex to secure access
    QMutexLocker locker ( &MutexWelcomeMessage );
    strWelcomeMessage = strNWelcMess;

    // restrict welcome message to maximum allowed length
    strWelcomeMessage = strWelcomeMessage.left ( MAX_LEN_CHAT_TEXT );
}

void CServer::customEvent ( QEvent* pEvent )
{
    if ( pEvent->type() == QEvent::User + 11 )
    {
        const int iMessType = ( (CCustomEvent*) pEvent )->iMessType;

        switch ( iMessType )
        {
        case MS_PACKET_RECEIVED:
            // wake up the server if a packet was received
            // if the server is still running, the call to Start() will have
            // no effect
            Start();
            break;
        }
    }
}

/// @brief Compute frame peak level for each client
bool CServer::CreateLevelsForAllConChannels ( const int                       iNumClients,
                                              const CVector<int>&             vecNumAudioChannels,
                                              const CVector<CVector<int16_t>> vecvecsData,
                                              CVector<uint16_t>&              vecLevelsOut )
{
    bool bLevelsWereUpdated = false;

    // low frequency updates
    if ( iFrameCount > CHANNEL_LEVEL_UPDATE_INTERVAL )
    {
        iFrameCount        = 0;
        bLevelsWereUpdated = true;

        for ( int j = 0; j < iNumClients; j++ )
        {
            // update and get signal level for meter in dB for each channel
            const double dCurSigLevelForMeterdB = vecSources[vecChanIDsCurConChan[j]].UpdateAndGetLevelForMeterdB ( vecvecsData[j],
                                                                                                                     iServerFrameSizeSamples,
                                                                                                                     vecNumAudioChannels[j] > 1 );

            // map value to integer for transmission via the protocol (4 bit available)
            vecLevelsOut[j] = static_cast<uint16_t> ( std::ceil ( dCurSigLevelForMeterdB ) );
        }
    }

    // increment the frame counter needed for low frequency update trigger
    iFrameCount++;

    if ( bUseDoubleSystemFrameSize )
    {
        // additional increment needed for double frame size to get to the same time interval
        iFrameCount++;
    }

    return bLevelsWereUpdated;
}
