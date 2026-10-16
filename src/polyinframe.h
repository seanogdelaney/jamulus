/******************************************************************************\
 * Stock receive window adapter; only identity/sidecars are feature-owned.
 * Copyright (c) 2026. AGPL-3.0-or-later.
\******************************************************************************/
#pragma once
#include "polyinwire.h"
#include "buffer.h"
#include <bitset>

namespace PolyIn
{
struct Counters
{
    uint64_t datagrams       = 0;
    uint64_t fragments       = 0;
    uint64_t completeRecords = 0;
    uint64_t admittedFrames  = 0;
    uint64_t stockPuts       = 0;
    uint64_t stockGets       = 0;
    uint64_t stockLoss       = 0;
    uint64_t rejected        = 0;
    uint64_t conflicts       = 0;
    uint64_t expired         = 0;
};

// Views are borrowed while the owning physical-session lock is held; do not
// cache them across a subsequent Receive/Reset. Data remain immutable within
// a slot after its first successful stock Get, including replay/backshift.
struct RecordView
{
    uint16_t       source   = 0;
    uint8_t        codec    = 0;
    uint8_t        channels = 0;
    const uint8_t* data     = nullptr;
    uint16_t       size     = 0;
};
struct FrameView
{
    uint32_t                           frame      = 0;
    uint64_t                           generation = 0;
    size_t                             count      = 0;
    std::array<RecordView, MaxSources> records{};
};

class StockFrameBuffer
{
public:
    explicit StockFrameBuffer ( CNetBufWithStats& existingPhysicalBuffer ) : stock_ ( existingPhysicalBuffer ) {}
    StockFrameBuffer ( const StockFrameBuffer& )            = delete;
    StockFrameBuffer& operator= ( const StockFrameBuffer& ) = delete;
    // One and only one actual production stock buffer per physical session.
    // Caller serializes Receive/Get/Resize/Reset using the stock session lock.
    // stockWindowBlocks uses the ordinary CChannel 1..20 physical-window range;
    // RetainedFrames=64 is NOT a physical jitter-buffer configuration value.
    // Caller preserves CChannel's SetUseDoubleSystemFrameSize-before-Init order.
    void            Init ( uint64_t sessionGeneration, int stockWindowBlocks );
    void            Resize ( int stockWindowBlocks );
    bool            Receive ( const uint8_t* packet, size_t bytes );
    bool            Get ( FrameView& view );
    void            GetErrorRates ( CVector<double>& rates, double& low, double& upper ) { stock_.GetErrorRates ( rates, low, upper ); }
    int             GetAutoSetting() { return stock_.GetAutoSetting(); }
    const Counters& Stats() const { return counters_; }
    uint64_t        Generation() const { return generation_; }

private:
    struct Source
    {
        uint8_t                              codec = 0, channels = 0;
        uint16_t                             length = 0, receivedCount = 0;
        bool                                 started = false, complete = false, invalid = false;
        std::array<uint8_t, MaxEncodedBytes> bytes{};
        std::bitset<MaxEncodedBytes>         present{};
    };
    struct Slot
    {
        uint32_t                       frame      = 0;
        uint64_t                       generation = 0;
        bool                           occupied = false, admitted = false, frozen = false;
        std::array<Source, MaxSources> sources{};
    };
    CNetBufWithStats&                stock_; // BORROW existing CChannel::SockBuf; never allocate a second stock buffer
    CVector<uint8_t>                 input_;
    CVector<uint8_t>                 output_;
    std::array<Slot, RetainedFrames> slots_{};   // stock-admitted snapshots only
    std::array<Slot, RetainedFrames> pending_{}; // incomplete frames cannot evict live stock tokens
    Counters                         counters_{};
    uint64_t                         generation_  = 0;
    bool                             initialized_ = false;
};
} // namespace PolyIn
