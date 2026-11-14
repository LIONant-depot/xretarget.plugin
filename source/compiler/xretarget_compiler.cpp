#include "xretarget_compiler.h"

#include "dependencies/xstrtool/source/xstrtool.h"

#include "../xretarget_descriptor.h"
#include "../xretarget_mapping.h"
#include "../xretarget.h"
#include "../xretarget_details.h"

#include "dependencies/xAnimation/source/xanimation.h"

#include "dependencies/xproperty/source/xcore/my_properties.cpp"
#include "dependencies/xmath/source/bridge/xmath_to_xproperty.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace xretarget_compiler
{
    struct implementation : xretarget_compiler::instance
    {
        //--------------------------------------------------------------------------------------

        xerr onCompile(void) noexcept override
        {
            displayProgressBar("Loading Descriptor", 0);
            {
                xproperty::settings::context    Context{};
                auto                            DescriptorFileName = std::format(L"{}/{}/Descriptor.txt", m_ProjectPaths.m_Project, m_InputSrcDescriptorPath);
                if (auto Err = m_Descriptor.Serialize(true, DescriptorFileName, Context); Err)
                    return Err;
            }
            {
                std::vector<std::string> Errors;
                m_Descriptor.Validate(Errors);
                if (not Errors.empty())
                {
                    for (auto& E : Errors) LogMessage(xresource_pipeline::msg_type::ERROR, std::move(E));
                    return xerr::create_f<state, "Validation Errors">();
                }
            }
            displayProgressBar("Loading Descriptor", 1);

            // The resource follows both skeletons: it compiles again whenever either does
            for (const auto* pRef : { &m_Descriptor.m_Source, &m_Descriptor.m_Destination })
            {
                m_Dependencies.m_Resources.push_back({ pRef->m_Instance, xrsc::skeleton_type_guid_v });
                m_Dependencies.m_Assets.push_back(xskeleton_desc::ManifestRelativePath(pRef->m_Instance.m_Value));
            }

            try
            {
                displayProgressBar("Mapping bones", 0);
                if (auto Err = BuildMapping(); Err) return Err;
                displayProgressBar("Mapping bones", 1);
            }
            catch (std::runtime_error Error)
            {
                LogMessage(xresource_pipeline::msg_type::ERROR, std::format("{}", Error.what()));
                return xerr::create_f<state, "Exception thrown">();
            }

            {
                xtextfile::stream File;
                if (auto Err = File.Open(false, std::format(L"{}\\Details.txt", m_ResourceLogPath), xtextfile::file_type::TEXT); Err)
                    return xerr::create_f<state, "Failed while opening the details.txt so it can't be saved">(Err);
                xproperty::settings::context C{};
                if (auto Err = xproperty::sprop::serializer::Stream(File, m_Details, C); Err)
                    return xerr::create_f<state, "Failed while serializing details.txt">(Err);
            }

            int Count = 0;
            for (auto& T : m_Target)
            {
                displayProgressBar("Serializing", Count++ / (float)m_Target.size());
                if (T.m_bValid) Serialize(T.m_DataPath);
            }
            displayProgressBar("Serializing", 1);
            return {};
        }

        //--------------------------------------------------------------------------------------

        xerr BuildMapping(void)
        {
            if (auto Err = xretarget_desc::MakeMapping(m_Descriptor, m_Mapping); Err) return Err;
            const auto& Auto = m_Mapping.m_Auto; const auto& Src = m_Mapping.m_Source; const auto& Dst = m_Mapping.m_Destination;
            if (!Auto.m_bUsable) LogMessage(xresource_pipeline::msg_type::WARNING, "The automatic mapping is not usable (no hips pair or no limb mapped): correct it with overrides");

            // the compiled resource
            m_Final.Initialize();
            m_Final.m_nTarget = static_cast<std::uint32_t>(Dst.m_Parent.size()); m_Final.m_nSource = static_cast<std::uint32_t>(Src.m_Parent.size());
            m_Final.m_pSourceOf = new std::int32_t[Dst.m_Parent.size()];
            std::copy(Auto.m_Map.m_SourceOf.begin(), Auto.m_Map.m_SourceOf.end(), m_Final.m_pSourceOf);
            m_Final.m_SrcHips = Auto.m_Map.m_SrcHips; m_Final.m_TgtHips = Auto.m_Map.m_TgtHips;
            m_Final.m_Travel = static_cast<std::uint8_t>(Auto.m_Map.m_Travel);
            m_Final.m_RegX = Auto.m_Map.m_Registration.m_X; m_Final.m_RegY = Auto.m_Map.m_Registration.m_Y; m_Final.m_RegZ = Auto.m_Map.m_Registration.m_Z; m_Final.m_RegW = Auto.m_Map.m_Registration.m_W;
            m_Final.m_SourceSignature = Src.m_Signature; m_Final.m_TargetSignature = Dst.m_Signature;
            m_Details = m_Mapping.m_Details;
            return {};
        }

        //--------------------------------------------------------------------------------------

        void Serialize(const std::wstring_view FilePath)
        {
            xserializer::stream Serializer;
            if (auto Err = Serializer.Save
                ( FilePath
                , m_Final
                , m_OptimizationType == optimization_type::O0 ? xserializer::compression_level::FAST : m_OptimizationType == optimization_type::O1 ? xserializer::compression_level::MEDIUM : xserializer::compression_level::HIGH
                ); Err)
            {
                throw(std::runtime_error(std::string(Err.getMessage())));
            }
        }

        implementation() { m_Final.Initialize(); }

        xretarget_desc::mapping      m_Mapping;
        xretarget_desc::details      m_Details;
        xretarget_desc::descriptor   m_Descriptor;
        xretarget::retarget          m_Final;
    };

    //------------------------------------------------------------------------------------

    std::unique_ptr<instance> instance::Create(void)
    {
        return std::make_unique<implementation>();
    }
}
