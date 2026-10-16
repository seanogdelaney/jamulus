/******************************************************************************\
 * Optional producer-only negotiation body for stock reliable CProtocol.
 * Copyright (c) 2026. AGPL-3.0-or-later.
\******************************************************************************/
#pragma once
#include "polyinwire.h"
#include <cstdint>
#include <string>
#include <vector>

namespace PolyIn {
struct ControlSource {
    uint16_t source = 0;
    uint8_t codec = 0, channels = 0;
    uint16_t bytes = 0;
    std::string name;
};
// Version + 64-bit epoch + count + bounded descriptors. No receiver mixer data.
inline bool DecodeConfig(const uint8_t* p, size_t n, uint64_t& epoch, std::vector<ControlSource>& out) {
    out.clear(); epoch=0;
    if (!p || n < 10 || n > 10 + MaxSources * 70 || p[0] != 1 || !(epoch=Detail::Read64(p+1))) return false;
    const size_t count=p[9];
    if (count==0) return n==10; // explicit acknowledged transition back to stock
    if (count<2 || count>MaxSources) return false;
    bool seen[MaxSources] = {};
    size_t at=10;
    for(size_t i=0; i<count; ++i) {
        if (n-at<6) return false;
        ControlSource s;
        s.source=p[at++]; s.codec=p[at++]; s.channels=p[at++];
        s.bytes=uint16_t(p[at] | (unsigned(p[at+1])<<8)); at+=2;
        size_t len=p[at++];
        if(s.source>=MaxSources || seen[s.source] || (s.codec!=2 && s.codec!=3) ||
           (s.channels!=1 && s.channels!=2) || !s.bytes || s.bytes>MaxEncodedBytes ||
           !len || len>64 || len>n-at) return false;
        seen[s.source]=true;
        s.name.assign(reinterpret_cast<const char*>(p+at),len); at+=len;
        out.push_back(std::move(s));
    }
    return at==n && seen[0];
}
inline bool EncodeConfig(uint64_t epoch, const std::vector<ControlSource>& src, std::vector<uint8_t>& out) {
    out.clear();
    if(!epoch || (src.size()!=0 && src.size()<2) || src.size()>MaxSources) return false;
    out.resize(10); out[0]=1; Detail::Write64(out.data()+1,epoch); out[9]=uint8_t(src.size());
    bool seen[MaxSources]={};
    for(const auto& s:src) {
        if(s.source>=MaxSources || seen[s.source] || (s.codec!=2 && s.codec!=3) ||
           (s.channels!=1 && s.channels!=2) || !s.bytes || s.bytes>MaxEncodedBytes ||
           s.name.empty() || s.name.size()>64) {out.clear();return false;}
        seen[s.source]=true;
        out.push_back(uint8_t(s.source));out.push_back(s.codec);out.push_back(s.channels);
        out.push_back(uint8_t(s.bytes));out.push_back(uint8_t(s.bytes>>8));
        out.push_back(uint8_t(s.name.size()));
        out.insert(out.end(),s.name.begin(),s.name.end());
    }
    if(!src.empty() && !seen[0]) {out.clear();return false;}
    return true;
}
inline std::vector<uint8_t> Response(uint64_t epoch, bool accepted) {
    std::vector<uint8_t> out(10);out[0]=1;
    Detail::Write64(out.data()+1,epoch);out[9]=accepted?1:0;return out;
}
inline bool DecodeResponse(const uint8_t* p,size_t n,uint64_t& epoch,bool& accepted) {
    if(!p || n!=10 || p[0]!=1 || p[9]>1) return false;
    epoch=Detail::Read64(p+1);accepted=p[9]==1;return epoch!=0;
}
}
