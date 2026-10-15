#!/usr/bin/env python3
"""Production CChannel::EnablePolyInVirtual extracted verbatim. The meter is
an instrumented boundary; this is not a Qt/real SignalLevelMeter integration."""
from pathlib import Path
import re,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
s=(root/'src/channel.cpp').read_text()
m=re.search(r'void CChannel::EnablePolyInVirtual\s*\([^)]*\)\s*\{',s)
assert m
brace=s.index('{',m.start());depth=0
for end in range(brace,len(s)):
 if s[end]=='{':depth+=1
 elif s[end]=='}':
  depth-=1
  if depth==0:break
fn=s[m.start():end+1]
program=r'''#include <cstdint>
#include <string>
#include <iostream>
#include <stdexcept>
using EAudComprType=int;
struct QString:std::string{using std::string::string;};
struct Meter{int resets=0;void Reset(){++resets;}};
class CChannel{
public:
 Meter SignalLevelMeter;CChannel* pPolyInOwner=nullptr;
 uint16_t iPolyInSource=0;QString PolyInTag;EAudComprType ePolyInCodec=0;
 int iPolyInChannels=0,iPolyInEncodedBytes=0,iPolyInFrameCursor=999;
 void EnablePolyInVirtual(CChannel*,uint16_t,const QString&,EAudComprType,int,int,bool);
};
// PROD
int main(){CChannel owner,virtualSource;
 virtualSource.EnablePolyInVirtual(&owner,1,QString("mic"),2,1,64,true);
 if(virtualSource.SignalLevelMeter.resets!=1 || virtualSource.pPolyInOwner!=&owner ||
 virtualSource.iPolyInSource!=1 || virtualSource.iPolyInEncodedBytes!=64 ||virtualSource.iPolyInFrameCursor!=0)return 1;
 virtualSource.EnablePolyInVirtual(&owner,1,QString("mic"),2,1,64,false);
 if(virtualSource.SignalLevelMeter.resets!=1)return 2;
 virtualSource.EnablePolyInVirtual(&owner,2,QString("guitar"),2,2,128,true);
 if(virtualSource.SignalLevelMeter.resets!=2 || virtualSource.iPolyInSource!=2 ||
 virtualSource.iPolyInChannels!=2 || virtualSource.iPolyInEncodedBytes!=128)return 3;
 std::cout<<"PASS production CChannel::EnablePolyInVirtual: first binding, idempotence, new source meter reset and codec metadata\n";
}
'''.replace('// PROD',fn)
with tempfile.TemporaryDirectory() as d:
 src=Path(d)/'meter.cpp';exe=Path(d)/'meter';src.write_text(program)
 cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)]
 print('BUILD '+' '.join(cmd),flush=True);subprocess.run(cmd,check=True)
 r=subprocess.run([str(exe)],capture_output=True,text=True);print(r.stdout,end='');print(r.stderr,file=sys.stderr);sys.exit(r.returncode)
