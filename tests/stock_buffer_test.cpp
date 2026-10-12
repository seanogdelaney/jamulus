// Characterisation of the unchanged stock CNetBuf public API.
// Build on an SDK-equipped machine: qmake stock_buffer_tests.pro && make && ./stock_buffer_tests
#include "../src/buffer.h"
#include <cassert>
#include <iostream>

static CVector<uint8_t> Block ( uint8_t data, uint8_t sequence )
{
    CVector<uint8_t> b ( 2 );
    b[0] = data;
    b[1] = sequence;
    return b;
}
int main()
{
    CVector<uint8_t> output ( 1 );
    CNetBuf          ordered;
    ordered.Init ( 1, 3, true );
    assert ( ordered.Put ( Block ( 42, 0 ), 2 ) );
    assert ( ordered.Get ( output, 1 ) && output[0] == 42 );
    assert ( ordered.Put ( Block ( 43, 1 ), 2 ) );
    assert ( ordered.Get ( output, 1 ) && output[0] == 43 );
    assert ( !ordered.Get ( output, 1 ) );

    CNetBuf wrap;
    wrap.Init ( 1, 3, true );
    // 255 is one packet behind sequence zero; it moves the get window backwards.
    assert ( wrap.Put ( Block ( 77, 255 ), 2 ) );
    assert ( wrap.Get ( output, 1 ) && output[0] == 77 );
    assert ( wrap.Put ( Block ( 78, 0 ), 2 ) );
    assert ( wrap.Get ( output, 1 ) && output[0] == 78 );

    CNetBuf fifo;
    fifo.Init ( 1, 2, false );
    CVector<uint8_t> bytes ( 1 );
    bytes[0] = 99;
    assert ( fifo.Put ( bytes, 1 ) );
    assert ( fifo.Get ( output, 1 ) && output[0] == 99 );
    assert ( !fifo.Get ( output, 1 ) );
    std::cout << "stock CNetBuf: PASS\n";
}
