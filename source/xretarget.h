#ifndef XRETARGET_RUNTIME_H
#define XRETARGET_RUNTIME_H
#pragma once

// The compiled Retarget resource: the resolved mapping from a destination skeleton's bones to a source skeleton's bones, the policies and the
// signatures of both skeletons (so a loaded resource can tell it no longer matches them). Flat arrays only, like every other resource here.
#include "dependencies/xserializer/source/xserializer.h"
#include <cstdint>
#include <cstring>

namespace xretarget
{
    struct retarget
    {
        inline static constexpr auto xserializer_version_v = 1;

        inline                  retarget    (void) noexcept = default;
        inline                  retarget    (xserializer::stream& Stream) noexcept {}
        inline void             Initialize  (void) noexcept { std::memset(this, 0, sizeof(*this)); m_SrcHips = m_TgtHips = -1; m_RegW = 1.0f; }
        inline void             Kill        (void) noexcept { if (m_pSourceOf) delete[] m_pSourceOf; Initialize(); }

        std::int32_t*   m_pSourceOf     {nullptr};      // m_nTarget entries: the source bone driving each destination bone (-1: none, it stays at rest)
        std::uint32_t   m_nTarget       {0};
        std::uint32_t   m_nSource       {0};
        std::int32_t    m_SrcHips       {-1};
        std::int32_t    m_TgtHips       {-1};
        std::uint8_t    m_Travel        {0};            // 0 proportional, 1 metric, 2 none (xanimation::travel_policy)
        float           m_RegX{0}, m_RegY{0}, m_RegZ{0}, m_RegW{1};     // registration rotation (source model frame to destination model frame)
        std::uint64_t   m_SourceSignature{0};           // hash of the source skeleton's bone name hashes and parents when this was compiled
        std::uint64_t   m_TargetSignature{0};
    };
}

namespace xserializer::io_functions
{
    template<> inline
    xerr SerializeIO<xretarget::retarget>(xserializer::stream& Stream, const xretarget::retarget& R) noexcept
    {
        xerr Err;
        false
            || (Err = Stream.Serialize(R.m_nTarget))
            || (Err = Stream.Serialize(R.m_pSourceOf, R.m_nTarget))
            || (Err = Stream.Serialize(R.m_nSource))
            || (Err = Stream.Serialize(R.m_SrcHips))
            || (Err = Stream.Serialize(R.m_TgtHips))
            || (Err = Stream.Serialize(R.m_Travel))
            || (Err = Stream.Serialize(R.m_RegX))
            || (Err = Stream.Serialize(R.m_RegY))
            || (Err = Stream.Serialize(R.m_RegZ))
            || (Err = Stream.Serialize(R.m_RegW))
            || (Err = Stream.Serialize(R.m_SourceSignature))
            || (Err = Stream.Serialize(R.m_TargetSignature))
            ;
        return Err;
    }
}

#endif
