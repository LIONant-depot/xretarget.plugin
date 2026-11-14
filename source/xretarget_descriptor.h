#ifndef XRETARGET_DESCRIPTOR_H
#define XRETARGET_DESCRIPTOR_H
#pragma once

#include "plugins/xskeleton.plugin/source/xskeleton_xgpu_rsc_loader.h"
#include "plugins/xskeleton.plugin/source/xskeleton_bone_manifest.h"
#include "xretarget.h"
#include "xretarget_details.h"
#include <format>

namespace xretarget_desc
{
    inline static constexpr auto    resource_type_guid_v    = xresource::type_guid(xresource::guid_generator::Instance64FromString("xRetarget"));

    // How the hips' travel is carried over (xanimation::travel_policy)
    enum class travel : std::uint8_t { PROPORTIONAL, METRIC, NONE };
    static constexpr auto travel_v = std::array
    { xproperty::settings::enum_item("Proportional", travel::PROPORTIONAL)
    , xproperty::settings::enum_item("Metric",       travel::METRIC)
    , xproperty::settings::enum_item("None",         travel::NONE)
    };

    // A correction of the automatic mapping, by bone NAME (the names are what survives a skeleton being compiled again)
    struct bone_override
    {
        std::string     m_Target    = {};       // destination bone
        std::string     m_Source    = {};       // the source bone that drives it
        bool            m_bUnmap    = false;    // true: no source drives it (it stays at rest), whatever m_Source says

        XPROPERTY_DEF
        ( "BoneOverride", bone_override
        , obj_member<"Target", &bone_override::m_Target >
        , obj_member<"Source", &bone_override::m_Source >
        , obj_member<"Unmap",  &bone_override::m_bUnmap >
        )
    };
    XPROPERTY_REG(bone_override)

    struct descriptor : xresource_pipeline::descriptor::base
    {
        using parent = xresource_pipeline::descriptor::base;

        void SetupFromSource(std::string_view FileName) override {}

        void Validate(std::vector<std::string>& Errors) const noexcept override
        {
            if (m_Source.empty())      Errors.push_back("The source skeleton reference is NULL");
            if (m_Destination.empty()) Errors.push_back("The destination skeleton reference is NULL");
            if (!m_Source.empty() && m_Source.m_Instance.m_Value == m_Destination.m_Instance.m_Value) Errors.push_back("The source and the destination are the same skeleton: nothing to retarget");
        }

        // The skeleton compiler exports each skeleton's final bone list (names, parents, rest pose) to its log folder; this reads it. The same file the
        // animation package reads ("AnimPackage.txt" is its name for every consumer: the content is the skeleton's, not the consumer's).
        static xerr ReadManifest(xskeleton_desc::bone_manifest& Out, const xrsc::skeleton& Ref, std::wstring_view DescriptorFileName) noexcept
        {
            auto Pos = DescriptorFileName.rfind(L".lionprj");
            if (Pos != std::wstring_view::npos) Pos += sizeof(".lionprj");
            else if (Pos = DescriptorFileName.rfind(L".lionlib"); Pos != std::wstring_view::npos) Pos += sizeof(".lionlib");
            else return xerr::create_f<xresource_pipeline::state, "Unable to locate the project path, fail to load a skeleton's bone manifest">();

            const auto ProjectPath = DescriptorFileName.substr(0, Pos - 1);
            const auto Path = std::format(L"{}/{}", ProjectPath, xskeleton_desc::ManifestRelativePath(Ref.m_Instance.m_Value));

            xtextfile::stream File; xproperty::settings::context C;
            if (auto Err = File.Open(true, Path, xtextfile::file_type::TEXT); Err)
                return xerr::create_f<xresource_pipeline::state, "Error opening a skeleton's bone manifest - has it been compiled yet?">(Err);
            if (auto Err = xproperty::sprop::serializer::Stream(File, Out, C); Err)
                return xerr::create_f<xresource_pipeline::state, "Error reading a skeleton's bone manifest">(Err);
            return {};
        }

        xerr Serialize(bool isReading, std::wstring_view FileName, xproperty::settings::context& Context) noexcept override
        {
            if (auto Err = xresource_pipeline::descriptor::base::Serialize(isReading, FileName, Context); Err)
                return Err;
            if (not isReading) return {};
            if (!m_Source.empty())      if (auto Err = ReadManifest(m_ResolvedSource,      m_Source,      FileName); Err) return Err;
            if (!m_Destination.empty()) if (auto Err = ReadManifest(m_ResolvedDestination, m_Destination, FileName); Err) return Err;
            return {};
        }

        xrsc::skeleton                  m_Source        = {};   // the skeleton the clips were authored on
        xrsc::skeleton                  m_Destination   = {};   // the skeleton they must play on
        travel                          m_Travel        = travel::PROPORTIONAL;
        xmath::fvec3                    m_RegistrationDeg = xmath::fvec3::fromZero();   // turn of the source's model frame into the destination's (degrees, XYZ)
        std::vector<bone_override>      m_Overrides     = {};   // the user's corrections of the automatic mapping

        xskeleton_desc::bone_manifest   m_ResolvedSource, m_ResolvedDestination;      // NOT saved: read in Serialize()

        XPROPERTY_VDEF
        ( "Retarget", descriptor
        , obj_member<"Source",              &descriptor::m_Source >
        , obj_member<"Destination",         &descriptor::m_Destination >
        , obj_member<"Travel",              &descriptor::m_Travel, member_enum_span<travel_v> >
        , obj_member<"RegistrationDeg",     &descriptor::m_RegistrationDeg >
        , obj_member<"Overrides",           &descriptor::m_Overrides >
        , obj_member<"ResolvedSource",      &descriptor::m_ResolvedSource,      member_flags<flags::DONT_SHOW, flags::DONT_SAVE> >
        , obj_member<"ResolvedDestination", &descriptor::m_ResolvedDestination, member_flags<flags::DONT_SHOW, flags::DONT_SAVE> >
        )
    };
    XPROPERTY_VREG(descriptor)

    //--------------------------------------------------------------------------------------

    struct factory final : xresource_pipeline::factory_base
    {
        using xresource_pipeline::factory_base::factory_base;

        std::unique_ptr<xresource_pipeline::descriptor::base> CreateDescriptor(void) const noexcept override { return std::make_unique<descriptor>(); }
        xresource::type_guid        ResourceTypeGUID(void) const noexcept override { return resource_type_guid_v; }
        const char*                 ResourceTypeName(void) const noexcept override { return "Retarget"; }
        const xproperty::type::object& ResourceXPropertyObject(void) const noexcept override { return *xproperty::getObjectByType<descriptor>(); }
    };

    inline static factory g_Factory{};
}

#endif
