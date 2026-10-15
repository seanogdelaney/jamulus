#include "polyinsources.h"
#include <algorithm>
#include <bitset>

namespace PolyIn
{
SourceSlots::SourceSlots ( int capacity ) : capacity_ ( capacity >= 1 && capacity <= StockCapacity ? capacity : 0 ) {}

// Prefix only on a complete UTF-8 character (stock stores a UTF-8 fader name).
std::string SourceSlots::Tagged ( const std::string& base, uint16_t source )
{
    const std::string suffix = " [" + std::to_string ( unsigned ( source ) + 1 ) + "]";
    const size_t maxBase = TagBytes - std::min ( suffix.size(), size_t ( TagBytes ) );
    size_t n = std::min ( base.size(), maxBase );
    while ( n && n < base.size() && ( static_cast<unsigned char> ( base[n] ) & 0xc0 ) == 0x80 ) --n;
    return base.substr ( 0, n ) + suffix;
}

bool SourceSlots::AdmitPhysical ( int id, uint64_t generation, const std::string& stockName )
{
    if ( id < 0 || id >= capacity_ || !generation || slots_[id].active || slots_[id].held || physical_[id].active ) return false;
    physical_[id] = {true, generation};
    slots_[id].active = true;
    slots_[id].identity = {id, id, 0, generation, stockName};
    return true;
}

void SourceSlots::ClearPending ( int physical )
{
    auto& transaction = pending_[physical];
    if ( transaction.active )
        for ( const auto& s : transaction.identities ) slots_[s.channel].held = false;
    transaction = {};
}

uint64_t SourceSlots::Reserve ( int physical, uint64_t expectedGeneration, uint64_t newGeneration,
                                const std::vector<Definition>& sources )
{
    if ( physical < 0 || physical >= capacity_ || pending_[physical].active ||
         !expectedGeneration || !newGeneration || !physical_[physical].active ||
         physical_[physical].generation != expectedGeneration || sources.empty() || sources.size() > MaxSources ||
         !slots_[physical].active || slots_[physical].identity.physical != physical ) return 0;
    std::bitset<MaxSources> seen;
    std::vector<Identity> proposed;
    proposed.reserve ( sources.size() );
    bool hasZero = false;
    // Claim existing IDs for continuing logical sources (stable fader identity).
    for ( const auto& def : sources )
    {
        if ( def.source >= MaxSources || seen.test ( def.source ) ||
             def.baseName.size() > 4 * TagBytes ) return 0;
        seen.set ( def.source );
        hasZero |= def.source == 0;
        int id = def.source == 0 ? physical : -1;
        if ( id == -1 )
            for ( int c = 0; c < capacity_; ++c )
                if ( slots_[c].active && slots_[c].identity.physical == physical &&
                     slots_[c].identity.source == def.source ) { id = c; break; }
        proposed.push_back ( {id, physical, def.source, newGeneration, Tagged ( def.baseName, def.source )} );
    }
    if ( !hasZero ) return 0;
    // For new sources, retired slots of this SAME physical may be reused at
    // promotion; never steal another physical ID or its live virtual sources.
    for ( auto& proposedSource : proposed )
    {
        if ( proposedSource.channel != -1 ) continue;
        for ( int pass = 0; pass < 2 && proposedSource.channel == -1; ++pass )
            for ( int c = 0; c < capacity_; ++c )
            {
                if ( slots_[c].held || ( slots_[c].active && (pass == 0 || slots_[c].identity.physical != physical )) ||
                     physical_[c].active ) continue;
                bool assigned = false;
                for ( const auto& x : proposed ) if ( x.channel == c ) { assigned = true; break; }
                if ( !assigned ) { proposedSource.channel = c; break; }
            }
        if ( proposedSource.channel == -1 ) return 0; // transaction never published
    }
    if (++serial_ == 0) ++serial_; // zero is always invalid
    pending_[physical] = {true, serial_, physical, expectedGeneration, newGeneration, std::move ( proposed )};
    for ( const auto& s : pending_[physical].identities ) slots_[s.channel].held = true;
    return pending_[physical].ticket;
}

bool SourceSlots::Promote ( uint64_t ticket, int physical, uint64_t expectedGeneration )
{
    if ( physical < 0 || physical >= capacity_ || !pending_[physical].active || !ticket ||
         pending_[physical].ticket != ticket || pending_[physical].physical != physical ||
         pending_[physical].expected != expectedGeneration || !physical_[physical].active ||
         physical_[physical].generation != expectedGeneration ) return false;
    for ( int id = 0; id < capacity_; ++id )
        if ( slots_[id].active && slots_[id].identity.physical == physical ) slots_[id].active = false;
    for ( const auto& identity : pending_[physical].identities )
    {
        slots_[identity.channel].active = true;
        slots_[identity.channel].identity = identity;
    }
    physical_[physical].generation = pending_[physical].next;
    ClearPending ( physical );
    return true;
}

bool SourceSlots::Rollback ( uint64_t ticket, int physical, uint64_t expectedGeneration )
{
    if ( physical < 0 || physical >= capacity_ || !pending_[physical].active || !ticket ||
         pending_[physical].ticket != ticket || pending_[physical].physical != physical ||
         pending_[physical].expected != expectedGeneration || !physical_[physical].active ||
         physical_[physical].generation != expectedGeneration ) return false;
    ClearPending ( physical );
    return true;
}

bool SourceSlots::RetirePhysical ( int physical, uint64_t generation )
{
    if ( physical < 0 || physical >= capacity_ || !generation || !physical_[physical].active ||
         physical_[physical].generation != generation ) return false;
    if ( pending_[physical].active ) ClearPending ( physical );
    for ( int id = 0; id < capacity_; ++id )
        if ( slots_[id].active && slots_[id].identity.physical == physical ) slots_[id] = {};
    physical_[physical] = {};
    return true;
}

bool SourceSlots::ResolveChannel ( int channel, Identity& out ) const
{
    if ( channel < 0 || channel >= capacity_ || !slots_[channel].active ) return false;
    out = slots_[channel].identity;
    return physical_[out.physical].active && physical_[out.physical].generation == out.generation;
}

bool SourceSlots::Resolve ( int physical, uint64_t generation, uint16_t source, Identity& out ) const
{
    if ( physical < 0 || physical >= capacity_ || !generation || !physical_[physical].active ||
         physical_[physical].generation != generation || source >= MaxSources ) return false;
    for ( int id = 0; id < capacity_; ++id )
        if ( slots_[id].active && slots_[id].identity.physical == physical &&
             slots_[id].identity.generation == generation && slots_[id].identity.source == source )
        { out = slots_[id].identity; return true; }
    return false;
}

bool SourceSlots::IsTaken ( int channel ) const
{
    return channel < 0 || channel >= capacity_ || slots_[channel].active || slots_[channel].held || physical_[channel].active;
}

std::vector<SourceSlots::Identity> SourceSlots::OrdinaryChannels() const
{
    std::vector<Identity> result;
    for ( int id = 0; id < capacity_; ++id )
    {
        Identity s;
        if ( ResolveChannel ( id, s ) ) result.push_back ( std::move ( s ) );
    }
    return result;
}
}
