#!/usr/bin/env python3
"""Static source/transport ownership gate. NOT a Qt lifecycle/UI test."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
server=(root/'src/server.cpp').read_text()
client=(root/'src/client.cpp').read_text()
channel=(root/'src/channel.cpp').read_text()
protocol=(root/'src/protocol.cpp').read_text()
main=(root/'src/main.cpp').read_text()
checks={
    'server dispatch queued under stock protocol mutex':
        re.search(r'&CChannel::PolyInConfigReceived,\s*this,\s*pOnPolyInConfigReceivedCh,\s*Qt::QueuedConnection', server) is not None,
    'stock protocol mutex still locks input':
        'void CServer::OnProtocolMessageReceived' in server and 'vecChannels[iCurChanID].PutProtocolData' in server,
    'server dispatch locks for config': 'void CServer::HandlePolyInConfig' in server and 'QMutexLocker audio ( &Mutex );' in server,
    'response outside audio mutex': 'vecChannels[physical].CreatePolyInResponse ( payload );' in server,
    'stock transport sent before producer control retry':
        'Protocol.CreateNetwTranspPropsMes ( GetNetworkTransportPropsFromCurrentSettings() );\n    emit NetwTranspPropsSent();' in channel and
        '&CChannel::NetwTranspPropsSent, this, &CClient::SendPolyInConfig' in client,
    'single stock return send path': 'CServer::MixEncodeTransmitData' in server,
    'legacy audio send remains explicit fallback': 'Channel.PrepAndSendPacket ( &Socket, vecCeltData, iCeltNumCodedBytes );' in client,
    'producer optional CLI only': 'Client.SetPolyInRequested ( bPolyInRequested );' in main,
    'stock connected clients list reused': 'vecChannels[i].CreateConClientListMes ( vecChanInfo );' in server,
    'stock fader source IDs': 'CChannelInfo ( i, info )' in server,
    'stock protocol reliable send': 'CreateAndSendMessage ( PROTMESSID_POLYIN_CONFIG, body );' in protocol,
    'no alternative receiver mixer': not any(x in (server+client+main).lower() for x in
        ('--polyinmixer', 'receiver subscription', 'source subscription', 'receiver roster')),
}
for desc,result in checks.items():
    print(('PASS' if result else 'FAIL')+' SOURCE_AUDIT '+desc)
assert all(checks.values()),'source ownership audit failed'
print('SOURCE AUDIT ONLY; Qt runtime, Opus packet receive, and legacy receiver UI NOT QUALIFIED')
