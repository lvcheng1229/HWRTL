#include <iostream>
#include "../hwrtl_pvs.h"

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(10686);
    {
        hwrtl::pvs::InitPVSGenerator();
        hwrtl::pvs::AddOccluderBound(hwrtl::pvs::SOccluderBound{ hwrtl::Vec3(0,0,0),hwrtl::Vec3(1,1,1) });
        hwrtl::pvs::DestoryPVSGenerator();
        std::cout << "end pvs example";
    }
    _CrtDumpMemoryLeaks();
}