/******************************************************************************\
 * Source-frame sidecars on the existing real stock CNetBufWithStats.
 * Copyright (c) 2026. AGPL-3.0-or-later.
\******************************************************************************/
#include "polyinframe.h"
#include <algorithm>
namespace PolyIn
{
void StockFrameBuffer::Init ( uint64_t sessionGeneration, int stockWindowBlocks )
{
    // Generation is the session owner's nonce/identity (Stage 3/5). Only a
    // real session replacement calls Init; network loss never does.
    generation_ = sessionGeneration;
    counters_   = {};
    slots_      = {};
    pending_    = {};
    input_.Init ( TokenBytes + 1 );
    output_.Init ( TokenBytes );
    // Match CChannel::SetSockBufNumFrames (global.h 1..20); 64 is sidecar capacity only.
    // The owning CChannel sets stock_.SetUseDoubleSystemFrameSize before Init.
    initialized_ = sessionGeneration != 0 && stockWindowBlocks >= MIN_NET_BUF_SIZE_NUM_BL && stockWindowBlocks <= MAX_NET_BUF_SIZE_NUM_BL;
    if ( initialized_ )
        stock_.Init ( TokenBytes, stockWindowBlocks, true );
}

void StockFrameBuffer::Resize ( int stockWindowBlocks )
{
    if ( !initialized_ || stockWindowBlocks < MIN_NET_BUF_SIZE_NUM_BL || stockWindowBlocks > MAX_NET_BUF_SIZE_NUM_BL )
        return;
    stock_.Init ( TokenBytes, stockWindowBlocks, true, true ); // stock preserve/backshift semantics
}

bool StockFrameBuffer::Receive ( const uint8_t* packet, size_t bytes )
{
    ++counters_.datagrams;
    ParsedDatagram parsed;
    if ( !initialized_ || !ParseDatagram ( packet, bytes, parsed ) || parsed.generation != generation_ )
    {
        ++counters_.rejected;
        return false;
    }
    const uint32_t frame    = parsed.frame;
    const size_t   index    = frame % RetainedFrames;
    Slot&          active   = slots_[index];
    const bool     isActive = active.occupied && active.admitted && active.frame == frame && active.generation == generation_;
    // Incomplete fragments stage separately. A stray partial for frame+64
    // cannot prematurely evict a *valid stock token* from the live sidecar.
    Slot& slot = isActive ? active : pending_[index];
    if ( !isActive && ( !slot.occupied || slot.frame != frame || slot.generation != generation_ ) )
    {
        slot            = {};
        slot.occupied   = true;
        slot.frame      = frame;
        slot.generation = generation_;
    }
    counters_.fragments += parsed.count;
    if ( slot.frozen )
        return true; // stock may replay; never mutate a published snapshot

    for ( size_t i = 0; i < parsed.count; ++i )
    {
        const FragmentView& p   = parsed.fragments[i];
        Source&             src = slot.sources[p.source];
        if ( src.invalid )
            continue;
        if ( !src.started )
        {
            src.started  = true;
            src.codec    = p.codec;
            src.channels = p.channels;
            src.length   = p.total;
        }
        if ( src.codec != p.codec || src.channels != p.channels || src.length != p.total )
        {
            // An already completed record is also a stable valid candidate;
            // a conflicting duplicate must not turn it into spurious PLC.
            if ( !src.complete )
                src.invalid = true;
            ++counters_.conflicts;
            continue;
        }
        // Identical retransmission is harmless; nonidentical overlap invalidates
        // only that source, not any other complete source or the stock frame.
        bool conflict = false;
        for ( size_t j = 0; j < p.size; ++j )
            if ( src.present.test ( p.offset + j ) && src.bytes[p.offset + j] != p.data[j] )
                conflict = true;
        if ( conflict )
        {
            // An already completed record is also a stable valid candidate;
            // a conflicting duplicate must not turn it into spurious PLC.
            if ( !src.complete )
                src.invalid = true;
            ++counters_.conflicts;
            continue;
        }
        for ( size_t j = 0; j < p.size; ++j )
        {
            if ( !src.present.test ( p.offset + j ) )
            {
                src.present.set ( p.offset + j );
                src.bytes[p.offset + j] = p.data[j];
                ++src.receivedCount;
            }
        }
        if ( !src.complete && src.receivedCount == src.length )
        {
            src.complete = true;
            ++counters_.completeRecords;
            if ( !slot.admitted )
            {
                // ONLY stock controls sequence placement and playout timing.
                // The 32+64 bits in the token are *identity*, not a new clock.
                Detail::Write32 ( input_.data(), frame );
                Detail::Write64 ( input_.data() + 4, generation_ );
                input_[TokenBytes] = uint8_t ( frame );
                ++counters_.stockPuts;
                if ( stock_.Put ( input_, int ( input_.size() ) ) )
                {
                    slot.admitted = true;
                    ++counters_.admittedFrames;
                }
            }
        }
    }
    if ( !isActive && slot.admitted )
    {
        // Admission is the one physical stock Put. Move the completed frame
        // into bounded live retention, without replacing a live sidecar on
        // mere partial fragment arrival.
        active = slot;
        slot   = {};
    }
    return true;
}

bool StockFrameBuffer::Get ( FrameView& view )
{
    view = {};
    if ( !initialized_ )
        return false;
    ++counters_.stockGets;
    if ( !stock_.Get ( output_, int ( output_.size() ) ) )
    {
        ++counters_.stockLoss;
        return false;
    }
    const uint32_t frame      = Detail::Read32 ( output_.data() );
    const uint64_t generation = Detail::Read64 ( output_.data() + 4 );
    Slot&          slot       = slots_[frame % RetainedFrames];
    if ( !slot.occupied || !slot.admitted || slot.frame != frame || slot.generation != generation || generation != generation_ )
    {
        ++counters_.expired;
        return false; // safe identity loss, NEVER another frame's data
    }
    slot.frozen     = true;
    view.frame      = frame;
    view.generation = generation;
    for ( size_t i = 0; i < MaxSources; ++i )
    {
        const Source& src = slot.sources[i];
        if ( src.complete && !src.invalid )
        {
            RecordView& record = view.records[view.count++];
            record.source      = uint16_t ( i );
            record.codec       = src.codec;
            record.channels    = src.channels;
            record.data        = src.bytes.data();
            record.size        = src.length;
        }
    }
    return true;
}
} // namespace PolyIn
