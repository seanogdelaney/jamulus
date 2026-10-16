/******************************************************************************\
 * Versioned, bounded optional poly-in datagram records (producer and consumer).
 * Copyright (c) 2026. AGPL-3.0-or-later.
\******************************************************************************/
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PolyIn
{
// Hard transport limits. The physical stock buffer is not defined here.
constexpr size_t  MaxDatagram         = 1200;
constexpr size_t  MaxSources          = 20;
constexpr size_t  MaxEncodedBytes     = 768;
constexpr size_t  RetainedFrames      = 64;
constexpr size_t  HeaderBytes         = 24;
constexpr size_t  FragmentHeaderBytes = 10;
constexpr uint8_t WireVersion         = 1;
constexpr size_t  TokenBytes          = 12;
struct SourceRecord
{
    uint16_t source = 0;
    // On-wire numeric namespace is stock EAudComprType: 1 CELT, 2 OPUS, 3 OPUS64; 0 invalid.
    uint8_t              codec    = 0;
    uint8_t              channels = 1;
    std::vector<uint8_t> encoded;
};
bool PackFrame ( uint32_t                           frame,
                 uint64_t                           generation,
                 const std::vector<SourceRecord>&   records,
                 size_t                             mtu,
                 std::vector<std::vector<uint8_t>>& datagrams );
struct FragmentView
{
    uint16_t       source = 0, total = 0, offset = 0, size = 0;
    uint8_t        codec = 0, channels = 0;
    const uint8_t* data = nullptr;
};
struct ParsedDatagram
{
    uint32_t                             frame      = 0;
    uint64_t                             generation = 0;
    size_t                               count      = 0;
    std::array<FragmentView, MaxSources> fragments{};
};
bool ParseDatagram ( const uint8_t* bytes, size_t size, ParsedDatagram& result );
namespace Detail
{
inline uint32_t Read32 ( const uint8_t* p )
{
    return uint32_t ( p[0] ) | ( uint32_t ( p[1] ) << 8 ) | ( uint32_t ( p[2] ) << 16 ) | ( uint32_t ( p[3] ) << 24 );
}
inline uint64_t Read64 ( const uint8_t* p ) { return uint64_t ( Read32 ( p ) ) | ( uint64_t ( Read32 ( p + 4 ) ) << 32 ); }
inline void     Write32 ( uint8_t* p, uint32_t v )
{
    for ( int i = 0; i < 4; ++i )
        p[i] = uint8_t ( v >> ( i * 8 ) );
}
inline void Write64 ( uint8_t* p, uint64_t v )
{
    Write32 ( p, uint32_t ( v ) );
    Write32 ( p + 4, uint32_t ( v >> 32 ) );
}
} // namespace Detail
} // namespace PolyIn
