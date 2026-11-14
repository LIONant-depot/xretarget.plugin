#ifndef XRETARGET_COMPILER_H
#define XRETARGET_COMPILER_H
#pragma once

#include "dependencies/xresource_pipeline_v2/source/xresource_pipeline.h"

namespace xretarget_compiler
{
    enum class state : std::uint8_t
    { OK
    , FAILURE
    };

    struct instance : xresource_pipeline::compiler::base
    {
        static std::unique_ptr<instance> Create(void);
    };
}

#endif
