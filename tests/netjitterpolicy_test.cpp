#include "../src/netjitterpolicy.h"
#include <cassert>
#include <iostream>
int main()
{
    int sizes[] = {2,3,4,5};
    double errors[] = {0.1, 0.004, 0.0001, 0};
    auto d = NetReceive::SelectAutoSize(errors,sizes,4,0.001,0.01);
    assert(d.normal==4 && d.emergency==3 && !d.noneBelowEmergency);
    for(double &e:errors) e=0.1;
    d=NetReceive::SelectAutoSize(errors,sizes,4,0.001,0.01);
    assert(d.normal==5 && d.emergency==5 && d.noneBelowEmergency);
    std::cout << "netjitterpolicy: PASS\n";
}
