#ifndef XRETARGET_REF_H
#define XRETARGET_REF_H
#pragma once

// How other resources name a Retarget resource (an animation package's import source says which Retarget moves its clips onto the package's skeleton)
#include "dependencies/xresource_guid/source/xresource_guid.h"

namespace xrsc
{
    inline static constexpr auto    retarget_type_guid_v    = xresource::type_guid(xresource::guid_generator::Instance64FromString("xRetarget"));
    using                           retarget                = xresource::def_guid<retarget_type_guid_v>;
}

#endif
