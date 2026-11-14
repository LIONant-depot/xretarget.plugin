#ifndef XRETARGET_DETAILS_H
#define XRETARGET_DETAILS_H
#pragma once

// What the compiler found, for the editor to show (read only): every row of the mapping with how it was decided, the warnings, and the numbers
// that follow from the two skeletons. Written to the resource's log folder as Details.txt each time the resource compiles.
#include <string>
#include <vector>

namespace xretarget_desc
{
    struct details
    {
        struct row
        {
            std::string  m_Target = {};         // destination bone
            std::string  m_Source = {};         // source bone ("" : none, it stays at rest)
            std::string  m_Slot   = {};         // hips, spine, thigh ...
            std::string  m_Side   = {};         // left, right, ""
            std::string  m_Level  = {};         // from the name, from the rest position, by chain rank, or reviewed (an override of the user's)
            std::string  m_Note   = {};

            XPROPERTY_DEF
            ( "row", row
            , obj_member<"Target", &row::m_Target, member_flags<flags::SHOW_READONLY> >
            , obj_member<"Source", &row::m_Source, member_flags<flags::SHOW_READONLY> >
            , obj_member<"Slot",   &row::m_Slot,   member_flags<flags::SHOW_READONLY> >
            , obj_member<"Side",   &row::m_Side,   member_flags<flags::SHOW_READONLY> >
            , obj_member<"Level",  &row::m_Level,  member_flags<flags::SHOW_READONLY> >
            , obj_member<"Note",   &row::m_Note,   member_flags<flags::SHOW_READONLY> >
            )
        };

        std::vector<row>          m_Rows       = {};
        std::vector<std::string>  m_Warnings   = {};
        bool                      m_bUsable    = false;     // hips found on both and at least one limb mapped
        int                       m_nMapped    = 0;
        int                       m_nUnmapped  = 0;         // destination bones that stay at rest
        float                     m_HipScale   = 1.0f;      // destination hip height / source hip height (what proportional travel multiplies by)

        XPROPERTY_DEF
        ( "details", details
        , obj_member<"Rows",       &details::m_Rows,      member_flags<flags::SHOW_READONLY> >
        , obj_member<"Warnings",   &details::m_Warnings,  member_flags<flags::SHOW_READONLY> >
        , obj_member<"Usable",     &details::m_bUsable,   member_flags<flags::SHOW_READONLY> >
        , obj_member<"Mapped",     &details::m_nMapped,   member_flags<flags::SHOW_READONLY> >
        , obj_member<"Unmapped",   &details::m_nUnmapped, member_flags<flags::SHOW_READONLY> >
        , obj_member<"HipScale",   &details::m_HipScale,  member_flags<flags::SHOW_READONLY> >
        )
    };
    XPROPERTY_REG(details)
    XPROPERTY_REG2(retarget_row_info_, details::row)
}

#endif
