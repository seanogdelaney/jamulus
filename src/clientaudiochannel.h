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

#pragma once

#ifdef USE_OPUS_SHARED_LIB
#    include "opus/opus_custom.h"
#else
#    include "opus_custom.h"
#endif
#include "channel.h"
#include "socket.h"
#include "util.h"

/**
 * One independently advertised client audio stream.
 *
 * A Jamulus client normally needs one bidirectional stream. Advanced audio
 * routing additionally creates transmit-only streams, each with a distinct
 * UDP source port and CChannel state. This class owns the shared mechanics for
 * both cases: codec setup, transport properties, protocol replies, jitter
 * buffer setup, and packet transmission. CClient retains the receive/mix path
 * for the first (primary) stream.
 */
class CClientAudioChannel
{
public:
    CClientAudioChannel ( quint16 iPortNumber, quint16 iQosNumber, bool bDisableIPv6 );
    ~CClientAudioChannel();

    CClientAudioChannel ( const CClientAudioChannel& )            = delete;
    CClientAudioChannel& operator= ( const CClientAudioChannel& ) = delete;

    CChannel&        GetChannel() { return Channel; }
    CProtocol&       GetConnLessProtocol() { return ConnLessProtocol; }
    CHighPrioSocket& GetSocket() { return Socket; }
    bool             IsIPv6Available() const { return bIPv6Available; }
    bool&            IPv6AvailabilityReference() { return bIPv6Available; }

    void StartSocket();
    void SetEnabled ( bool bEnable );
    bool IsEnabled() { return Channel.IsEnabled(); }
    void Disconnect();

    void SetChannelInfo ( const CChannelCoreInfo& NewChannelInfo );

    void Configure ( EAudComprType  eNewAudioCompressionType,
                     int            iNewCodedBytes,
                     int            iNewFrameSizeSamples,
                     int            iNewFrameSizeFactor,
                     int            iNewNumAudioChannels,
                     OpusCustomMode* pNewOpusMode,
                     bool           bUseRawAudio,
                     int            iNewSockBufNumFrames,
                     int            iNewServerSockBufNumFrames,
                     bool           bNewDoAutoSockBufSize );

    void SetSocketBufferSettings ( int iNewSockBufNumFrames, int iNewServerSockBufNumFrames, bool bNewDoAutoSockBufSize );
    void CreateServerJitterBufferMessage();
    void Process ( const CVector<int16_t>& vecAudio, bool bMute );
    void DiscardReceivedAudio();

    int GetUploadRateKbps() { return Channel.GetUploadRateKbps(); }
    CVector<int16_t>& GetAudioData() { return vecAudioData; }

private:
    void CreateEncoder();
    void DestroyEncoder();
    void OnNewConnection();
    void OnReqChanInfo();

    CChannel  Channel;
    bool      bIPv6Available;
    CHighPrioSocket Socket;
    CProtocol ConnLessProtocol;

    CChannelCoreInfo ChannelInfo;
    OpusCustomMode*  pOpusMode;
    OpusCustomEncoder* pOpusEncoder;
    EAudComprType     eAudioCompressionType;
    int               iCodedBytes;
    int               iFrameSizeSamples;
    int               iFrameSizeFactor;
    int               iNumAudioChannels;
    int               iSockBufNumFrames;
    int               iServerSockBufNumFrames;
    bool              bDoAutoSockBufSize;
    bool              bUseRawAudio;
    bool              bSocketStarted;

    CVector<uint8_t> vecCodedData;
    CVector<uint8_t> vecDiscardedReceiveData;
    CVector<int16_t> vecZeros;
    CVector<int16_t> vecAudioData;

};
