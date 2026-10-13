#pragma once
#include <algorithm>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace lvm {
inline std::size_t processor_participants() {
    const auto logical=std::max(1U,std::thread::hardware_concurrency());
    std::size_t physical=0;
#ifdef _WIN32
    DWORD bytes=0;
    GetLogicalProcessorInformation(nullptr,&bytes);
    if(bytes && bytes<=1024*1024) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info((bytes+sizeof(info[0])-1)/sizeof(info[0]));
        DWORD_PTR allowed=~DWORD_PTR(0),system=0;
        GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system);
        if(GetLogicalProcessorInformation(info.data(),&bytes))
            for(std::size_t i=0;i<bytes/sizeof(info[0]);++i)
                if(info[i].Relationship==RelationProcessorCore && (info[i].ProcessorMask&allowed))++physical;
    }
#endif
    return std::min<std::size_t>(32,physical ? std::min<std::size_t>(physical,logical) : logical);
}
}
