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

#pragma once

#include <QObject>
#include <QDateTime>
#include <QHostAddress>
#include <QFileInfo>
#include <algorithm>
#ifdef USE_OPUS_SHARED_LIB
#    include "opus/opus_custom.h"
#else
#    include "opus_custom.h"
#endif
#include "global.h"
#include "buffer.h"
#include "signalhandler.h"
#include "socket.h"
#include "channel.h"
#include "multisource.h"
#include "util.h"
#include "serverlogging.h"
#include "serverlist.h"
#include "recorder/jamcontroller.h"

#include "threadpool.h"

/* Definitions ****************************************************************/
// no valid channel number
#define INVALID_CHANNEL_ID ( MAX_NUM_CHANNELS + 1 )

/* Classes ********************************************************************/
template<unsigned int slotId>
class CServerSlots : public CServerSlots<slotId - 1>
{
public:
    void OnSendProtMessCh ( CVector<uint8_t> mess ) { SendProtMessage ( slotId - 1, mess ); }
    void OnReqConnClientsListCh() { CreateAndSendChanListForThisChan ( slotId - 1 ); }

    void OnChatTextReceivedCh ( QString strChatText ) { CreateAndSendChatTextForAllConChannels ( slotId - 1, strChatText ); }

    void OnMuteStateHasChangedCh ( int iChanID, bool bIsMuted ) { CreateOtherMuteStateChanged ( slotId - 1, iChanID, bIsMuted ); }

    void OnServerAutoSockBufSizeChangeCh ( int iNNumFra ) { CreateAndSendJitBufMessage ( slotId - 1, iNNumFra ); }

protected:
    virtual void SendProtMessage ( int iChID, CVector<uint8_t> vecMessage ) = 0;

    virtual void CreateAndSendChanListForThisChan ( const int iCurChanID ) = 0;

    virtual void CreateAndSendChatTextForAllConChannels ( const int iCurChanID, const QString& strChatText ) = 0;

    virtual void CreateOtherMuteStateChanged ( const int iCurChanID, const int iOtherChanID, const bool bIsMuted ) = 0;

    virtual void CreateAndSendJitBufMessage ( const int iCurChanID, const int iNNumFra ) = 0;
};

template<>
class CServerSlots<0>
{};

// `CChannel` is deliberately the transport/session object.  The separate
// source object below is the normal visible mixer/recording primitive and
// never owns a socket, protocol instance or return encoder.
class CServerSource
{
public:
    CServerSource() : SignalLevelMeter ( false, 0.5 ) {}
    enum EState
    {
        SS_FREE,
        SS_RESERVED,
        SS_ACTIVE
    };

    void Reset()
    {
        eState           = SS_FREE;
        iParentSessionID = INVALID_INDEX;
        bLegacy          = false;
        Config           = CMultiSourceSourceConfig();
        iFadeInCnt       = 0;
        iFadeInCntMax    = 1;
        SignalLevelMeter.Reset();
    }

    void Reserve ( const int parentSessionID, const CMultiSourceSourceConfig& config, const bool legacy, const int fadeInCntMax )
    {
        eState           = SS_RESERVED;
        iParentSessionID = parentSessionID;
        bLegacy          = legacy;
        Config           = config;
        iFadeInCnt       = 0;
        iFadeInCntMax    = std::max ( 1, fadeInCntMax );
    }

    void Activate()
    {
        eState     = SS_ACTIVE;
        iFadeInCnt = 0;
    }
    bool                            IsAllocated() const { return eState != SS_FREE; }
    bool                            IsActive() const { return eState == SS_ACTIVE; }
    bool                            IsReserved() const { return eState == SS_RESERVED; }
    bool                            IsLegacy() const { return bLegacy; }
    int                             ParentSessionID() const { return iParentSessionID; }
    const CMultiSourceSourceConfig& GetConfig() const { return Config; }
    CMultiSourceSourceConfig&       GetConfig() { return Config; }

    float FadeInGain() const { return static_cast<float> ( iFadeInCnt ) / iFadeInCntMax; }
    void  AdvanceFade()
    {
        if ( iFadeInCnt < iFadeInCntMax )
            ++iFadeInCnt;
    }
    double UpdateAndGetLevelForMeterdB ( const CVector<int16_t>& data, const int samples, const bool stereo )
    {
        SignalLevelMeter.Update ( data, samples, stereo );
        return SignalLevelMeter.GetLevelForMeterdBLeftOrMono();
    }

    CChannelCoreInfo MakeVisibleInfo ( const CChannelCoreInfo& owner ) const
    {
        CChannelCoreInfo result = owner;
        if ( !bLegacy )
        {
            result.strName     = owner.strName + QStringLiteral ( " — " ) + Config.strTag;
            result.iInstrument = Config.iInstrument;
        }
        return result;
    }

private:
    EState                   eState           = SS_FREE;
    int                      iParentSessionID = INVALID_INDEX;
    bool                     bLegacy          = false;
    CMultiSourceSourceConfig Config;
    int                      iFadeInCnt    = 0;
    int                      iFadeInCntMax = 1;
    CStereoSignalLevelMeter  SignalLevelMeter;
};

class CServerSessionState
{
public:
    enum EState
    {
        ST_LEGACY,
        ST_PREPARED,
        ST_ACTIVE
    };

    CServerSessionState() : vecSourceIDs ( MAX_NUM_CHANNELS ) {}

    void Reset()
    {
        eState                    = ST_LEGACY;
        iLegacySourceID           = INVALID_INDEX;
        iNumSources               = 0;
        iGeneration               = 0;
        bHaveNextSequence         = false;
        iNextSequence             = 0;
        iFirstSequence            = 0;
        bIngressPrimed            = false;
        iIngressTargetFrames      = DEF_NET_BUF_SIZE_NUM_BL;
        bIngressAuto              = false;
        bAdvancedHalfFramePending = false;
        bPromotionQueued          = false;
        iPromotionFirstSequence   = 0;
        Ingress.Reset();
    }

    EState       eState          = ST_LEGACY;
    int          iLegacySourceID = INVALID_INDEX;
    CVector<int> vecSourceIDs; // reserved while prepared; active after first frame
    int          iNumSources               = 0;
    uint16_t     iGeneration               = 0;
    bool         bHaveNextSequence         = false;
    uint32_t     iNextSequence             = 0;
    uint32_t     iFirstSequence            = 0;
    bool         bIngressPrimed            = false;
    int          iIngressTargetFrames      = DEF_NET_BUF_SIZE_NUM_BL;
    bool         bIngressAuto              = false;
    bool         bAdvancedHalfFramePending = false;
    // The first multiplexed packet is received in the high-priority socket
    // thread.  Promotion, protocol messages and recorder/UI signals must be
    // deferred to CServer's QObject thread.
    bool                        bPromotionQueued        = false;
    uint32_t                    iPromotionFirstSequence = 0;
    MultiSource::SessionIngress Ingress;
};

class CServer : public QObject, public CServerSlots<MAX_NUM_CHANNELS>
{
    Q_OBJECT

public:
    CServer ( const int          iNewMaxNumChan,
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
              const ELicenceType eNLicenceType );

    virtual ~CServer();

    void Start();
    void Stop();
    bool IsRunning() { return HighPrecisionTimer.isActive(); }

    bool PutAudioData ( const CVector<uint8_t>& vecbyRecBuf, const int iNumBytesRead, const CHostAddress& HostAdr, int& iCurChanID );

    int GetNumberOfConnectedClients();

    void GetConCliParam ( CVector<CHostAddress>&     vecHostAddresses,
                          CVector<QString>&          vecsName,
                          CVector<int>&              veciJitBufNumFrames,
                          CVector<int>&              veciNetwFrameSizeFact,
                          CVector<CChannelCoreInfo>& vecChanInfo );

    void CreateCLServerListReqVerAndOSMes ( const CHostAddress& InetAddr ) { ConnLessProtocol.CreateCLReqVersionAndOSMes ( InetAddr ); }

    // IPv6 Available
    bool IsIPv6Available() { return bIPv6Available; }

    // GUI settings ------------------------------------------------------------
    int GetClientNumAudioChannels ( const int iChanNum )
    {
        return MathUtils::InRange<int> ( iChanNum, 0, MAX_NUM_CHANNELS - 1 ) && vecSources[iChanNum].IsActive()
                   ? vecSources[iChanNum].GetConfig().iNumChannels
                   : 0;
    }
    int GetNumberOfConnectedSessions();

    void           SetDirectoryType ( const EDirectoryType eNCSAT ) { ServerListManager.SetDirectoryType ( eNCSAT ); }
    EDirectoryType GetDirectoryType() { return ServerListManager.GetDirectoryType(); }
    bool           IsDirectory() { return ServerListManager.IsDirectory(); }
    ESvrRegStatus  GetSvrRegStatus() { return ServerListManager.GetSvrRegStatus(); }

    void             SetServerName ( const QString& strNewName ) { ServerListManager.SetServerName ( strNewName ); }
    QString          GetServerName() { return ServerListManager.GetServerName(); }
    void             SetServerCity ( const QString& strNewCity ) { ServerListManager.SetServerCity ( strNewCity ); }
    QString          GetServerCity() { return ServerListManager.GetServerCity(); }
    void             SetServerCountry ( const QLocale::Country eNewCountry ) { ServerListManager.SetServerCountry ( eNewCountry ); }
    QLocale::Country GetServerCountry() { return ServerListManager.GetServerCountry(); }

    bool    GetRecorderInitialised() { return JamController.GetRecorderInitialised(); }
    void    SetEnableRecording ( bool bNewEnableRecording );
    bool    GetDisableRecording() { return bDisableRecording; }
    QString GetRecorderErrMsg() { return JamController.GetRecorderErrMsg(); }
    bool    GetRecordingEnabled() { return JamController.GetRecordingEnabled(); }
    void    RequestNewRecording() { JamController.RequestNewRecording(); }
    void    SetRecordingDir ( QString newRecordingDir )
    {
        JamController.SetRecordingDir ( newRecordingDir, iServerFrameSizeSamples, bDisableRecording );
    }
    QString GetRecordingDir() { return JamController.GetRecordingDir(); }

    void    SetWelcomeMessage ( const QString& strNWelcMess );
    QString GetWelcomeMessage() { return strWelcomeMessage; }

    void    SetDirectoryAddress ( const QString& sNDirectoryAddress ) { ServerListManager.SetDirectoryAddress ( sNDirectoryAddress ); }
    QString GetDirectoryAddress() { return ServerListManager.GetDirectoryAddress(); }

    QString GetServerListFileName() { return ServerListManager.GetServerListFileName(); }
    bool    SetServerListFileName ( QString strFilename ) { return ServerListManager.SetServerListFileName ( strFilename ); }

    void SetAutoRunMinimized ( const bool NAuRuMin ) { bAutoRunMinimized = NAuRuMin; }
    bool GetAutoRunMinimized() { return bAutoRunMinimized; }

    void SetEnableDelayPanning ( bool bDelayPanningOn ) { bDelayPan = bDelayPanningOn; }
    bool IsDelayPanningEnabled() { return bDelayPan; }

    void SendChatTextToAllConChannels ( const int iSendingChanID, const QString& strChatText );
    bool SendChatTextToConChannel ( const int iCurChanID, const QString& strChatText );

protected:
    // A session is a CChannel endpoint; a source is an ordinary visible fader.
    bool IsConnected ( const int iSessionID ) { return vecSessions[iSessionID].IsConnected(); }

    int                   FindChannel ( const CHostAddress& checkAddr, const bool bAllowNew = false );
    void                  InitChannel ( const int iNewSessionID, const CHostAddress& inetAddr );
    void                  FreeChannel ( const int iCurSessionID );
    int                   AllocateSource ( const int parentSessionID, const CMultiSourceSourceConfig& config, const bool legacy, const bool active );
    void                  FreeSource ( const int sourceID );
    void                  FreeAllSourcesForSession ( const int sessionID );
    CChannelCoreInfo      GetSourceInfo ( const int sourceID ) const;
    int                   GetLegacySourceID ( const int sessionID ) const;
    bool                  PrepareAdvancedSources ( const int sessionID, const CVector<CMultiSourceSourceConfig>& config, uint8_t& rejectReason );
    bool                  PutAdvancedAudioData ( const CVector<uint8_t>& packet, const int packetBytes, const CHostAddress& address, int& sessionID );
    bool                  ActivatePreparedSources ( const int sessionID, const uint16_t generation, const uint32_t firstSequence );
    void                  OnReqMultiSourceCaps ( const int sessionID );
    void                  OnMultiSourceConfig ( const int sessionID, CVector<CMultiSourceSourceConfig> config );
    void                  DumpChannels ( const QString& title );
    CVector<CChannelInfo> CreateChannelList();

    virtual void CreateAndSendChanListForAllConChannels();
    virtual void CreateAndSendChanListForThisChan ( const int iCurChanID );

    virtual void CreateAndSendChatTextForAllConChannels ( const int iCurChanID, const QString& strChatText );

    virtual void CreateOtherMuteStateChanged ( const int iCurChanID, const int iOtherChanID, const bool bIsMuted );

    virtual void CreateAndSendJitBufMessage ( const int iCurChanID, const int iNNumFra );

    virtual void SendProtMessage ( int iChID, CVector<uint8_t> vecMessage );

    template<unsigned int slotId>
    inline void connectChannelSignalsToServerSlots();

    static void DecodeReceiveDataBlocks ( CServer* pServer, const int startSource, const int stopSource, const int numSources );

    static void MixEncodeTransmitDataBlocks ( CServer* pServer, const int startSession, const int stopSession, const int numSources );

    void DecodeReceiveData ( const int sourceIndex, const int numSources );

    void MixEncodeTransmitData ( const int sessionIndex, const int numSources );
    bool ReadAdvancedFrame ( const int sessionID, const int blockIndex );
    bool SetAdvancedIngressTarget ( int sessionID, int numFrames );
    void UpdateAdvancedIngressAutoPolicy ( int sessionID );
    void OnSessionJitterPolicyChanged ( int sessionID, int numFrames, bool bAuto );
    void QueueAdvancedPromotion ( int sessionID, uint32_t firstSequence );
    void QueueAdvancedJitterReport ( int sessionID, int numFrames );

    virtual void customEvent ( QEvent* pEvent );

    void CreateAndSendRecorderStateForAllConChannels();

    // if server mode is normal or double system frame size
    bool bUseDoubleSystemFrameSize;
    int  iServerFrameSizeSamples;

    // variables needed for multithreading support
    bool                       bUseMultithreading;
    int                        iMaxNumThreads;
    CVector<std::future<void>> Futures;

    bool CreateLevelsForAllConChannels ( const int                       iNumClients,
                                         const CVector<int>&             vecNumAudioChannels,
                                         const CVector<CVector<int16_t>> vecvecsData,
                                         CVector<uint16_t>&              vecLevelsOut );

    // Session slots retain the established CChannel protocol/socket lifetime.
    CChannel            vecSessions[MAX_NUM_CHANNELS];
    CServerSessionState vecSessionState[MAX_NUM_CHANNELS];
    CServerSource       vecSources[MAX_NUM_CHANNELS];
    int                 iMaxNumChannels; // visible-source cap from server configuration

    int    iCurNumSessions;
    int    vecSessionOrder[MAX_NUM_CHANNELS]; // address-sorted active session slots
    int    iCurNumSources;                    // active plus reserved, never exceeds iMaxNumChannels
    QMutex MutexChanOrder;

    CProtocol ConnLessProtocol;
    QMutex    Mutex;
    QMutex    MutexWelcomeMessage;
    bool      bChannelIsNowDisconnected;

    // audio encoder/decoder
    OpusCustomMode*    Opus64Mode[MAX_NUM_CHANNELS];
    OpusCustomEncoder* Opus64EncoderMono[MAX_NUM_CHANNELS];
    OpusCustomDecoder* Opus64DecoderMono[MAX_NUM_CHANNELS];
    OpusCustomEncoder* Opus64EncoderStereo[MAX_NUM_CHANNELS];
    OpusCustomDecoder* Opus64DecoderStereo[MAX_NUM_CHANNELS];
    OpusCustomMode*    OpusMode[MAX_NUM_CHANNELS];
    OpusCustomEncoder* OpusEncoderMono[MAX_NUM_CHANNELS];
    OpusCustomDecoder* OpusDecoderMono[MAX_NUM_CHANNELS];
    OpusCustomEncoder* OpusEncoderStereo[MAX_NUM_CHANNELS];
    OpusCustomDecoder* OpusDecoderStereo[MAX_NUM_CHANNELS];
    CConvBuf<int16_t>  DoubleFrameSizeConvBufIn[MAX_NUM_CHANNELS];
    CConvBuf<int16_t>  DoubleFrameSizeConvBufOut[MAX_NUM_CHANNELS];

    // needed for disabling raw audio transmission
    bool bDisableRaw;

    CVector<QString> vstrChatColors;
    CVector<int>     vecChanIDsCurConChan;       // visible source IDs for current frame
    CVector<int>     vecSessionIDsCurConSession; // physical target sessions for current frame

    CVector<CVector<float>>   vecvecfGains;
    CVector<CVector<float>>   vecvecfPannings;
    CVector<CVector<int16_t>> vecvecsData;
    CVector<CVector<int16_t>> vecvecsData2;
    CVector<int>              vecNumAudioChannels;
    CVector<int>              vecNumFrameSizeConvBlocks;
    CVector<int>              vecUseDoubleSysFraSizeConvBuf;
    CVector<EAudComprType>    vecAudioComprType;
    CVector<CVector<int16_t>> vecvecsSendData;
    CVector<CVector<float>>   vecvecfIntermediateProcBuf;
    CVector<CVector<uint8_t>> vecvecbyCodedData;
    // Advanced ingress is stored by actual visible source ID so a session frame
    // can be decoded once and consumed by several faders without a second jitter buffer.
    CVector<CVector<uint8_t>> vecSourceIngressPayload;
    CVector<uint8_t>          vecSourceIngressPresent; // [source id * 2 + codec-frame block]

    // Channel levels
    CVector<uint16_t> vecChannelLevels;

    // actual working objects
    bool            bIPv6Available; // must be before Socket - passed by reference to Socket
    CHighPrioSocket Socket;

    // logging
    CServerLogging Logging;

    // channel level update frame interval counter
    int iFrameCount;

    CHighPrecisionTimer HighPrecisionTimer;

    // server list
    CServerListManager ServerListManager;

    // jam recorder
    recorder::CJamController JamController;
    bool                     bDisableRecording;

    // GUI settings
    bool bAutoRunMinimized;

    // for delay panning
    bool bDelayPan;

    // messaging
    QString      strWelcomeMessage;
    ELicenceType eLicenceType;
    bool         bDisconnectAllClientsOnQuit;

    CSignalHandler* pSignalHandler;

    std::unique_ptr<CThreadPool> pThreadPool;

signals:
    void Started();
    void Stopped();
    void ClientDisconnected ( const int iChID );
    void ClientConnected ( const int iChID, const QHostAddress RecHostAddr, const int iTotChans );
    void sentChatMessage ( const int iSendingChanID, const QString& strChatText );
    void SvrRegStatusChanged();
    void AudioFrame ( const int              iChID,
                      const QString          stChName,
                      const CHostAddress     RecHostAddr,
                      const int              iNumAudChan,
                      const CVector<int16_t> vecsData );

    void CLVersionAndOSReceived ( CHostAddress InetAddr, COSUtil::EOpSystemType eOSType, QString strVersion );

    // pass through from jam controller
    void RestartRecorder();
    void StopRecorder();
    void RecordingSessionStarted ( QString sessionDir );
    void EndRecorderThread();

public slots:
    void OnTimer();

    void OnNewConnection ( int iChID, int iTotChans, CHostAddress RecHostAddr );

    void OnServerFull ( CHostAddress RecHostAddr );

    void OnSendCLProtMessage ( CHostAddress InetAddr, CVector<uint8_t> vecMessage );

    void OnProtocolCLMessageReceived ( int iRecID, CVector<uint8_t> vecbyMesBodyData, CHostAddress RecHostAddr );

    void OnProtocolMessageReceived ( int iRecCounter, int iRecID, CVector<uint8_t> vecbyMesBodyData, CHostAddress RecHostAddr );

    void OnCLPingReceived ( CHostAddress InetAddr, int iMs ) { ConnLessProtocol.CreateCLPingMes ( InetAddr, iMs ); }

    void OnCLPingWithNumClientsReceived ( CHostAddress InetAddr, int iMs, int )
    {
        ConnLessProtocol.CreateCLPingWithNumClientsMes ( InetAddr, iMs, GetNumberOfConnectedClients() );
    }

    void OnCLSendEmptyMes ( CHostAddress TargetInetAddr )
    {
        // only send empty message if not a directory
        if ( !ServerListManager.IsDirectory() )
        {
            ConnLessProtocol.CreateCLEmptyMes ( TargetInetAddr );
        }
    }

    void OnCLReqServerList ( CHostAddress InetAddr ) { ServerListManager.RetrieveAll ( InetAddr ); }

    void OnCLReqVersionAndOS ( CHostAddress InetAddr ) { ConnLessProtocol.CreateCLVersionAndOSMes ( InetAddr ); }

    void OnCLReqConnClientsList ( CHostAddress InetAddr ) { ConnLessProtocol.CreateCLConnClientsListMes ( InetAddr, CreateChannelList() ); }

    void OnCLRegisterServerReceived ( CHostAddress InetAddr, CHostAddress LInetAddr, CServerCoreInfo ServerInfo )
    {
        ServerListManager.Append ( InetAddr, LInetAddr, ServerInfo );
    }

    void OnCLRegisterServerExReceived ( CHostAddress    InetAddr,
                                        CHostAddress    LInetAddr,
                                        CServerCoreInfo ServerInfo,
                                        COSUtil::EOpSystemType,
                                        QString strVersion )
    {
        ServerListManager.Append ( InetAddr, LInetAddr, ServerInfo, strVersion );
    }

    void OnCLRegisterServerResp ( CHostAddress /* unused */, ESvrRegResult eResult ) { ServerListManager.StoreRegistrationResult ( eResult ); }

    void OnCLUnregisterServerReceived ( CHostAddress InetAddr ) { ServerListManager.Remove ( InetAddr ); }

    void OnCLReqServerFeatures ( CHostAddress InetAddr );

    void OnCLReqWelcomeMessage ( CHostAddress InetAddr );

    void OnCLDisconnection ( CHostAddress InetAddr );

    void OnAboutToQuit();

    void OnHandledSignal ( int sigNum );
};

Q_DECLARE_METATYPE ( CVector<int16_t> )
