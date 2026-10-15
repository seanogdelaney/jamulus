/******************************************************************************\
 * Bounded physical-owner to ordinary stock channel-ID mapping.
 * Copyright (c) 2026. AGPL-3.0-or-later.
\******************************************************************************/
#pragma once
#include "polyinwire.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace PolyIn
{
// IDs are indices in the EXISTING server vecChannels / CChannelInfo namespace.
// A physical slot doubles as source zero, and virtual slots occupy the same
// bounded namespace. Caller must exclude IsTaken() IDs from stock admission.
class SourceSlots
{
public:
    static constexpr int StockCapacity = 150; // src/global.h MAX_NUM_CHANNELS (compile-checked by integration/tests)
    static constexpr int TagBytes = 16;       // src/global.h MAX_LEN_FADER_TAG
    struct Identity
    {
        int channel = -1;
        int physical = -1;
        uint16_t source = 0;
        uint64_t generation = 0;
        std::string tag;
    };
    struct Definition { uint16_t source; std::string baseName; };
    explicit SourceSlots ( int capacity );
    int Capacity() const { return capacity_; }
    // Called only after stock creates an actual physical CChannel at 'id'.
    // Stock InitChannel/ResetInfo starts with an empty name; admission cannot
    // depend on a later metadata/control message supplying a display name.
    bool AdmitPhysical ( int id, uint64_t generation, const std::string& stockName );
    // Start replacement/churn with compare-and-swap against current generation.
    // This DOES NOT change the active roster until Promote. A nonzero new
    // generation denotes genuine session replacement; equal means source churn.
    uint64_t Reserve ( int physical, uint64_t expectedGeneration, uint64_t newGeneration,
                       const std::vector<Definition>& sources );
    bool Promote ( uint64_t ticket, int physical, uint64_t expectedGeneration );
    bool Rollback ( uint64_t ticket, int physical, uint64_t expectedGeneration );
    // A delayed disconnect cannot retire a successor epoch.
    bool RetirePhysical ( int physical, uint64_t generation );
    bool Resolve ( int physical, uint64_t generation, uint16_t source, Identity& out ) const;
    bool ResolveChannel ( int channel, Identity& out ) const;
    bool IsTaken ( int channel ) const;
    std::vector<Identity> OrdinaryChannels() const; // sorted ascending by stock ID
    static std::string Tagged ( const std::string& base, uint16_t source );
private:
    struct Slot { bool active=false, held=false; Identity identity; };
    struct Physical { bool active=false; uint64_t generation=0; };
    struct Pending
    {
        bool active=false;
        uint64_t ticket=0;
        int physical=-1;
        uint64_t expected=0, next=0;
        std::vector<Identity> identities;
    };
    std::array<Pending, StockCapacity> pending_{};
    int capacity_;
    uint64_t serial_=0;
    std::array<Slot, StockCapacity> slots_{};
    std::array<Physical, StockCapacity> physical_{};
    void ClearPending ( int physical );
};
}
