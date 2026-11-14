#include "xretarget_compiler.h"

#include "dependencies/xstrtool/source/xstrtool.h"

#include "../xretarget_descriptor.h"
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
        // One skeleton as xAnimation sees it, from the skeleton compiler's bone manifest (final bone order, parents, rest pose)
        struct skeleton_data
        {
            std::vector<std::int32_t>           m_Parent;
            std::vector<std::string_view>       m_Names;
            xanimation::pose                    m_Rest;
            std::uint64_t                       m_Signature = 0;

            void Build(const xskeleton_desc::bone_manifest& M)
            {
                const int N = static_cast<int>(M.m_Bones.size());
                m_Parent.resize(N); m_Names.resize(N); m_Rest.resize(N);
                m_Signature = 1469598103934665603ull;                                   // FNV-1a over every bone's name hash and parent: the skeleton's identity for this resource
                auto Mix = [&](std::uint64_t V) { m_Signature = (m_Signature ^ V) * 1099511628211ull; };
                for (int i = 0; i < N; ++i)
                {
                    const auto& B = M.m_Bones[i];
                    m_Parent[i] = B.m_iParent; m_Names[i] = B.m_Name;
                    xmath::fquat Q; Q.setup(B.m_RestRotX, B.m_RestRotY, B.m_RestRotZ, B.m_RestRotW);
                    m_Rest.m_Position[i] = B.m_RestPosition; m_Rest.m_Rotation[i] = Q; m_Rest.m_Scale[i] = B.m_RestScale;
                    Mix(B.m_NameHash); Mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(B.m_iParent)));
                }
            }
            xanimation::rest_skeleton rest(void) const noexcept { return { { std::span<const std::int32_t>(m_Parent) }, &m_Rest }; }
        };

        static const char* SlotName(xanimation::bone_slot S) noexcept
        {
            static const char* N[] = { "", "root", "hips", "spine", "neck", "head", "clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "toe" };
            return N[static_cast<int>(S)];
        }
        static const char* SideName(xanimation::bone_side S) noexcept { return S == xanimation::bone_side::LEFT ? "left" : S == xanimation::bone_side::RIGHT ? "right" : ""; }
        static const char* LevelName(xanimation::map_level L) noexcept
        {
            return L == xanimation::map_level::INFERRED_NAME ? "inferred: name" : L == xanimation::map_level::INFERRED_POSITION ? "inferred: rest position" : "inferred: chain rank";
        }

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
            skeleton_data Src, Dst;
            Src.Build(m_Descriptor.m_ResolvedSource); Dst.Build(m_Descriptor.m_ResolvedDestination);
            if (Src.m_Parent.empty() || Dst.m_Parent.empty())
                return xerr::create_f<state, "A skeleton has no bones (has it been compiled?)">();

            // 1. automatic mapping from the names and the hierarchy (xAnimation retarget)
            auto Auto = xanimation::AutoMapBones(Src.rest(), Src.m_Names, Dst.rest(), Dst.m_Names);

            // 2. the user's corrections, by bone name
            std::vector<std::string> Reviewed(Dst.m_Parent.size());
            auto Find = [](const skeleton_data& S, const std::string& Name) { for (int i = 0; i < static_cast<int>(S.m_Names.size()); ++i) if (S.m_Names[i] == Name) return i; return -1; };
            for (auto& O : m_Descriptor.m_Overrides)
            {
                const int T = Find(Dst, O.m_Target);
                if (T < 0) { Auto.m_Warnings.push_back(std::format("override: destination bone [{}] not found", O.m_Target)); continue; }
                if (O.m_bUnmap) { Auto.m_Map.m_SourceOf[T] = -1; Reviewed[T] = "unmapped by the user"; continue; }
                const int S = Find(Src, O.m_Source);
                if (S < 0) { Auto.m_Warnings.push_back(std::format("override: source bone [{}] not found", O.m_Source)); continue; }
                Auto.m_Map.m_SourceOf[T] = S; Reviewed[T] = "mapped by the user";
            }

            // 3. policies
            Auto.m_Map.m_Travel = static_cast<xanimation::travel_policy>(m_Descriptor.m_Travel);
            {
                const auto D = m_Descriptor.m_RegistrationDeg; const float K = 0.017453292f;     // XYZ Euler, degrees: X first, then Y, then Z
                xmath::fquat X, Y, Z; X.setupRotationX(xmath::radian{ D.m_X * K }); Y.setupRotationY(xmath::radian{ D.m_Y * K }); Z.setupRotationZ(xmath::radian{ D.m_Z * K });
                Auto.m_Map.m_Registration = Z * Y * X;
            }
            // hips of each side must stay inside their skeleton
            if (Auto.m_Map.m_SrcHips >= static_cast<int>(Src.m_Parent.size()) || Auto.m_Map.m_TgtHips >= static_cast<int>(Dst.m_Parent.size()))
                return xerr::create_f<state, "Hips index out of range">();

            // 4. the numbers that follow, and the editor's view
            xanimation::retargeter R;
            R.Init(Src.rest(), Dst.rest(), Auto.m_Map);
            m_Details = {};
            m_Details.m_Warnings = Auto.m_Warnings; m_Details.m_bUsable = Auto.m_bUsable; m_Details.m_HipScale = R.hipScale();
            for (int t = 0; t < static_cast<int>(Dst.m_Parent.size()); ++t)
            {
                xretarget_desc::details::row Row; Row.m_Target = std::string(Dst.m_Names[t]);
                const int S = Auto.m_Map.m_SourceOf[t];
                if (S >= 0) { Row.m_Source = std::string(Src.m_Names[S]); ++m_Details.m_nMapped; } else ++m_Details.m_nUnmapped;
                for (const auto& A : Auto.m_Rows) if (A.m_Target == t) { Row.m_Slot = SlotName(A.m_Slot); Row.m_Side = SideName(A.m_Side); Row.m_Level = LevelName(A.m_Level); Row.m_Note = A.m_Note; break; }
                if (!Reviewed[t].empty()) { Row.m_Level = "reviewed"; Row.m_Note = Reviewed[t]; }
                m_Details.m_Rows.push_back(std::move(Row));
            }
            if (!Auto.m_bUsable) LogMessage(xresource_pipeline::msg_type::WARNING, "The automatic mapping is not usable (no hips pair or no limb mapped): correct it with overrides");

            // 5. the compiled resource
            m_Final.Initialize();
            m_Final.m_nTarget = static_cast<std::uint32_t>(Dst.m_Parent.size()); m_Final.m_nSource = static_cast<std::uint32_t>(Src.m_Parent.size());
            m_Final.m_pSourceOf = new std::int32_t[Dst.m_Parent.size()];
            std::copy(Auto.m_Map.m_SourceOf.begin(), Auto.m_Map.m_SourceOf.end(), m_Final.m_pSourceOf);
            m_Final.m_SrcHips = Auto.m_Map.m_SrcHips; m_Final.m_TgtHips = Auto.m_Map.m_TgtHips;
            m_Final.m_Travel = static_cast<std::uint8_t>(Auto.m_Map.m_Travel);
            m_Final.m_RegX = Auto.m_Map.m_Registration.m_X; m_Final.m_RegY = Auto.m_Map.m_Registration.m_Y; m_Final.m_RegZ = Auto.m_Map.m_Registration.m_Z; m_Final.m_RegW = Auto.m_Map.m_Registration.m_W;
            m_Final.m_SourceSignature = Src.m_Signature; m_Final.m_TargetSignature = Dst.m_Signature;
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
