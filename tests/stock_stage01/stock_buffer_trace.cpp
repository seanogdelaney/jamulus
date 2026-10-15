// Exercises the exact pinned production src/buffer.cpp: ordinary frame and
// token (same real production buffer) on identical event/tick schedules.
#include "buffer.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <string>
#include <array>

struct Pair {
    CNetBufWithStats legacy, token;
    CVector<uint8_t> frame{5}, tok{5}, a{4}, b{4};
    int put_count=0,get_count=0;
    int baseline_miss=0, token_miss=0, differences=0;
    explicit Pair(int n) { legacy.Init(4,n,true); token.Init(4,n,true); }
    void resize(int n) { legacy.Init(4,n,true,true); token.Init(4,n,true,true); }
    void put(int seq) {
        // Identical 4-byte payloads; token = identity not encoded audio.
        for(int i=0;i<4;i++) frame[i]=tok[i]=(uint8_t)((seq>>(i*8))&255);
        frame[4]=tok[4]=(uint8_t)seq;
        bool x=legacy.Put(frame,5), y=token.Put(tok,5);
        if(x!=y) differences++;
        put_count++;
    }
    void get(int tick,int scenario) {
        bool x=legacy.Get(a,4),y=token.Get(b,4);
        if(!x) baseline_miss++;
        if(!y) token_miss++;
        if(x!=y || (x && a!=b)) differences++;
        CVector<double> ex,ey; double lo,hi,lo2,hi2;
        legacy.GetErrorRates(ex,lo,hi);
        token.GetErrorRates(ey,lo2,hi2);
        if(ex.size()!=10 || ex!=ey || lo!=lo2 || hi!=hi2 || legacy.GetAutoSetting()!=token.GetAutoSetting()) differences++;
        int id=x ? ((int)a[0]|((int)a[1]<<8)|((int)a[2]<<16)|((int)a[3]<<24)) : -1;
        printf("%d,%d,%d,%d,%d,%d,%d,%d",scenario,tick,x,y,id,legacy.GetAutoSetting(),baseline_miss,differences);
        for(double e:ex) printf(",%.9f",e);
        printf("\n");
        get_count++;
    }
};
int main(){
    // Scenario 1: reorder, duplicate, backwards replay and preserve resizing 2->6->2.
    {
        Pair p(2);
        for(int t=0;t<550;t++) {
            if(t==70)p.resize(6);
            if(t==430)p.resize(2);
            if(t%27==0 && t>5)p.put(t-4); // late/backshift
            if(t%13!=0)p.put(t);           // loss
            if(t%17==0)p.put(t+1);         // early/out-of-order
            p.get(t,1);
        }
        fprintf(stderr,"L01 reorder: gets=%d, baseline_miss=%d, diffs=%d\n",p.get_count,p.baseline_miss,p.differences);
        if(p.differences) return 2;
    }
    // Scenario 2: connected wild jitter: bounded burst arrivals, jitter, blackout.
    {
        Pair p(9);
        int seq=0;
        for(int t=0;t<1200;t++) {
            if(t%80>=8 && t%80<15) { /* blackout */ }
            else if(t%19==0) { for(int j=0;j<6;j++)p.put(seq++); }
            else if(t%7!=0) p.put(seq++);
            if(t%31==0 && seq>8)p.put(seq-8);
            p.get(t,2);
        }
        fprintf(stderr,"L01 wild-jitter continuous: gets=%d, baseline_miss=%d, diffs=%d\n",p.get_count,p.baseline_miss,p.differences);
        if(p.differences) return 3;
    }
    // OF-01 comparator scaffolding: target=2, delayed packet/repeat and
    // residual drift. NO invented backlog metric: read identity is logged.
    {
        Pair p(2);
        for(int t=0;t<1500;t++) {
            if(t%103==0 && t>0) { for(int j=0;j<9;j++)p.put(t+j); }
            else if(t%23!=0) p.put(t);
            if(t%89==0 && t>0)p.put(t-9);
            p.get(t,3);
        }
        fprintf(stderr,"OF-01 stock comparator: gets=%d baseline_miss=%d diffs=%d (no feature adapter)\n",p.get_count,p.baseline_miss,p.differences);
        if(p.differences) return 4;
    }
    // Alias and wrap: preserve stock's inherited unmodified 8-bit semantics.
    {
        Pair p(6);
        for(int t=0;t<610;t++) {
            if(t==130)p.put(t+256);
            if(t%3!=1)p.put(t);
            if(t%40==0 && t>4)p.put(t-4);
            p.get(t,4);
        }
        if(p.differences) return 5;
    }
    fprintf(stderr,"PASS real production Put/Get, ten candidates, resize, replay, wild jitter, OF-01 comparison infrastructure; feature path NOT tested\n");
}
