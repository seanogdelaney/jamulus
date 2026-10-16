/******************************************************************************\
 * Poly-in packet packing and structural decoding. AGPL-3.0-or-later.
\******************************************************************************/
#include "polyinwire.h"
#include <algorithm>
#include <bitset>
namespace PolyIn
{
using Detail::Read32;
using Detail::Read64;
namespace
{
using Detail::Write32;
using Detail::Write64;

uint16_t Read16 ( const uint8_t* p ) { return uint16_t ( p[0] | ( uint16_t ( p[1] ) << 8 ) ); }
void     Write16 ( uint8_t* p, uint16_t v )
{
    p[0] = uint8_t ( v );
    p[1] = uint8_t ( v >> 8 );
}
// Wire IDs ARE stock EAudComprType values (src/util.h): CELT=1, OPUS=2,
// OPUS64=3. CT_NONE=0 is a sentinel, not a decodable audio record.
bool ValidCodec ( uint8_t codec, uint8_t channels ) { return codec >= 1 && codec <= 3 && ( channels == 1 || channels == 2 ); }
void StartPacket ( std::vector<uint8_t>& p, uint32_t frame, uint64_t generation )
{
    p.assign ( HeaderBytes, 0 );
    p[0] = 'P';
    p[1] = 'L';
    p[2] = 'I';
    p[3] = 'N';
    p[4] = WireVersion;
    Write32 ( p.data() + 8, frame );
    Write64 ( p.data() + 12, generation );
}
void FinishPacket ( std::vector<uint8_t>& p ) { Write16 ( p.data() + 20, uint16_t ( p.size() - HeaderBytes ) ); }
} // namespace

bool PackFrame ( uint32_t                           frame,
                 uint64_t                           generation,
                 const std::vector<SourceRecord>&   records,
                 size_t                             mtu,
                 std::vector<std::vector<uint8_t>>& datagrams )
{
    datagrams.clear();
    if ( generation == 0 || records.empty() || records.size() > MaxSources || mtu > MaxDatagram || mtu < HeaderBytes + FragmentHeaderBytes + 1 )
        return false;
    std::bitset<MaxSources> seen;
    for ( const auto& rec : records )
    {
        if ( rec.source >= MaxSources || seen.test ( rec.source ) || !ValidCodec ( rec.codec, rec.channels ) || rec.encoded.empty() ||
             rec.encoded.size() > MaxEncodedBytes )
            return false;
        seen.set ( rec.source );
    }
    std::vector<uint8_t> packet;
    StartPacket ( packet, frame, generation );
    for ( const auto& rec : records )
    {
        size_t offset = 0;
        while ( offset < rec.encoded.size() )
        {
            if ( packet.size() + FragmentHeaderBytes + 1 > mtu || packet[6] == MaxSources )
            {
                FinishPacket ( packet );
                datagrams.push_back ( std::move ( packet ) );
                StartPacket ( packet, frame, generation );
            }
            const size_t n     = std::min ( rec.encoded.size() - offset, mtu - packet.size() - FragmentHeaderBytes );
            const size_t start = packet.size();
            packet.resize ( start + FragmentHeaderBytes + n );
            Write16 ( packet.data() + start, rec.source );
            packet[start + 2] = rec.codec;
            packet[start + 3] = rec.channels;
            Write16 ( packet.data() + start + 4, uint16_t ( rec.encoded.size() ) );
            Write16 ( packet.data() + start + 6, uint16_t ( offset ) );
            Write16 ( packet.data() + start + 8, uint16_t ( n ) );
            std::copy ( rec.encoded.begin() + offset, rec.encoded.begin() + offset + n, packet.begin() + start + FragmentHeaderBytes );
            ++packet[6];
            offset += n;
        }
    }
    FinishPacket ( packet );
    datagrams.push_back ( std::move ( packet ) );
    return true;
}

bool ParseDatagram ( const uint8_t* packet, size_t bytes, ParsedDatagram& parsed )
{
    parsed = {};
    if ( !packet || bytes < HeaderBytes + FragmentHeaderBytes + 1 || bytes > MaxDatagram || packet[0] != 'P' || packet[1] != 'L' ||
         packet[2] != 'I' || packet[3] != 'N' || packet[4] != WireVersion || packet[5] || packet[6] == 0 || packet[6] > MaxSources || packet[7] ||
         Read64 ( packet + 12 ) == 0 || Read16 ( packet + 20 ) != bytes - HeaderBytes || Read16 ( packet + 22 ) )
        return false;
    size_t cursor = HeaderBytes;
    for ( size_t i = 0; i < packet[6]; ++i )
    {
        if ( bytes - cursor < FragmentHeaderBytes )
            return false;
        const uint8_t* p = packet + cursor;
        FragmentView   frag;
        frag.source   = Read16 ( p );
        frag.total    = Read16 ( p + 4 );
        frag.offset   = Read16 ( p + 6 );
        frag.size     = Read16 ( p + 8 );
        frag.codec    = p[2];
        frag.channels = p[3];
        frag.data     = p + FragmentHeaderBytes;
        cursor += FragmentHeaderBytes;
        if ( frag.source >= MaxSources || !ValidCodec ( frag.codec, frag.channels ) || frag.total == 0 || frag.total > MaxEncodedBytes ||
             !frag.size || frag.offset >= frag.total || frag.size > frag.total - frag.offset || frag.size > bytes - cursor )
            return false;
        parsed.fragments[i] = frag;
        cursor += frag.size;
    }
    if ( cursor != bytes )
        return false;
    parsed.frame      = Read32 ( packet + 8 );
    parsed.generation = Read64 ( packet + 12 );
    parsed.count      = packet[6];
    return true;
}
} // namespace PolyIn
