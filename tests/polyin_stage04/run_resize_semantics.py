#!/usr/bin/env python3
"""Production CChannel::SetSockBufNumFrames + F02/stock CNetBuf parity.
Runs old F05 branch as a negative control with --pre-fix.
Qt mutex/control are inert; no live Qt server claim.
"""
from pathlib import Path
import re,subprocess,tempfile,sys,argparse
parser=argparse.ArgumentParser();parser.add_argument('--pre-fix',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parents[2];src=root/'src';here=Path(__file__).resolve().parent
source=(src/'channel.cpp').read_text()
if args.pre_fix:
 old='''if ( PolyFrameBuffer )
                {
                    // The borrowed stock window must honour the same reset vs.
                    // preserve flag as the ordinary SockBuf.Init call below.
                    if ( bPreserve ) PolyFrameBuffer->Resize ( iNewNumFrames );
                    else PolyFrameBuffer->Init ( PolyFrameBuffer->Generation(), iNewNumFrames );
                }'''
 assert source.count(old)==1
 source=source.replace(old,'if ( PolyFrameBuffer ) PolyFrameBuffer->Resize ( iNewNumFrames );')
def fn(src,name,result):
 m=re.search(rf'{result}\s+CChannel::{name}\s*\([^)]*\)\s*\{{',src);assert m,name
 pos=src.index('{',m.start());depth=0
 for i in range(pos,len(src)):
  if src[i]=='{':depth+=1
  elif src[i]=='}':
   depth-=1
   if depth==0:return src[m.start():i+1]
 raise ValueError(name)
script=(here/'run_channel_get.py').read_text()
original=(here/'channel_get_fixture.cpp').read_text()
original=original.replace('int iAudioFrameSizeSamples=64, disconnectedCount=0;', '''int iAudioFrameSizeSamples=64, disconnectedCount=0;
    int iCurSockBufNumFrames=6, iCeltNumCodedBytes=8;
    bool bDoAutoSockBufSize=false, bIsServer=false, bUseSequenceNumber=true;
    void ServerAutoSockBufSizeChange(int){};
    bool SetSockBufNumFrames(const int,const bool);''')
original=original.replace('// STOCK_CHANNEL_GET','// STOCK_CHANNEL_GET\n'+fn(source,'SetSockBufNumFrames','bool'))
original=original.replace('int main() {','''static void preserve_probe() {
  CChannel poly, stock;
  poly.SockBuf.SetUseDoubleSystemFrameSize(false);
  poly.PolyFrameBuffer=std::make_unique<PolyIn::StockFrameBuffer>(poly.SockBuf);
  poly.PolyFrameBuffer->Init(4343,6);
  CNetBufWithStats ordinary;
  ordinary.SetUseDoubleSystemFrameSize(false);
  ordinary.Init(PolyIn::TokenBytes,6,true);
  for(int f=0;f<16;f++) {
    for(const auto& packet:make(f,4343,false)) CHECK(poly.PolyFrameBuffer->Receive(packet.data(),packet.size()));
    CVector<uint8_t> token(PolyIn::TokenBytes+1);
    PolyIn::Detail::Write32(token.data(),f);
    PolyIn::Detail::Write64(token.data()+4,4343);
    token[PolyIn::TokenBytes]=uint8_t(f);
    ordinary.Put(token,PolyIn::TokenBytes+1);
  }
  CHECK(!poly.SetSockBufNumFrames(7,false));
  ordinary.Init(PolyIn::TokenBytes,7,true,false);
  PolyIn::FrameView frame;
  CVector<uint8_t> out(PolyIn::TokenBytes);
  bool polyGot=poly.PolyFrameBuffer->Get(frame), stockGot=ordinary.Get(out,PolyIn::TokenBytes);
  std::cout << "RESET_FALSE poly_kept="<<polyGot<<" stock_kept="<<stockGot<<"\\n";
  CHECK(polyGot==stockGot); CHECK(!polyGot);
  for(int f=20;f<36;f++) {
    for(const auto& packet:make(f,4343,false)) CHECK(poly.PolyFrameBuffer->Receive(packet.data(),packet.size()));
    CVector<uint8_t> token(PolyIn::TokenBytes+1);
    PolyIn::Detail::Write32(token.data(),f);PolyIn::Detail::Write64(token.data()+4,4343);
    token[PolyIn::TokenBytes]=uint8_t(f);ordinary.Put(token,PolyIn::TokenBytes+1);
  }
  CHECK(!poly.SetSockBufNumFrames(8,true));
  ordinary.Init(PolyIn::TokenBytes,8,true,true);
  polyGot=poly.PolyFrameBuffer->Get(frame);stockGot=ordinary.Get(out,PolyIn::TokenBytes);
  std::cout << "RESET_TRUE poly_kept="<<polyGot<<" stock_kept="<<stockGot<<"\\n";
  CHECK(polyGot==stockGot);CHECK(polyGot);
}
int main() {preserve_probe();''')
with tempfile.TemporaryDirectory(prefix='stage04-resize-parity-') as tmp:
 td=Path(tmp);(td/'fixture.cpp').write_text(original)
 script=script.replace("template=(here/'channel_get_fixture.cpp').read_text()",'template=Path('+repr(str(td/'fixture.cpp'))+').read_text()')
 script=script.replace("root=Path(__file__).resolve().parents[2];src=root/'src';here=Path(__file__).resolve().parent",'root=Path('+repr(str(root))+');src=root/\'src\';here=Path('+repr(str(here))+')')
 (td/'run.py').write_text(script)
 proc=subprocess.run([sys.executable,str(td/'run.py')],capture_output=True,text=True)
 print(proc.stdout,end='');print(proc.stderr,end='',file=sys.stderr)
 sys.exit(proc.returncode)
