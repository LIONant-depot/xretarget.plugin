#ifndef XRETARGET_MAPPING_H
#define XRETARGET_MAPPING_H
#pragma once

// The mapping a Retarget descriptor describes, resolved: the two skeletons as xAnimation sees them (from their bone manifests), the automatic bone
// mapping, the user's overrides applied, the policies, and the editor's view of it (details). The Retarget compiler writes it out; any other compiler that
// needs the transfer (the animation package, importing a clip made on another skeleton) builds the same thing from the same descriptor, so there is one answer.
#include "xretarget_descriptor.h"
#include "dependencies/xAnimation/source/xanimation.h"
#include <string_view>

namespace xretarget_desc
{
    // One skeleton as xAnimation sees it, from the skeleton compiler's bone manifest (final bone order, parents, rest pose)
    struct skeleton_data
    {
        std::vector<std::int32_t>           m_Parent;
        std::vector<std::string_view>       m_Names;         // views into the manifest: it must outlive this
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

    struct mapping
    {
        skeleton_data                   m_Source, m_Destination;
        xanimation::pose                m_SourceCalibration, m_DestinationCalibration;   // each skeleton's calibration pose (limbs along the family directions): what the transfer treats as the rest
        xanimation::rest_skeleton       sourceCalibrated(void) const noexcept      { return { { std::span<const std::int32_t>(m_Source.m_Parent) }, &m_SourceCalibration }; }
        xanimation::rest_skeleton       destinationCalibrated(void) const noexcept { return { { std::span<const std::int32_t>(m_Destination.m_Parent) }, &m_DestinationCalibration }; }
        xanimation::auto_map_result     m_Auto;                 // the final map is m_Auto.m_Map (overrides and policies applied)
        details                         m_Details;
        float                           m_AutoFacingDeg = 0.0f; // the registration measured from the two facings (about Y)
        std::vector<std::string>        m_Reviewed;             // per destination bone: why the user's override applies ("" none)
    };

    inline const char* SlotName(xanimation::bone_slot S) noexcept
    {
        static const char* N[] = { "", "root", "hips", "spine", "neck", "head", "clavicle", "upperarm", "lowerarm", "hand", "thigh", "calf", "foot", "toe" };
        return N[static_cast<int>(S)];
    }
    inline const char* SideName(xanimation::bone_side S) noexcept { return S == xanimation::bone_side::LEFT ? "left" : S == xanimation::bone_side::RIGHT ? "right" : ""; }
    inline const char* LevelName(xanimation::map_level L) noexcept
    {
        return L == xanimation::map_level::INFERRED_NAME ? "inferred: name" : L == xanimation::map_level::INFERRED_POSITION ? "inferred: rest position" : "inferred: chain rank";
    }

    // Resolves a descriptor (its two bone manifests already read by Serialize). Fails when a skeleton has no bones. M must stay alive: names are views into
    // the descriptor's manifests.
    inline xerr MakeMapping(const descriptor& D, mapping& M)
    {
        M.m_Source.Build(D.m_ResolvedSource); M.m_Destination.Build(D.m_ResolvedDestination);
        auto& Src = M.m_Source; auto& Dst = M.m_Destination;
        if (Src.m_Parent.empty() || Dst.m_Parent.empty())
            return xerr::create_f<xresource_pipeline::state, "A skeleton has no bones (has it been compiled?)">();

        // 0. each skeleton's calibration pose: its rest with the limbs, spine and neck brought along the family's directions, so an A-pose rig and a T-pose rig are
        //    compared in the same stance (the rest poses themselves, and the skins' bind poses, are never changed)
        //    (each in its own facing: the limbs go out along ITS left, so a skeleton authored facing another way is calibrated in its own frame and the registration is the turn between the two)
        M.m_SourceCalibration      = xanimation::SeedCalibrationPose(Src.rest().m_Skel, Src.m_Names, Src.m_Rest, xanimation::MeasureFacingDeg(Src.rest().m_Skel, Src.m_Names, Src.m_Rest));
        M.m_DestinationCalibration = xanimation::SeedCalibrationPose(Dst.rest().m_Skel, Dst.m_Names, Dst.m_Rest, xanimation::MeasureFacingDeg(Dst.rest().m_Skel, Dst.m_Names, Dst.m_Rest));

        // 1. automatic mapping from the names and the hierarchy (xAnimation retarget)
        M.m_Auto = xanimation::AutoMapBones(Src.rest(), Src.m_Names, Dst.rest(), Dst.m_Names);
        auto& Auto = M.m_Auto;

        // 2. the user's corrections, by bone name
        M.m_Reviewed.assign(Dst.m_Parent.size(), std::string());
        auto Find = [](const skeleton_data& S, const std::string& Name) { for (int i = 0; i < static_cast<int>(S.m_Names.size()); ++i) if (S.m_Names[i] == Name) return i; return -1; };
        for (auto& O : D.m_Overrides)
        {
            const int T = Find(Dst, O.m_Target);
            if (T < 0) { Auto.m_Warnings.push_back(std::format("override: destination bone [{}] not found", O.m_Target)); continue; }
            if (O.m_bUnmap) { Auto.m_Map.m_SourceOf[T] = -1; M.m_Reviewed[T] = "unmapped by the user"; continue; }
            const int S = Find(Src, O.m_Source);
            if (S < 0) { Auto.m_Warnings.push_back(std::format("override: source bone [{}] not found", O.m_Source)); continue; }
            Auto.m_Map.m_SourceOf[T] = S; M.m_Reviewed[T] = "mapped by the user";
        }

        // 3. policies
        Auto.m_Map.m_Travel = static_cast<xanimation::travel_policy>(D.m_Travel);
        // The model frames: two skeletons authored facing different ways (a PreTransform not finished, or two tools' conventions) need the turn between them or every limb
        // swings about the wrong axis. It is measured from the facing of the two calibrated rests (the left foot to its toe) and applied automatically; the descriptor's
        // RegistrationDeg is a correction ON TOP of it (zero by default).
        float AutoFacingDeg = 0.0f;
        xmath::fquat AutoReg = xmath::fquat::fromIdentity();
        {
            const auto Raw = xanimation::CompareFrames(M.sourceCalibrated(), M.destinationCalibrated(), Auto);
            if (Raw.m_bHaveFacing && std::fabs(Raw.m_FacingDeg) > 5.0f)
            {
                AutoFacingDeg = Raw.m_FacingDeg; AutoReg.setupRotationY(xmath::radian{ AutoFacingDeg * 0.017453292f });
                Auto.m_Warnings.push_back(std::format("the skeletons face different ways ({:.1f} degrees about Y): a registration of that turn is applied automatically", AutoFacingDeg));
            }
        }
        {
            const auto E = D.m_RegistrationDeg; const float K = 0.017453292f;     // XYZ Euler, degrees: X first, then Y, then Z
            xmath::fquat X, Y, Z; X.setupRotationX(xmath::radian{ E.m_X * K }); Y.setupRotationY(xmath::radian{ E.m_Y * K }); Z.setupRotationZ(xmath::radian{ E.m_Z * K });
            Auto.m_Map.m_Registration = (Z * Y * X) * AutoReg;
        }
        M.m_AutoFacingDeg = AutoFacingDeg;
        if (Auto.m_Map.m_SrcHips >= static_cast<int>(Src.m_Parent.size()) || Auto.m_Map.m_TgtHips >= static_cast<int>(Dst.m_Parent.size()))
            return xerr::create_f<xresource_pipeline::state, "Hips index out of range">();

        // do the two skeletons agree on size and facing? (their PreTransforms should have normalized them)
        for (const auto& W : xanimation::CompareFrames(M.sourceCalibrated(), M.destinationCalibrated(), Auto, Auto.m_Map.m_Registration).m_Warnings) Auto.m_Warnings.push_back(W);     // what still disagrees AFTER the registration

        // 4. the numbers that follow, and the editor's view
        xanimation::retargeter R;
        R.Init(M.sourceCalibrated(), M.destinationCalibrated(), Auto.m_Map);
        M.m_Details = {};
        M.m_Details.m_Warnings = Auto.m_Warnings; M.m_Details.m_bUsable = Auto.m_bUsable; M.m_Details.m_HipScale = R.hipScale();
        for (int t = 0; t < static_cast<int>(Dst.m_Parent.size()); ++t)
        {
            details::row Row; Row.m_Target = std::string(Dst.m_Names[t]);
            const int S = Auto.m_Map.m_SourceOf[t];
            if (S >= 0) { Row.m_Source = std::string(Src.m_Names[S]); ++M.m_Details.m_nMapped; } else ++M.m_Details.m_nUnmapped;
            for (const auto& A : Auto.m_Rows) if (A.m_Target == t) { Row.m_Slot = SlotName(A.m_Slot); Row.m_Side = SideName(A.m_Side); Row.m_Level = LevelName(A.m_Level); Row.m_Note = A.m_Note; break; }
            if (!M.m_Reviewed[t].empty()) { Row.m_Level = "reviewed"; Row.m_Note = M.m_Reviewed[t]; }
            M.m_Details.m_Rows.push_back(std::move(Row));
        }
        return {};
    }
}

#endif
