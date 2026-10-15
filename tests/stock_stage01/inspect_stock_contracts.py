#!/usr/bin/env python3
"""Source assertions ONLY. Does not count as live PCM/Qt L02-L05 evidence."""
from pathlib import Path
root=Path(__file__).resolve().parents[2] / 'src'
checks={
    'L02 decode/PLC and 64/128 [source only]': [('server.cpp','void CServer::DecodeReceiveData'),('server.cpp','opus_custom_decode'),('server.cpp','pCurCodedData = nullptr'),('server.cpp','DoubleFrameSizeConvBufIn[iCurChanID].Get'),('server.cpp','memset ( &vecvecsData')],
    'L03 original mixer, both delaypan branches and history [source only]':[('server.cpp','void CServer::MixEncodeTransmitData'),('server.cpp','if ( bDelayPan )'),('server.cpp','if ( isMono )'),('server.cpp','const float fPan  = bDelayPan ? 0.5f'),('server.cpp','vecvecsData2[i][j] = vecvecsData[i][j]'),('server.cpp','Float2Short')],
    'L04 existing return encode/packet [source only]':[('server.cpp','opus_custom_encoder_ctl'),('server.cpp','opus_custom_encode'),('server.cpp','DoubleFrameSizeConvBufOut[iCurChanID].Put'),('server.cpp','PrepAndSendPacket')],
    'L05 timeout/fade/recorder/levels [source only]':[('channel.cpp','iConTimeOut -= iAudioFrameSizeSamples'),('channel.cpp','emit Disconnected()'),('channel.cpp','ResetTimeOutCounter()'),('server.cpp','GetFadeInGain()'),('server.cpp','emit AudioFrame'),('server.cpp','CreateLevelsForAllConChannels')],
    'legacy sorted IDs/control/reordered levels [source only]':[('server.cpp','CVector<CChannelInfo> CServer::CreateChannelList'),('protocol.cpp','PROTMESSID_CONN_CLIENTS_LIST'),('client.cpp','this relies on the received client list being in order'),('client.cpp','bool CClient::ReorderLevelList'),('protocol.cpp','PutValOnStream ( vecData, iPos, static_cast<uint32_t> ( vecChanInfo[i].iChanID ), 1 )'),('global.h','#define MAX_NUM_CHANNELS 150')]
}
for label,items in checks.items():
    for filename,needle in items:
        assert needle in (root/filename).read_text(),f'{label}: missing {filename}:{needle}'
    print('STATIC_PASS:',label)
print('STATIC_ONLY: stock anchors L02-L05, legacy protocol. L02-L04 have a separate compiled actual-method fixture; L05/legacy full Qt integration deferred to owning stages.')
