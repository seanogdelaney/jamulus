/* Focused regression tests for legacy compatibility and Poly-in server lifecycle. */
#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../src/server.h"

namespace
{
constexpr int kOpusMonoBytes      = 45;
constexpr int kOpusStereoBytes    = 71;
constexpr int kOpus64MonoBytes    = 22;
constexpr int kOpus64StereoBytes  = 35;
constexpr int kJitterBufferFrames = 4;

CHostAddress TestAddress ( const quint16 port ) { return CHostAddress ( QHostAddress::LocalHost, port ); }

int CodedBytes ( const EAudComprType codec, const int channels, const bool raw )
{
    const int frameSamples = codec == CT_OPUS ? DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES : SYSTEM_FRAME_SIZE_SAMPLES;
    if ( raw )
        return static_cast<int> ( sizeof ( int16_t ) ) * frameSamples * channels;
    if ( codec == CT_OPUS )
        return channels == 1 ? kOpusMonoBytes : kOpusStereoBytes;
    return channels == 1 ? kOpus64MonoBytes : kOpus64StereoBytes;
}

std::vector<int16_t> MakePCM ( const int firstFrame, const int frames, const int channels )
{
    std::vector<int16_t> pcm ( static_cast<size_t> ( frames * channels ) );
    for ( int frame = 0; frame < frames; ++frame )
    {
        const int phase = ( firstFrame + frame ) % 32;
        const int left  = ( phase - 16 ) * 420;
        if ( channels == 1 )
        {
            pcm[static_cast<size_t> ( frame )] = static_cast<int16_t> ( left );
        }
        else
        {
            const int right                            = ( ( ( firstFrame + frame + 9 ) % 29 ) - 14 ) * -360;
            pcm[static_cast<size_t> ( 2 * frame )]     = static_cast<int16_t> ( left );
            pcm[static_cast<size_t> ( 2 * frame + 1 )] = static_cast<int16_t> ( right );
        }
    }
    return pcm;
}

qint64 ChannelEnergy ( const std::vector<int16_t>& pcm, const int channels, const int channel )
{
    qint64 energy = 0;
    for ( size_t index = static_cast<size_t> ( channel ); index < pcm.size(); index += static_cast<size_t> ( channels ) )
        energy += std::abs ( static_cast<int> ( pcm[index] ) );
    return energy;
}

CVector<CPolyInSourceConfig> MakePolyInConfig ( const int count, const EAudComprType codec = CT_OPUS, const bool raw = false )
{
    CVector<CPolyInSourceConfig> config ( count );
    const int                    frameSamples = codec == CT_OPUS ? DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES : SYSTEM_FRAME_SIZE_SAMPLES;
    for ( int index = 0; index < count; ++index )
    {
        CPolyInSourceConfig& source = config[index];
        source.iKey                 = static_cast<uint8_t> ( index + 1 );
        source.iNumChannels         = 1;
        source.eCodec               = codec;
        source.bRaw                 = raw;
        source.iPayloadBytes =
            raw ? static_cast<uint16_t> ( sizeof ( int16_t ) * frameSamples ) : static_cast<uint16_t> ( CodedBytes ( codec, 1, false ) );
        source.strTag = QStringLiteral ( "source-%1" ).arg ( index + 1 );
    }
    return config;
}
} // namespace

class CServerTestHarness : public CServer
{
public:
    explicit CServerTestHarness ( const bool useDoubleSystemFrameSize,
                                  const int  maxChannels = MAX_NUM_CHANNELS,
                                  const int  maxSessions = MAX_NUM_CHANNELS,
                                  const bool disableRaw  = false ) :
        CServer ( maxChannels,
                  maxSessions,
                  QString(),
                  QStringLiteral ( "127.0.0.1" ),
                  0,
                  0,
                  QString(),
                  QString(),
                  QString(),
                  QString(),
                  QString(),
                  QString(),
                  QString(),
                  false,
                  useDoubleSystemFrameSize,
                  disableRaw,
                  false,
                  true,
                  false,
                  true,
                  LT_NO_LICENCE )
    {}

    int ReserveLegacySession ( const CHostAddress& address ) { return FindChannel ( address, true ); }

    int CreateMalformedSession ( const CHostAddress& address )
    {
        CVector<uint8_t> packet ( 3 );
        packet[0]            = 0x12;
        packet[1]            = 0x34;
        packet[2]            = 0x56;
        int        sessionID = INVALID_CHANNEL_ID;
        const bool isNew     = PutAudioData ( packet, packet.Size(), address, sessionID );
        return isNew ? sessionID : INVALID_CHANNEL_ID;
    }

    void ConfigureLegacySession ( const int           sessionID,
                                  const EAudComprType codec,
                                  const int           channels,
                                  const int           codedBytes,
                                  const int           networkFrameFactor )
    {
        vecSessions[sessionID].SetSockBufNumFrames ( kJitterBufferFrames );
        vecSessions[sessionID].OnNetTranspPropsReceived ( CNetworkTransportProps ( static_cast<uint32_t> ( codedBytes ),
                                                                                   static_cast<uint16_t> ( networkFrameFactor ),
                                                                                   static_cast<uint32_t> ( channels ),
                                                                                   SYSTEM_SAMPLE_RATE_HZ,
                                                                                   codec,
                                                                                   NF_NONE,
                                                                                   0 ) );
        CChannelCoreInfo info;
        info.strName = QStringLiteral ( "legacy-test" );
        vecSessions[sessionID].SetChanInfo ( info );
    }

    EPutDataStat PutLegacyPacket ( const int sessionID, const CVector<uint8_t>& packet )
    {
        return vecSessions[sessionID].PutAudioData ( packet, packet.Size(), vecSessions[sessionID].GetAddress() );
    }

    std::vector<int16_t> DecodeLegacyBlock ( const int sourceID, const int sessionID )
    {
        DecodeLegacySource ( 0, sourceID, sessionID );
        const int            channels = vecNumAudioChannels[0];
        std::vector<int16_t> output ( static_cast<size_t> ( iServerFrameSizeSamples * channels ) );
        std::copy_n ( &vecvecsData[0][0], output.size(), output.begin() );
        return output;
    }

    int EncodeLegacyFrame ( const int           sessionID,
                            const EAudComprType codec,
                            const int           channels,
                            const int16_t*      pcm,
                            const int           codedBytes,
                            uint8_t*            output )
    {
        OpusCustomEncoder* encoder = nullptr;
        if ( codec == CT_OPUS )
            encoder = channels == 1 ? OpusEncoderMono[sessionID] : OpusEncoderStereo[sessionID];
        else
            encoder = channels == 1 ? Opus64EncoderMono[sessionID] : Opus64EncoderStereo[sessionID];
        const int frameSamples = codec == CT_OPUS ? DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES : SYSTEM_FRAME_SIZE_SAMPLES;
        opus_custom_encoder_ctl ( encoder, OPUS_SET_BITRATE ( CalcBitRateBitsPerSecFromCodedBytes ( codedBytes, frameSamples ) ) );
        return opus_custom_encode ( encoder, pcm, frameSamples, output, codedBytes );
    }

    void EnablePolyIn ( const int sessionID ) { vecSessions[sessionID].OnSplitMessSupported(); }

    void ConfigurePolyInCapableSession ( const int sessionID, const int channels = 2 )
    {
        ConfigureLegacySession ( sessionID, CT_OPUS, channels, CodedBytes ( CT_OPUS, channels, false ), 1 );
        vecSessions[sessionID].ResetTimeOutCounter();
        EnablePolyIn ( sessionID );
    }

    bool PreparePolyIn ( const int sessionID, const CVector<CPolyInSourceConfig>& config, uint8_t& rejectReason )
    {
        return PreparePolyInSources ( sessionID, config, rejectReason );
    }

    bool PutPolyInFrame ( const int sessionID, const uint16_t generation, const uint32_t sequence, const CVector<CPolyInSourceConfig>& config )
    {
        std::vector<std::vector<uint8_t>> payloads ( static_cast<size_t> ( config.Size() ) );
        std::vector<PolyIn::RecordView>   records ( static_cast<size_t> ( config.Size() ) );
        for ( int index = 0; index < config.Size(); ++index )
        {
            payloads[static_cast<size_t> ( index )].assign ( config[index].iPayloadBytes, static_cast<uint8_t> ( 0x20 + index ) );
            records[static_cast<size_t> ( index )] =
                PolyIn::RecordView{ config[index].iKey, payloads[static_cast<size_t> ( index )].data(), config[index].iPayloadBytes };
        }

        PolyIn::FramePacketizer packetizer;
        const PolyIn::Datagram* datagrams     = nullptr;
        size_t                  datagramCount = 0;
        if ( !packetizer.Packetize ( generation, sequence, config[0].bRaw, records.data(), records.size(), datagrams, datagramCount ) )
        {
            return false;
        }

        for ( size_t index = 0; index < datagramCount; ++index )
        {
            CVector<uint8_t> packet ( static_cast<int> ( datagrams[index].length ) );
            std::memcpy ( &packet[0], datagrams[index].bytes.data(), datagrams[index].length );
            int resolvedSessionID = INVALID_CHANNEL_ID;
            PutAudioData ( packet, packet.Size(), vecSessions[sessionID].GetAddress(), resolvedSessionID );
            if ( resolvedSessionID != sessionID )
                return false;
        }
        return true;
    }

    void ProcessPostedServerEvents() { QCoreApplication::sendPostedEvents ( this, QEvent::User + 11 ); }

    int                         LegacySourceID ( const int sessionID ) const { return GetLegacySourceID ( sessionID ); }
    int                         SessionCount() const { return iCurNumSessions; }
    int                         SourceCount() const { return iCurNumSources; }
    int                         VisibleSourceCount() { return CreateChannelList().Size(); }
    int                         VisibleSourceID ( const int index ) { return CreateChannelList()[index].iChanID; }
    CServerSessionState::EState SessionState ( const int sessionID ) const { return vecSessionState[sessionID].eState; }
    uint16_t                    Generation ( const int sessionID ) const { return vecSessionState[sessionID].iGeneration; }
    int                         PreparedSourceCount ( const int sessionID ) const { return vecSessionState[sessionID].iNumSources; }
    int   PreparedSourceID ( const int sessionID, const int index ) const { return vecSessionState[sessionID].vecSourceIDs[index]; }
    bool  PromotionQueued ( const int sessionID ) const { return vecSessionState[sessionID].bPromotionQueued; }
    bool  SourceIsActive ( const int sourceID ) const { return vecSources[sourceID].IsActive(); }
    bool  SourceIsReserved ( const int sourceID ) const { return vecSources[sourceID].IsReserved(); }
    int   SourceParentSession ( const int sourceID ) const { return vecSources[sourceID].ParentSessionID(); }
    int   SourceFaderID ( const int sourceID ) const { return vecSources[sourceID].GetConfig().iFaderID; }
    void  AcknowledgePolyInAccept ( const int sessionID ) { OnPolyInReliableMessageAcknowledged ( sessionID, PROTMESSID_POLY_IN_ACCEPT ); }
    bool  PreparedExpiryIsArmed ( const int sessionID ) const { return vecSessionState[sessionID].iPreparedExpirySamplesRemaining > 0; }
    void  ArmPreparedForExpiry ( const int sessionID ) { vecSessionState[sessionID].iPreparedExpirySamplesRemaining = iServerFrameSizeSamples; }
    void  FreeTestSession ( const int sessionID ) { FreeChannel ( sessionID ); }
    void  ArmSessionForExpiry ( const int sessionID ) { vecSessions[sessionID].Disconnect(); }
    int   DecodedChannels() const { return vecNumAudioChannels[0]; }
    int   DecodedCodec() const { return static_cast<int> ( vecAudioComprType[0] ); }
    int   SessionCodec ( const int sessionID ) const { return static_cast<int> ( vecSessions[sessionID].GetAudioCompressionType() ); }
    float SessionFade ( const int sessionID ) const { return vecSessions[sessionID].GetFadeInGain(); }
    float VisibleSourceFade ( const int sourceID ) const { return vecSources[sourceID].FadeInGain(); }
    float EffectiveSourceFade ( const int sourceID ) const { return GetSourceFadeInGain ( sourceID ); }
    void  AdvanceVisibleSourceFade ( const int sourceID, const int frames ) { vecSources[sourceID].AdvanceFade ( frames ); }

    void NotifyMute ( const int mutingSessionID, const int mutedSourceID, const bool muted )
    {
        CreateOtherMuteStateChanged ( mutingSessionID, mutedSourceID, muted );
    }

    void ClearSentMessages()
    {
        sentSessionIDs.clear();
        sentMessages.clear();
    }

    std::vector<int>              sentSessionIDs;
    std::vector<CVector<uint8_t>> sentMessages;

protected:
    void SendProtMessage ( int sessionID, CVector<uint8_t> message ) override
    {
        sentSessionIDs.push_back ( sessionID );
        sentMessages.push_back ( message );
    }
};

class ServerLegacyTest : public QObject
{
    Q_OBJECT

private slots:
    void phantomExpiry_data()
    {
        QTest::addColumn<bool> ( "doubleFrames" );
        QTest::newRow ( "128-sample-server" ) << true;
        QTest::newRow ( "64-sample-server" ) << false;
    }

    void phantomExpiry()
    {
        QFETCH ( bool, doubleFrames );
        CServerTestHarness server ( doubleFrames );
        const int          sessionID = server.CreateMalformedSession ( TestAddress ( doubleFrames ? 41001 : 41002 ) );

        QVERIFY ( sessionID >= 0 && sessionID < MAX_NUM_CHANNELS );
        QCOMPARE ( server.SessionCount(), 1 );
        QCOMPARE ( server.SourceCount(), 1 );
        QCOMPARE ( server.SessionCodec ( sessionID ), static_cast<int> ( CT_NONE ) );

        // Put the sample-based timeout on its final tick, then exercise that
        // tick through CServer::OnTimer. The source has no configured codec,
        // so this fails if lifetime again depends on reaching CChannel::GetData()
        // from a legacy decoder.
        server.ArmSessionForExpiry ( sessionID );
        server.OnTimer();
        QCOMPARE ( server.SessionCount(), 0 );
        QCOMPARE ( server.SourceCount(), 0 );
    }

    void muteNotificationUsesMutingIdentity()
    {
        CServerTestHarness server ( true );
        const int          mutingSession = server.CreateMalformedSession ( TestAddress ( 41011 ) );
        const int          mutedSession  = server.CreateMalformedSession ( TestAddress ( 41012 ) );
        const int          mutingSource  = server.LegacySourceID ( mutingSession );
        const int          mutedSource   = server.LegacySourceID ( mutedSession );

        server.ClearSentMessages();
        server.NotifyMute ( mutingSession, mutedSource, true );

        QCOMPARE ( static_cast<int> ( server.sentMessages.size() ), 1 );
        QCOMPARE ( server.sentSessionIDs[0], mutedSession );

        CVector<uint8_t> body;
        int              counter   = 0;
        int              messageID = 0;
        QVERIFY ( !CProtocol::ParseMessageFrame ( server.sentMessages[0], server.sentMessages[0].Size(), body, counter, messageID ) );
        QCOMPARE ( messageID, PROTMESSID_MUTE_STATE_CHANGED );
        QCOMPARE ( body.Size(), 2 );
        QCOMPARE ( static_cast<int> ( body[0] ), mutingSource );
        QCOMPARE ( static_cast<int> ( body[1] ), 1 );
    }

    void legacyFadeIsPacketDriven()
    {
        CServerTestHarness server ( true );
        const int          sessionID  = server.ReserveLegacySession ( TestAddress ( 41021 ) );
        const int          sourceID   = server.LegacySourceID ( sessionID );
        const int          codedBytes = CodedBytes ( CT_OPUS, 1, true );
        server.ConfigureLegacySession ( sessionID, CT_OPUS, 1, codedBytes, 1 );

        const std::vector<int16_t> pcm = MakePCM ( 0, DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES, 1 );
        CVector<uint8_t>           packet ( codedBytes );
        std::memcpy ( &packet[0], pcm.data(), static_cast<size_t> ( codedBytes ) );

        QCOMPARE ( static_cast<int> ( server.PutLegacyPacket ( sessionID, packet ) ), static_cast<int> ( PS_NEW_CONNECTION ) );
        QCOMPARE ( server.SessionFade ( sessionID ), 0.0f );
        server.DecodeLegacyBlock ( sourceID, sessionID );
        for ( int tick = 0; tick < 20; ++tick )
            server.DecodeLegacyBlock ( sourceID, sessionID );

        QCOMPARE ( server.SessionFade ( sessionID ), 0.0f );
        QCOMPARE ( server.VisibleSourceFade ( sourceID ), 0.0f );

        // A legacy source-local fade must be ignored even if it is non-zero;
        // the physical CChannel's packet-driven fade is the standard contract.
        server.AdvanceVisibleSourceFade ( sourceID, 100 );
        QVERIFY ( server.VisibleSourceFade ( sourceID ) > 0.0f );
        QCOMPARE ( server.EffectiveSourceFade ( sourceID ), 0.0f );

        QCOMPARE ( static_cast<int> ( server.PutLegacyPacket ( sessionID, packet ) ), static_cast<int> ( PS_AUDIO_OK ) );
        QVERIFY ( server.SessionFade ( sessionID ) > 0.0f );
        QCOMPARE ( server.EffectiveSourceFade ( sourceID ), server.SessionFade ( sessionID ) );
    }

    void polyInChannelProfilesKeepFallbackStereo()
    {
        const PolyIn::ChannelProfiles mono = PolyIn::ResolveChannelProfiles ( CC_MONO, CC_STEREO );
        QCOMPARE ( static_cast<int> ( mono.input ), static_cast<int> ( CC_MONO ) );
        QCOMPARE ( static_cast<int> ( mono.transport ), static_cast<int> ( CC_MONO ) );

        const PolyIn::ChannelProfiles savedMono = PolyIn::ResolveChannelProfiles ( CC_POLY_IN, CC_MONO );
        QCOMPARE ( static_cast<int> ( savedMono.input ), static_cast<int> ( CC_MONO ) );
        QCOMPARE ( static_cast<int> ( savedMono.transport ), static_cast<int> ( CC_STEREO ) );

        const PolyIn::ChannelProfiles savedMonoStereoOut = PolyIn::ResolveChannelProfiles ( CC_POLY_IN, CC_MONO_IN_STEREO_OUT );
        QCOMPARE ( static_cast<int> ( savedMonoStereoOut.input ), static_cast<int> ( CC_MONO_IN_STEREO_OUT ) );
        QCOMPARE ( static_cast<int> ( savedMonoStereoOut.transport ), static_cast<int> ( CC_STEREO ) );

        const PolyIn::ChannelProfiles savedStereo = PolyIn::ResolveChannelProfiles ( CC_POLY_IN, CC_STEREO );
        QCOMPARE ( static_cast<int> ( savedStereo.input ), static_cast<int> ( CC_STEREO ) );
        QCOMPARE ( static_cast<int> ( savedStereo.transport ), static_cast<int> ( CC_STEREO ) );
    }

    void polyInRequiresStereoPhysicalSession()
    {
        CServerTestHarness                 server ( true );
        const int                          sessionID    = server.ReserveLegacySession ( TestAddress ( 41031 ) );
        const CVector<CPolyInSourceConfig> config       = MakePolyInConfig ( 2 );
        uint8_t                            rejectReason = 0;

        server.ConfigurePolyInCapableSession ( sessionID, 1 );
        QVERIFY ( !server.PreparePolyIn ( sessionID, config, rejectReason ) );
        QCOMPARE ( static_cast<int> ( rejectReason ), static_cast<int> ( PolyInProtocol::kRejectStereoReturnRequired ) );
        QCOMPARE ( static_cast<int> ( server.SessionState ( sessionID ) ), static_cast<int> ( CServerSessionState::ST_LEGACY ) );
        QCOMPARE ( server.SourceCount(), 1 );

        server.ConfigurePolyInCapableSession ( sessionID, 2 );
        QVERIFY ( server.PreparePolyIn ( sessionID, config, rejectReason ) );
        QCOMPARE ( static_cast<int> ( server.SessionState ( sessionID ) ), static_cast<int> ( CServerSessionState::ST_PREPARED ) );
    }

    void polyInReservationAndPromotionAreAtomic()
    {
        CServerTestHarness                 server ( true );
        const int                          sessionID      = server.ReserveLegacySession ( TestAddress ( 41041 ) );
        const int                          legacySourceID = server.LegacySourceID ( sessionID );
        const CVector<CPolyInSourceConfig> config         = MakePolyInConfig ( 2 );
        uint8_t                            rejectReason   = 0;

        server.ConfigurePolyInCapableSession ( sessionID );
        QVERIFY ( server.PreparePolyIn ( sessionID, config, rejectReason ) );
        const uint16_t generation = server.Generation ( sessionID );
        QVERIFY ( generation != 0 );
        QCOMPARE ( server.PreparedSourceCount ( sessionID ), 2 );
        QCOMPARE ( server.SourceCount(), 3 );
        QCOMPARE ( server.VisibleSourceCount(), 1 );
        QCOMPARE ( server.VisibleSourceID ( 0 ), legacySourceID );

        uint8_t secondPrepareReason = 0;
        QVERIFY ( !server.PreparePolyIn ( sessionID, config, secondPrepareReason ) );
        QCOMPARE ( static_cast<int> ( secondPrepareReason ), static_cast<int> ( PolyInProtocol::kRejectInvalidSessionState ) );
        QCOMPARE ( server.SourceCount(), 3 );
        QCOMPARE ( server.PreparedSourceCount ( sessionID ), 2 );

        std::array<int, 2> reservedIDs{};
        for ( int index = 0; index < 2; ++index )
        {
            reservedIDs[static_cast<size_t> ( index )] = server.PreparedSourceID ( sessionID, index );
            const int sourceID                         = reservedIDs[static_cast<size_t> ( index )];
            QVERIFY ( server.SourceIsReserved ( sourceID ) );
            QVERIFY ( !server.SourceIsActive ( sourceID ) );
            QCOMPARE ( server.SourceParentSession ( sourceID ), sessionID );
            QCOMPARE ( server.SourceFaderID ( sourceID ), sourceID );
        }

        const uint16_t staleGeneration = generation == 0xffff ? static_cast<uint16_t> ( generation - 1 ) : static_cast<uint16_t> ( generation + 1 );
        QVERIFY ( server.PutPolyInFrame ( sessionID, staleGeneration, 90, config ) );
        QVERIFY ( !server.PromotionQueued ( sessionID ) );
        server.ProcessPostedServerEvents();
        QCOMPARE ( static_cast<int> ( server.SessionState ( sessionID ) ), static_cast<int> ( CServerSessionState::ST_PREPARED ) );
        QCOMPARE ( server.VisibleSourceCount(), 1 );

        QVERIFY ( server.PutPolyInFrame ( sessionID, generation, 91, config ) );
        QVERIFY ( server.PromotionQueued ( sessionID ) );
        QCOMPARE ( server.VisibleSourceCount(), 1 );
        server.ProcessPostedServerEvents();

        QCOMPARE ( static_cast<int> ( server.SessionState ( sessionID ) ), static_cast<int> ( CServerSessionState::ST_ACTIVE ) );
        QCOMPARE ( server.SourceCount(), 2 );
        QCOMPARE ( server.VisibleSourceCount(), 2 );
        for ( int index = 0; index < 2; ++index )
        {
            const int sourceID = reservedIDs[static_cast<size_t> ( index )];
            QVERIFY ( server.SourceIsActive ( sourceID ) );
            QCOMPARE ( server.VisibleSourceID ( index ), sourceID );
        }
    }

    void polyInPreparedMapExpiresBackToLegacy()
    {
        CServerTestHarness                 server ( true );
        const int                          sessionID      = server.ReserveLegacySession ( TestAddress ( 41051 ) );
        const int                          legacySourceID = server.LegacySourceID ( sessionID );
        const CVector<CPolyInSourceConfig> config         = MakePolyInConfig ( 2 );
        uint8_t                            rejectReason   = 0;

        server.ConfigurePolyInCapableSession ( sessionID );
        QVERIFY ( server.PreparePolyIn ( sessionID, config, rejectReason ) );
        const int firstReserved  = server.PreparedSourceID ( sessionID, 0 );
        const int secondReserved = server.PreparedSourceID ( sessionID, 1 );
        QVERIFY ( !server.PreparedExpiryIsArmed ( sessionID ) );

        server.AcknowledgePolyInAccept ( sessionID );
        QVERIFY ( server.PreparedExpiryIsArmed ( sessionID ) );
        server.ArmPreparedForExpiry ( sessionID );
        server.OnTimer();

        QCOMPARE ( static_cast<int> ( server.SessionState ( sessionID ) ), static_cast<int> ( CServerSessionState::ST_LEGACY ) );
        QCOMPARE ( server.Generation ( sessionID ), static_cast<uint16_t> ( 0 ) );
        QCOMPARE ( server.SourceCount(), 1 );
        QCOMPARE ( server.VisibleSourceCount(), 1 );
        QCOMPARE ( server.VisibleSourceID ( 0 ), legacySourceID );
        QVERIFY ( server.SourceIsActive ( legacySourceID ) );
        QVERIFY ( !server.SourceIsReserved ( firstReserved ) );
        QVERIFY ( !server.SourceIsReserved ( secondReserved ) );
    }

    void polyInCapacitySeparatesSessionsFromSources()
    {
        CServerTestHarness sessionLimited ( true, 4, 1 );
        const int          firstSession = sessionLimited.ReserveLegacySession ( TestAddress ( 41061 ) );
        QVERIFY ( firstSession != INVALID_CHANNEL_ID );
        sessionLimited.ConfigurePolyInCapableSession ( firstSession );
        const CVector<CPolyInSourceConfig> threeSources = MakePolyInConfig ( 3 );
        uint8_t                            rejectReason = 0;
        QVERIFY ( sessionLimited.PreparePolyIn ( firstSession, threeSources, rejectReason ) );
        const uint16_t generation = sessionLimited.Generation ( firstSession );
        QVERIFY ( sessionLimited.PutPolyInFrame ( firstSession, generation, 150, threeSources ) );
        sessionLimited.ProcessPostedServerEvents();
        QCOMPARE ( sessionLimited.SessionCount(), 1 );
        QCOMPARE ( sessionLimited.SourceCount(), 3 );
        QCOMPARE ( sessionLimited.VisibleSourceCount(), 3 );

        const int secondSession = sessionLimited.ReserveLegacySession ( TestAddress ( 41062 ) );
        QCOMPARE ( secondSession, INVALID_CHANNEL_ID );
        QCOMPARE ( sessionLimited.SessionCount(), 1 );
        QCOMPARE ( sessionLimited.SourceCount(), 3 );

        CServerTestHarness sourceLimited ( true, 2, MAX_NUM_CHANNELS );
        const int          ownerSession = sourceLimited.ReserveLegacySession ( TestAddress ( 41063 ) );
        const int          otherSession = sourceLimited.ReserveLegacySession ( TestAddress ( 41064 ) );
        QVERIFY ( ownerSession != INVALID_CHANNEL_ID );
        QVERIFY ( otherSession != INVALID_CHANNEL_ID );
        QCOMPARE ( sourceLimited.SessionCount(), 2 );
        QCOMPARE ( sourceLimited.SourceCount(), 2 );

        sourceLimited.ConfigurePolyInCapableSession ( ownerSession );
        const CVector<CPolyInSourceConfig> config = MakePolyInConfig ( 2 );
        rejectReason                              = 0;
        QVERIFY ( !sourceLimited.PreparePolyIn ( ownerSession, config, rejectReason ) );
        QCOMPARE ( static_cast<int> ( rejectReason ), static_cast<int> ( PolyInProtocol::kRejectCapacity ) );
        QCOMPARE ( static_cast<int> ( sourceLimited.SessionState ( ownerSession ) ), static_cast<int> ( CServerSessionState::ST_LEGACY ) );
        QCOMPARE ( sourceLimited.SessionCount(), 2 );
        QCOMPARE ( sourceLimited.SourceCount(), 2 );
        QCOMPARE ( sourceLimited.VisibleSourceCount(), 2 );
    }

    void polyInReconnectClearsChildrenAndRejectsOldGeneration()
    {
        CServerTestHarness                 server ( true );
        const CHostAddress                 address      = TestAddress ( 41071 );
        const CVector<CPolyInSourceConfig> config       = MakePolyInConfig ( 2 );
        uint8_t                            rejectReason = 0;

        const int firstSession = server.ReserveLegacySession ( address );
        server.ConfigurePolyInCapableSession ( firstSession );
        QVERIFY ( server.PreparePolyIn ( firstSession, config, rejectReason ) );
        const uint16_t firstGeneration = server.Generation ( firstSession );
        QVERIFY ( server.PutPolyInFrame ( firstSession, firstGeneration, 200, config ) );
        server.ProcessPostedServerEvents();
        QCOMPARE ( static_cast<int> ( server.SessionState ( firstSession ) ), static_cast<int> ( CServerSessionState::ST_ACTIVE ) );
        QCOMPARE ( server.SourceCount(), 2 );

        server.FreeTestSession ( firstSession );
        QCOMPARE ( server.SessionCount(), 0 );
        QCOMPARE ( server.SourceCount(), 0 );
        QCOMPARE ( server.Generation ( firstSession ), static_cast<uint16_t> ( 0 ) );

        const int secondSession = server.ReserveLegacySession ( address );
        QCOMPARE ( secondSession, firstSession );
        server.ConfigurePolyInCapableSession ( secondSession );
        QVERIFY ( server.PreparePolyIn ( secondSession, config, rejectReason ) );
        const uint16_t secondGeneration = server.Generation ( secondSession );
        QVERIFY ( secondGeneration != 0 );
        QVERIFY ( secondGeneration != firstGeneration );

        QVERIFY ( server.PutPolyInFrame ( secondSession, firstGeneration, 201, config ) );
        QVERIFY ( !server.PromotionQueued ( secondSession ) );
        server.ProcessPostedServerEvents();
        QCOMPARE ( static_cast<int> ( server.SessionState ( secondSession ) ), static_cast<int> ( CServerSessionState::ST_PREPARED ) );
        QCOMPARE ( server.VisibleSourceCount(), 1 );

        QVERIFY ( server.PutPolyInFrame ( secondSession, secondGeneration, 202, config ) );
        server.ProcessPostedServerEvents();
        QCOMPARE ( static_cast<int> ( server.SessionState ( secondSession ) ), static_cast<int> ( CServerSessionState::ST_ACTIVE ) );
        QCOMPARE ( server.SourceCount(), 2 );
    }

    void legacyCodecMatrix_data()
    {
        QTest::addColumn<bool> ( "doubleFrames" );
        QTest::addColumn<int> ( "codecValue" );
        QTest::addColumn<int> ( "channels" );
        QTest::addColumn<bool> ( "raw" );

        // Both standard codec cadences are covered in mono and stereo, with
        // their compressed and Raw payload forms.
        for ( const EAudComprType codec : { CT_OPUS, CT_OPUS64 } )
        {
            const bool doubleFrames = codec == CT_OPUS;
            for ( const int channels : { 1, 2 } )
            {
                for ( const bool raw : { false, true } )
                {
                    const QString name = QStringLiteral ( "%1-%2-%3" )
                                             .arg ( codec == CT_OPUS ? QStringLiteral ( "opus128" ) : QStringLiteral ( "opus64" ) )
                                             .arg ( channels == 1 ? QStringLiteral ( "mono" ) : QStringLiteral ( "stereo" ) )
                                             .arg ( raw ? QStringLiteral ( "raw" ) : QStringLiteral ( "compressed" ) );
                    QTest::newRow ( qPrintable ( name ) ) << doubleFrames << static_cast<int> ( codec ) << channels << raw;
                }
            }
        }
    }

    void legacyCodecMatrix()
    {
        QFETCH ( bool, doubleFrames );
        QFETCH ( int, codecValue );
        QFETCH ( int, channels );
        QFETCH ( bool, raw );
        const EAudComprType codec = static_cast<EAudComprType> ( codecValue );

        CServerTestHarness server ( doubleFrames );
        const int          sessionID     = server.ReserveLegacySession ( TestAddress ( 41100 + codecValue * 20 + channels * 2 + ( raw ? 1 : 0 ) ) );
        const int          sourceID      = server.LegacySourceID ( sessionID );
        const int          frameSamples  = codec == CT_OPUS ? DOUBLE_SYSTEM_FRAME_SIZE_SAMPLES : SYSTEM_FRAME_SIZE_SAMPLES;
        const int          networkFactor = doubleFrames && codec == CT_OPUS64 ? 2 : 1;
        const int          serverTicksPerPacket = !doubleFrames && codec == CT_OPUS ? 2 : 1;
        const int          codedBytes           = CodedBytes ( codec, channels, raw );
        server.ConfigureLegacySession ( sessionID, codec, channels, codedBytes, networkFactor );

        std::vector<int16_t> expected;
        std::vector<int16_t> decoded;
        int                  firstFrame = 0;

        // Warm the stateful codec before checking the final packet. Raw rows
        // remain exact and compressed rows are checked for both-channel output.
        for ( int iteration = 0; iteration < 4; ++iteration )
        {
            const int inputFrames = networkFactor * frameSamples;
            expected              = MakePCM ( firstFrame, inputFrames, channels );
            firstFrame += inputFrames;

            CVector<uint8_t> packet ( networkFactor * codedBytes );
            for ( int frame = 0; frame < networkFactor; ++frame )
            {
                const int16_t* framePCM = expected.data() + static_cast<size_t> ( frame * frameSamples * channels );
                uint8_t*       frameOut = &packet[frame * codedBytes];
                if ( raw )
                {
                    std::memcpy ( frameOut, framePCM, static_cast<size_t> ( codedBytes ) );
                }
                else
                {
                    QCOMPARE ( server.EncodeLegacyFrame ( sessionID, codec, channels, framePCM, codedBytes, frameOut ), codedBytes );
                }
            }

            const EPutDataStat status = server.PutLegacyPacket ( sessionID, packet );
            QVERIFY ( status == PS_NEW_CONNECTION || status == PS_AUDIO_OK );

            decoded.clear();
            for ( int tick = 0; tick < serverTicksPerPacket; ++tick )
            {
                const std::vector<int16_t> block = server.DecodeLegacyBlock ( sourceID, sessionID );
                decoded.insert ( decoded.end(), block.begin(), block.end() );
            }
        }

        QCOMPARE ( server.DecodedChannels(), channels );
        QCOMPARE ( server.DecodedCodec(), codecValue );
        QCOMPARE ( static_cast<int> ( decoded.size() ), static_cast<int> ( expected.size() ) );

        if ( raw )
        {
            QVERIFY ( decoded == expected );
        }
        else
        {
            QVERIFY2 ( ChannelEnergy ( decoded, channels, 0 ) > 1000, "decoded left/mono channel is silent" );
            if ( channels == 2 )
                QVERIFY2 ( ChannelEnergy ( decoded, channels, 1 ) > 1000, "decoded right channel is silent" );
        }
    }
};

QTEST_GUILESS_MAIN ( ServerLegacyTest )
#include "server_legacy_test.moc"
