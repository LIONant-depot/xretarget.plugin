#ifndef XRETARGET_EDITOR_H
#define XRETARGET_EDITOR_H
#pragma once

// The Retarget resource editor: the descriptor (source and destination skeletons, travel policy, registration, the user's overrides), the BONE MAPPING and the two
// skeletons side by side in the shared skeleton viewport (the skeleton editor's own rendering).
// How the user maps the bones: the Mapping panel lists every DESTINATION bone with the SOURCE bone that drives it. The mapping is made automatically from the names and the
// hierarchy and shown at once (no compile needed); to correct a row the user opens its Source box and picks another source bone, "(none)" (the bone stays at rest) or
// "(automatic)" (takes the correction back). Each correction is an override of the descriptor, by bone name, undoable like any property edit. Hovering a row lights the
// two bones and draws a line between them in the viewport; clicking a row keeps them lit. "Show all links" draws every pair.
// In the viewport (the quick way): hover a bone of either skeleton to see its name, the bones it is linked to (lit, with a line) and its row in the list; press on a bone of one
// skeleton and drag onto a bone of the other to map the two (a line follows the mouse; release on the bone). Smart mirror (on by default) does the mirrored pair too: map a left
// bone and its right mirror is mapped to the mirror of the source (found by name: CC_Base_L_Thigh <-> CC_Base_R_Thigh).
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "source/Tools/Editor/xeditor_camera_toolbar.h"
#include "dependencies/xeditor/include/xeditor/hint.h"
#include "dependencies/xeditor/include/xeditor/commands.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_inspector_pickers.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_resources.h"
#include "plugins/xskeleton.plugin/source/Editor/xskeleton_viewport.h"
#include "plugins/xretarget.plugin/source/xretarget_descriptor.h"
#include "plugins/xretarget.plugin/source/xretarget_mapping.h"
#include "dependencies/xAnimation/source/retarget/xanimation_mirror.h"
#include "dependencies/xGPU/source/Tools/editors/xgpu_editor_anim_pose.h"

namespace xretarget_editor
{
    struct session : xeditor::descriptor_editor
    {
        using desc = xretarget_desc::descriptor;

        xskeleton_editor::skeleton_viewport             m_Viewport;
        xeditor::camera_toolbar                         m_CameraToolbar;
        xeditor::camera_cmds                            m_CameraCmds;
        xrsc::skeleton                                  m_SourceRef, m_DestinationRef;
        xretarget_desc::details                         m_Details;              // what the compiler found, last time it ran
        std::string                                     m_ErrorMessage;         // why nothing can be shown, when so
        bool                                            m_bShowable = false;    // both skeletons are loaded
        bool                                            m_bReady = false;
        std::vector<xskeleton_editor::bone_world>       m_SourceBones, m_DestinationBones;
        xmath::fvec3                                    m_DestinationOffset = {};
        // the editing modes (the row above the viewport) and the placement of the two skeletons (saved in the descriptor, applied to what is drawn)
        enum class mode { map, translate, rotate, scale };
        mode                                            m_Mode = mode::map;
        std::vector<xskeleton_editor::bone_world>       m_SourceWorld, m_DestinationWorld;          // the rest bones as placed
        std::vector<int>                                m_SourceFrom, m_DestinationFrom;            // each bone's wedge starts at this bone (-1: none)
        xretarget_desc::view_place                      m_Place[2];                                 // the working placement while a drag is on
        bool                                            m_bPlaceDrag = false; int m_PlaceWho = 0, m_PlaceSel = -1;
        xretarget_desc::view_place                      m_PlaceStart;
        ImVec2                                          m_PlaceMouse0 = {}; xmath::fvec3 m_PlaceHit0 = {};
        float                                           m_SourceRadius = 0, m_DestinationRadius = 0;      // how big each skeleton is (a unit mismatch shows here)
        std::uint64_t                                   m_LoadedSource = 0, m_LoadedDestination = 0;     // the skeletons the viewport shows: an edit of either reloads it

        // the live mapping (made here, from the descriptor as it is now: no compile needed)
        xretarget_desc::mapping                         m_Live;
        bool                                            m_bLive = false;
        std::string                                     m_LiveSignature;
        ImGuiTextFilter                                 m_DestinationFilter, m_SourceFilter;
        int                                             m_iHover = -1, m_iSelected = -1;               // destination bone indices: the row under the mouse, the row clicked
        bool                                            m_bShowAllLinks = false, m_bOnlyUnmapped = false;
        std::set<int>                                   m_SelSource, m_SelDestination;
        int                                             m_ViewHoverSource = -1, m_ViewHoverDestination = -1;     // the bones under the mouse in the viewport (the list lights their rows)
        bool                                            m_bDragging = false, m_bDragFromSource = true;           // a mapping line being dragged from a bone of one skeleton to a bone of the other
        int                                             m_DragBone = -1;
        bool                                            m_bSmartMirror = true;
        std::string                                     m_Notice; double m_NoticeUntil = 0;                      // what the last action did (the mirror too)
        int                                             m_ScrollRow = -1, m_LastScrolledRow = -1;                // the list follows the viewport's hover

        session(xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) noexcept
            : descriptor_editor("Retarget", Guid, LibraryGuid, pDevice)
            , m_CameraCmds(m_Undo, m_Viewport.m_Scene.Camera())
        {
            // Reading the descriptor also reads both skeletons' bone manifests: not having them yet (not compiled) does not make the descriptor unusable
            m_Document.m_TolerateReadError = [](const xresource_pipeline::descriptor::base&) { return true; };
            m_Document.Load();
            BindDescriptorInspector();
            xresource_editor::WireResourcePickerCallbacks(m_DescriptorInspector.m_Inspector);
            LoadDetails();

            AddPanel("Mapping",             dock::right,  [this] { RenderMapping(); });
            if (pDevice) m_CameraToolbar.Init(m_Document.m_Guid.m_Instance.m_Value);
            AddPanel("Retarget Viewport",   dock::center, [this] { RenderViewport(); }, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            AddPanel("Description",         dock::left,   [this] { m_DescriptorInspector.Show(); });

            m_Viewport.m_pOpen = &m_bOpen;
            m_bReady = pDevice && m_Viewport.Init(*pDevice);
            Reload();
        }

        ~session() noexcept override
        {
            xresource::g_Mgr.ReleaseRef(m_SourceRef);
            xresource::g_Mgr.ReleaseRef(m_DestinationRef);
            m_Viewport.Release();
        }

        desc* Desc() noexcept { return m_Document.isLoaded() ? static_cast<desc*>(m_Document.m_pDescriptor.get()) : nullptr; }

        void LoadDetails() noexcept
        {
            m_Details = {};
            xtextfile::stream File;
            if (auto Err = File.Open(true, m_Document.m_LogPath + L"\\Details.txt", {}); Err) return;
            xproperty::settings::context Context;
            if (auto Err = xproperty::sprop::serializer::Stream(File, m_Details, Context); Err) m_Details = {};
        }

        void OnDescriptorReplaced() noexcept override { m_LiveSignature.clear(); Reload(); }
        void OnCompileStarted()     noexcept override { LetGo(); }
        void OnCompiled()           noexcept override { LoadDetails(); m_LiveSignature.clear(); m_ManifestSource = m_ManifestDestination = ~0ull; Reload(); }

        void LetGo() noexcept
        {
            xresource::g_Mgr.ReleaseRef(m_SourceRef); xresource::g_Mgr.ReleaseRef(m_DestinationRef);
            m_SourceRef.clear(); m_DestinationRef.clear();
            m_bShowable = false;
        }

        // One skeleton's rest pose as bone worlds, drawn at a common size: its feet on the ground, centred over the origin, scaled so its radius is 1
        // (two rigs often come in different units: a Daz character in centimetres beside a metre one). Real is its size in world units, for the numbers.
        static void RestBones(const xskeleton::skeleton& S, std::vector<xskeleton_editor::bone_world>& Out, float& Real) noexcept
        {
            std::vector<xmath::fmat4> Worlds;
            xgpu::tools::editors::ComputeRestBoneWorlds(S, Worlds);
            Out.resize(Worlds.size());
            xmath::fvec3 Center(0, 0, 0); float MinY = 1e30f;
            for (std::size_t i = 0; i < Worlds.size(); ++i) { Out[i].m_Position = Worlds[i].ExtractPosition(); Out[i].m_Right = Worlds[i].Right(); Out[i].m_Up = Worlds[i].Up(); Center += Out[i].m_Position; MinY = std::min(MinY, Out[i].m_Position.m_Y); }
            if (!Worlds.empty()) Center /= float(Worlds.size());
            float Radius = 1e-4f;
            for (auto& B : Out) Radius = std::max(Radius, (B.m_Position - Center).Length());
            Real = Radius;
            for (auto& B : Out) B.m_Position = xmath::fvec3((B.m_Position.m_X - Center.m_X) / Radius, (B.m_Position.m_Y - MinY) / Radius, (B.m_Position.m_Z - Center.m_Z) / Radius);
        }

        void Reload() noexcept
        {
            LetGo();
            m_ErrorMessage.clear();
            auto* pDesc = Desc();
            if (!pDesc) { m_ErrorMessage = "The descriptor could not be read."; return; }
            if (pDesc->m_Source.empty() || pDesc->m_Destination.empty()) { m_ErrorMessage = "Set the Source and the Destination skeleton in the Description panel."; return; }

            m_LoadedSource = pDesc->m_Source.m_Instance.m_Value; m_LoadedDestination = pDesc->m_Destination.m_Instance.m_Value;
            m_SourceRef.m_Instance = pDesc->m_Source.m_Instance;
            m_DestinationRef.m_Instance = pDesc->m_Destination.m_Instance;
            auto* pSource      = xresource::g_Mgr.getResource(m_SourceRef);
            auto* pDestination = xresource::g_Mgr.getResource(m_DestinationRef);
            if (!pSource || !pDestination) { LetGo(); m_ErrorMessage = "A skeleton could not be loaded (compile it first)."; return; }

            float RS = 0, RD = 0;
            RestBones(*pSource, m_SourceBones, RS);
            RestBones(*pDestination, m_DestinationBones, RD);
            m_SourceRadius = RS; m_DestinationRadius = RD;
            m_SourceFrom      = xskeleton_editor::DrawParents(*pSource,      xskeleton_editor::JointBones(*pSource));
            m_DestinationFrom = xskeleton_editor::DrawParents(*pDestination, xskeleton_editor::JointBones(*pDestination));
            m_DestinationOffset = xmath::fvec3(2.6f, 0.0f, 0.0f);                       // the destination stands to the right of the source, both of radius 1
            m_Viewport.m_Scene.m_Center   = xmath::fvec3(1.3f, 0.9f, 0.0f);
            m_Viewport.m_Scene.m_Radius   = 2.0f;
            m_Viewport.m_Scene.m_bReframe = true;
            m_bShowable = true;
        }

        //----------------------------------------------------------------------------------------
        // The live mapping and the corrections
        //----------------------------------------------------------------------------------------

        // What the mapping depends on: remade when it changes (an edit of the Description, a correction, an undo)
        static std::string Signature(const desc& D)
        {
            std::string S = std::format("{}|{}|{}|{:.4f},{:.4f},{:.4f}|", D.m_Source.m_Instance.m_Value, D.m_Destination.m_Instance.m_Value, int(D.m_Travel), D.m_RegistrationDeg.m_X, D.m_RegistrationDeg.m_Y, D.m_RegistrationDeg.m_Z);
            for (const auto& O : D.m_Overrides) { S += O.m_Target; S += '>'; S += O.m_Source; S += O.m_bUnmap ? "!" : "."; S += ';'; }
            S += std::format("{}|{}", D.m_ResolvedSource.m_Bones.size(), D.m_ResolvedDestination.m_Bones.size());
            return S;
        }

        std::uint64_t m_ManifestSource = ~0ull, m_ManifestDestination = ~0ull;
        void RefreshLive() noexcept
        {
            auto* pDesc = Desc();
            if (!pDesc) { m_bLive = false; return; }
            // the bone lists are read when the file is loaded: a skeleton picked since (or compiled since) is read here
            auto Read = [&](auto& Out, const auto& Ref, std::uint64_t& Seen)
            {
                if (Ref.m_Instance.m_Value == Seen) return;
                Seen = Ref.m_Instance.m_Value; Out = {};
                if (!Ref.empty()) if (auto Err = desc::ReadManifest(Out, Ref, m_Document.m_DescriptorPath); Err) Out = {};
            };
            Read(pDesc->m_ResolvedSource, pDesc->m_Source, m_ManifestSource); Read(pDesc->m_ResolvedDestination, pDesc->m_Destination, m_ManifestDestination);
            const auto Sig = Signature(*pDesc);
            if (Sig == m_LiveSignature) return;
            m_LiveSignature = Sig;
            m_Live = {};
            m_bLive = !pDesc->m_ResolvedSource.m_Bones.empty() && !pDesc->m_ResolvedDestination.m_Bones.empty() && !xretarget_desc::MakeMapping(*pDesc, m_Live);
        }

        // A correction: Target bone -> a Source bone, "(none)" (Unmap), or back to automatic (the override removed)
        enum class pick { source, none, automatic };
        void Correct(const std::string& Target, const std::string& Source, pick Pick) noexcept
        {
            auto* pDesc = Desc();
            if (!pDesc) return;
            auto Path = [](int i, const char* Field) { return std::format("Retarget/Overrides[G:{}]/{}", i, Field); };
            auto Set  = [&](const std::string& P, const std::string& V, const std::string& Before) { xeditor::Run(m_Undo, std::format("SetProperty -Path {} -Value {} -Before {}", xeditor::Quote(P), xeditor::Quote(V), xeditor::Quote(Before))); };
            auto& Os = pDesc->m_Overrides;
            int iFound = -1; for (int i = 0; i < static_cast<int>(Os.size()); ++i) if (Os[i].m_Target == Target) { iFound = i; break; }
            if (Pick == pick::automatic)
            {
                if (iFound < 0) return;
                const int iLast = static_cast<int>(Os.size()) - 1;
                if (iFound != iLast)       // the last one takes its place, then the list is one shorter
                {
                    const auto L = Os[iLast]; const auto F = Os[iFound];
                    Set(Path(iFound, "Target"), L.m_Target, F.m_Target);
                    Set(Path(iFound, "Source"), L.m_Source, F.m_Source);
                    Set(Path(iFound, "Unmap"), L.m_bUnmap ? "true" : "false", F.m_bUnmap ? "true" : "false");
                }
                Set("Retarget/Overrides[]", std::to_string(iLast), std::to_string(iLast + 1));
                m_LiveSignature.clear();
                return;
            }
            const bool bUnmap = Pick == pick::none;
            if (iFound < 0)
            {
                iFound = static_cast<int>(Os.size());
                Set("Retarget/Overrides[]", std::to_string(iFound + 1), std::to_string(iFound));
                Set(Path(iFound, "Target"), Target, "");
            }
            const auto Old = pDesc->m_Overrides[iFound];
            if (Old.m_Source != Source) Set(Path(iFound, "Source"), Source, Old.m_Source);
            if (Old.m_bUnmap != bUnmap) Set(Path(iFound, "Unmap"), bUnmap ? "true" : "false", Old.m_bUnmap ? "true" : "false");
            m_LiveSignature.clear();
        }

        // The correction of destination bone T (S: a source bone, or -1 for none) with smart mirror: the mirrored destination bone takes the mirrored source bone
        void Apply(int T, int S, pick Pick) noexcept
        {
            if (!m_bLive || T < 0 || T >= static_cast<int>(m_Live.m_Destination.m_Names.size())) return;
            const std::string Target(m_Live.m_Destination.m_Names[T]);
            const std::string Source = S >= 0 ? std::string(m_Live.m_Source.m_Names[S]) : std::string();
            Correct(Target, Source, Pick);
            std::string What = Pick == pick::source ? Target + " <- " + Source : Pick == pick::none ? Target + " stays at rest" : Target + " back to automatic";
            if (m_bSmartMirror)
            {
                const int mT = xanimation::FindMirrorBone(m_Live.m_Destination.m_Names, T);
                const int mS = S >= 0 ? xanimation::FindMirrorBone(m_Live.m_Source.m_Names, S) : -1;
                if (mT >= 0 && (Pick != pick::source || mS >= 0))
                {
                    const std::string MT(m_Live.m_Destination.m_Names[mT]);
                    const std::string MS = mS >= 0 ? std::string(m_Live.m_Source.m_Names[mS]) : std::string();
                    Correct(MT, MS, Pick);
                    What += Pick == pick::source ? "; mirrored: " + MT + " <- " + MS : "; mirrored: " + MT;
                }
                else if (Pick == pick::source && mT >= 0) What += "; (no mirror found for the source bone)";
            }
            m_Notice = What; m_NoticeUntil = ImGui::GetTime() + 6.0;
        }

        bool Overridden(const std::string& Target) noexcept
        {
            if (auto* pDesc = Desc()) for (const auto& O : pDesc->m_Overrides) if (O.m_Target == Target) return true;
            return false;
        }

        //----------------------------------------------------------------------------------------
        // Panels
        //----------------------------------------------------------------------------------------

        // The placed bones of one skeleton: its rest bones turned about the vertical axis, scaled, and moved (from its default place)
        void PlaceBones(int Who, const std::vector<xskeleton_editor::bone_world>& Rest, std::vector<xskeleton_editor::bone_world>& Out) const noexcept
        {
            const auto& V = Who == 0 ? Desc2()->m_SourceView : Desc2()->m_DestinationView;
            const auto& W = m_bPlaceDrag ? m_Place[Who] : V;
            const float Rad = W.m_YawDeg * 3.14159265f / 180.0f, C = std::cos(Rad), S = std::sin(Rad);
            const xmath::fvec3 Base = Who == 0 ? xmath::fvec3(0, 0, 0) : m_DestinationOffset;
            auto Turn = [&](const xmath::fvec3& P) { return xmath::fvec3(C * P.m_X + S * P.m_Z, P.m_Y, -S * P.m_X + C * P.m_Z); };
            Out = Rest;
            for (auto& B : Out) { B.m_Position = Base + W.m_Move + Turn(B.m_Position * W.m_Scale); B.m_Right = Turn(B.m_Right); B.m_Up = Turn(B.m_Up); }
        }
        const desc* Desc2() const noexcept { return m_Document.isLoaded() ? static_cast<const desc*>(m_Document.m_pDescriptor.get()) : nullptr; }

        // The middle of a bone as drawn: the wedge runs from the bone toward its child (the longest one when it has several), so the links attach halfway along it
        xmath::fvec3 Mid(bool bSource, int i) const noexcept
        {
            const auto& W = bSource ? m_SourceWorld : m_DestinationWorld; const auto& F = bSource ? m_SourceFrom : m_DestinationFrom;
            int Best = -1; float Len = -1;
            for (int c = 0; c < static_cast<int>(F.size()); ++c) if (F[c] == i) { const float L = (W[c].m_Position - W[i].m_Position).Length(); if (L > Len) { Len = L; Best = c; } }
            return Best >= 0 ? (W[i].m_Position + W[Best].m_Position) * 0.5f : W[i].m_Position;
        }

        // Where the mouse ray meets the horizontal plane at height Y
        xmath::fvec3 GroundHit(const ImVec2& Mouse, float Y) noexcept
        {
            const auto O = m_Viewport.m_Scene.m_View.getPosition(); const auto Dir = m_Viewport.m_Scene.m_View.RayFromScreen(Mouse.x, Mouse.y);
            if (std::fabs(Dir.m_Y) < 1e-4f) return O;
            const float T = (Y - O.m_Y) / Dir.m_Y; return O + Dir * T;
        }

        // A placement change committed to the descriptor (undoable): only the fields that moved
        void CommitPlace(int Who) noexcept
        {
            const auto& Now = m_Place[Who]; const auto& Was = m_PlaceStart;
            const char* Root = Who == 0 ? "Retarget/SourceView" : "Retarget/DestinationView";
            auto Set = [&](const std::string& P, float V, float B) { if (V != B) xeditor::Run(m_Undo, std::format("SetProperty -Path {} -Value {} -Before {}", xeditor::Quote(P), xeditor::Quote(std::format("{}", V)), xeditor::Quote(std::format("{}", B)))); };
            Set(std::format("{}/Move/X", Root), Now.m_Move.m_X, Was.m_Move.m_X); Set(std::format("{}/Move/Y", Root), Now.m_Move.m_Y, Was.m_Move.m_Y); Set(std::format("{}/Move/Z", Root), Now.m_Move.m_Z, Was.m_Move.m_Z);
            Set(std::format("{}/YawDeg", Root), Now.m_YawDeg, Was.m_YawDeg); Set(std::format("{}/Scale", Root), Now.m_Scale, Was.m_Scale);
        }

        void RenderViewport() noexcept
        {
            // the modes: what the left mouse button does in the viewport
            {
                static const char* Names[] = { "Map", "Translate", "Rotate", "Scale" };
                static const char* Tips[]  = { "Drag from a bone of one skeleton onto a bone of the other to map them", "Drag a skeleton to move it (hold Shift to lift it)", "Drag a skeleton sideways to turn it", "Drag a skeleton up or down to make it smaller or bigger" };
                for (int i = 0; i < 4; ++i)
                {
                    if (i) ImGui::SameLine();
                    if (ImGui::Selectable(Names[i], m_Mode == static_cast<mode>(i), 0, ImVec2(70, 0))) { m_Mode = static_cast<mode>(i); m_bDragging = false; m_bPlaceDrag = false; }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Tips[i]);
                }
                ImGui::SameLine(); ImGui::TextDisabled("(how the skeletons are placed here is saved with the resource; it changes nothing that is compiled)");
            }
            if (auto* pDesc = Desc(); pDesc && (pDesc->m_Source.m_Instance.m_Value != m_LoadedSource || pDesc->m_Destination.m_Instance.m_Value != m_LoadedDestination)) Reload();
            m_CameraToolbar.Render(ImGui::GetContentRegionAvail(), m_Viewport.m_Scene.m_State, [this] { RenderViewportBody(); });
        }

        void RenderViewportBody() noexcept
        {
            auto* pHost   = xeditor::host::current();
            auto* pWindow = pHost ? pHost->find<xgpu::window>() : nullptr;
            if (!m_bReady || !pWindow) { ImGui::TextDisabled("The 3D view needs a GPU device (open from the editor)."); return; }
            if (!m_bShowable) { ImGui::TextWrapped("%s", m_ErrorMessage.empty() ? "Nothing to show." : m_ErrorMessage.c_str()); return; }
            auto* pSource      = xresource::g_Mgr.getResource(m_SourceRef);
            auto* pDestination = xresource::g_Mgr.getResource(m_DestinationRef);
            if (!pSource || !pDestination) return;

            RefreshLive();
            if (!Desc2()) return;
            PlaceBones(0, m_SourceBones, m_SourceWorld); PlaceBones(1, m_DestinationBones, m_DestinationWorld);
            xskeleton_editor::viewport_item Items[2] =
            { { .m_pSkeleton = pSource,      .m_Bones = m_SourceWorld,      .m_NormalColor = IM_COL32(255, 200, 120, 255), .m_pSelected = &m_SelSource }                                          // source: warm
            , { .m_pSkeleton = pDestination, .m_Bones = m_DestinationWorld, .m_NormalColor = IM_COL32(150, 220, 255, 255), .m_pSelected = &m_SelDestination } };   // destination: cool

            // the bone under the mouse (last frame's view: the pick is one frame behind, which no hand notices). The nearer of the two skeletons wins.
            const ImVec2 Mouse = ImGui::GetMousePos();
            int iS = -1, iD = -1;
            if (m_bLive && m_Viewport.Over() && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                float TS = 0, TD = 0;
                m_Viewport.PickBone(Items[0], Mouse, iS, TS); m_Viewport.PickBone(Items[1], Mouse, iD, TD);
                if (iS >= 0 && iD >= 0) { if (TS < TD) iD = -1; else iS = -1; }
            }
            m_ViewHoverSource = iS; m_ViewHoverDestination = iD;

            // what is lit: the clicked row's pair; the hovered row's pair; the pair(s) of the hovered bone (a source bone may drive several destination bones)
            m_SelSource.clear(); m_SelDestination.clear();
            int iHoverSource = -1, iHoverDestination = -1;
            const auto& Map = m_Live.m_Auto.m_Map.m_SourceOf;
            std::vector<int> Linked;        // the destination bones whose line is drawn bright
            auto LightDest = [&](int T, bool bHover)
            {
                if (T < 0 || T >= static_cast<int>(m_DestinationBones.size()) || !m_bLive) return;
                const int S = Map[T];
                if (bHover) { iHoverDestination = T; iHoverSource = S; Linked.push_back(T); }
                m_SelDestination.insert(T); if (S >= 0) m_SelSource.insert(S);
            };
            LightDest(m_iSelected, false);
            if (m_bDragging) { /* while dragging only the dragged bone and the one under the mouse are lit (below) */ }
            else if (m_iHover >= 0) LightDest(m_iHover, true);
            else if (iD >= 0) LightDest(iD, true);
            else if (iS >= 0 && m_bLive)
            {
                iHoverSource = iS; m_SelSource.insert(iS);
                for (int T = 0; T < static_cast<int>(Map.size()); ++T) if (Map[T] == iS) { m_SelDestination.insert(T); Linked.push_back(T); if (iHoverDestination < 0) iHoverDestination = T; }
            }
            if (m_bDragging)
            {
                (m_bDragFromSource ? iHoverSource : iHoverDestination) = m_DragBone;
                (m_bDragFromSource ? m_SelSource : m_SelDestination).insert(m_DragBone);
                if (m_bDragFromSource && iD >= 0) { iHoverDestination = iD; m_SelDestination.insert(iD); }
                if (!m_bDragFromSource && iS >= 0) { iHoverSource = iS; m_SelSource.insert(iS); }
            }
            Items[0].m_Hovered = iHoverSource; Items[1].m_Hovered = iHoverDestination;
            m_Viewport.Render(Items);

            // the list follows the viewport: the row of the hovered bone (the first of them) is scrolled to once
            m_ScrollRow = -1;
            if (m_bLive && !m_bDragging)
            {
                if (iD >= 0) m_ScrollRow = iD;
                else if (iS >= 0) for (int T = 0; T < static_cast<int>(Map.size()); ++T) if (Map[T] == iS) { m_ScrollRow = T; break; }
            }

            if (m_bLive)
            {
                auto* pDraw = ImGui::GetWindowDrawList();
                auto Link = [&](int T, ImU32 Color, float Thick)
                {
                    const int S = Map[T];
                    if (S < 0 || S >= static_cast<int>(m_SourceBones.size())) return;
                    ImVec2 A, B;
                    if (m_Viewport.Project(Mid(true, S), A) && m_Viewport.Project(Mid(false, T), B)) pDraw->AddLine(A, B, Color, Thick);
                };
                if (m_bShowAllLinks) for (int T = 0; T < static_cast<int>(m_DestinationBones.size()); ++T) Link(T, IM_COL32(255, 255, 255, 50), 1.0f);
                if (m_iSelected >= 0) Link(m_iSelected, IM_COL32(120, 255, 160, 230), 2.0f);
                for (const int T : Linked) Link(T, IM_COL32(255, 255, 255, 255), 2.5f);

                // drag and drop: press on a bone of one skeleton, release on a bone of the other
                if (m_Mode == mode::map && !m_bDragging && m_Viewport.Over() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (iS >= 0 || iD >= 0))
                { m_bDragging = true; m_bDragFromSource = iS >= 0; m_DragBone = iS >= 0 ? iS : iD; }
                if (m_bDragging)
                {
                    ImVec2 From;
                    const xmath::fvec3 P = Mid(m_bDragFromSource, m_DragBone);
                    const int iOther = m_bDragFromSource ? iD : iS;          // the bone of the OTHER skeleton under the mouse
                    if (m_Viewport.Project(P, From))
                    {
                        const ImU32 Color = iOther >= 0 ? IM_COL32(120, 255, 160, 255) : IM_COL32(255, 220, 80, 255);
                        pDraw->AddLine(From, Mouse, Color, 3.0f); pDraw->AddCircleFilled(From, 5.0f, Color); pDraw->AddCircleFilled(Mouse, 4.0f, Color);
                    }
                    if (iOther >= 0)
                    {
                        const std::string_view Other = m_bDragFromSource ? m_Live.m_Destination.m_Names[iOther] : m_Live.m_Source.m_Names[iOther];
                        const std::string_view Mine  = m_bDragFromSource ? m_Live.m_Source.m_Names[m_DragBone] : m_Live.m_Destination.m_Names[m_DragBone];
                        ImGui::SetTooltip("release: %.*s drives %.*s", int(m_bDragFromSource ? Mine.size() : Other.size()), m_bDragFromSource ? Mine.data() : Other.data(), int(m_bDragFromSource ? Other.size() : Mine.size()), m_bDragFromSource ? Other.data() : Mine.data());
                    }
                    else ImGui::SetTooltip("drop on a bone of the %s skeleton", m_bDragFromSource ? "destination" : "source");
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        if (iOther >= 0) Apply(m_bDragFromSource ? iOther : m_DragBone, m_bDragFromSource ? m_DragBone : iOther, pick::source);
                        m_bDragging = false; m_DragBone = -1;
                    }
                }
                else if (iS >= 0 || iD >= 0)      // the name of the bone under the mouse, and what it is linked to
                {
                    ImGui::BeginTooltip();
                    if (iS >= 0)
                    {
                        const auto N = m_Live.m_Source.m_Names[iS];
                        ImGui::Text("source bone: %.*s", int(N.size()), N.data());
                        int n = 0; for (int T = 0; T < static_cast<int>(Map.size()); ++T) if (Map[T] == iS) { const auto D = m_Live.m_Destination.m_Names[T]; ImGui::TextDisabled("drives %.*s", int(D.size()), D.data()); ++n; }
                        if (!n) ImGui::TextDisabled("drives nothing: drag it onto a destination bone");
                    }
                    else
                    {
                        const auto N = m_Live.m_Destination.m_Names[iD];
                        ImGui::Text("destination bone: %.*s", int(N.size()), N.data());
                        if (Map[iD] >= 0) { const auto S = m_Live.m_Source.m_Names[Map[iD]]; ImGui::TextDisabled("driven by %.*s", int(S.size()), S.data()); }
                        else ImGui::TextDisabled("not driven (stays at rest): drag a source bone onto it");
                    }
                    ImGui::EndTooltip();
                }
            }
            if (m_bDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_bDragging = false; m_DragBone = -1; }

            // translate / rotate / scale: press on a skeleton (or anywhere, with one already chosen) and drag
            if (m_Mode != mode::map)
            {
                if (m_Viewport.Over() && !m_bPlaceDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (iS >= 0 || iD >= 0) m_PlaceSel = iS >= 0 ? 0 : 1;
                    if (m_PlaceSel >= 0)
                    {
                        const auto* pD = Desc2();
                        m_bPlaceDrag = true; m_PlaceWho = m_PlaceSel; m_Place[0] = pD->m_SourceView; m_Place[1] = pD->m_DestinationView;
                        m_PlaceStart = m_Place[m_PlaceWho]; m_PlaceMouse0 = Mouse; m_PlaceHit0 = GroundHit(Mouse, 0.0f);
                    }
                }
                if (m_bPlaceDrag)
                {
                    auto& W = m_Place[m_PlaceWho];
                    if (m_Mode == mode::translate)
                    {
                        if (ImGui::GetIO().KeyShift) W.m_Move.m_Y = m_PlaceStart.m_Move.m_Y - (Mouse.y - m_PlaceMouse0.y) * 0.005f * m_Viewport.m_Scene.m_Radius;
                        else { const auto H = GroundHit(Mouse, 0.0f); W.m_Move.m_X = m_PlaceStart.m_Move.m_X + (H.m_X - m_PlaceHit0.m_X); W.m_Move.m_Z = m_PlaceStart.m_Move.m_Z + (H.m_Z - m_PlaceHit0.m_Z); }
                    }
                    else if (m_Mode == mode::rotate) W.m_YawDeg = m_PlaceStart.m_YawDeg + (Mouse.x - m_PlaceMouse0.x) * 0.5f;
                    else W.m_Scale = std::max(0.05f, m_PlaceStart.m_Scale * std::exp(-(Mouse.y - m_PlaceMouse0.y) * 0.005f));
                    ImGui::SetTooltip("%s: move %.2f, %.2f, %.2f  turn %.1f deg  scale %.2f", m_PlaceWho ? "destination" : "source", W.m_Move.m_X, W.m_Move.m_Y, W.m_Move.m_Z, W.m_YawDeg, W.m_Scale);
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { CommitPlace(m_PlaceWho); m_bPlaceDrag = false; }
                }
                else if (iS >= 0 || iD >= 0) ImGui::SetTooltip("%s skeleton: press and drag to %s it", iS >= 0 ? "source" : "destination", m_Mode == mode::translate ? "move" : m_Mode == mode::rotate ? "turn" : "scale");
            }
        }

        // One destination bone's row: its name, the Source box (the correction), how it was decided
        void RenderRow(int T) noexcept
        {
            const auto& Row = m_Live.m_Details.m_Rows[T];
            const int   S   = m_Live.m_Auto.m_Map.m_SourceOf[T];
            const std::string Target = Row.m_Target;
            ImGui::TableNextRow();
            ImGui::PushID(T);
            if (T == m_ViewHoverDestination || (m_ViewHoverSource >= 0 && S == m_ViewHoverSource)) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(90, 140, 210, 110));       // the viewport's hover, lit in the list
            if (T == m_ScrollRow && m_ScrollRow != m_LastScrolledRow) { ImGui::SetScrollHereY(0.5f); m_LastScrolledRow = m_ScrollRow; }
            ImGui::TableNextColumn();
            if (ImGui::Selectable(Target.c_str(), m_iSelected == T, ImGuiSelectableFlags_AllowOverlap)) m_iSelected = (m_iSelected == T) ? -1 : T;
            if (ImGui::IsItemHovered()) m_iHover = T;

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const std::string Shown = S >= 0 ? std::string(m_Live.m_Source.m_Names[S]) : std::string("(none: stays at rest)");
            if (ImGui::BeginCombo("##src", Shown.c_str()))
            {
                m_SourceFilter.Draw("filter", 160.0f);
                if (ImGui::Selectable("(none: stays at rest)", S < 0)) Apply(T, -1, pick::none);
                if (Overridden(Target) && ImGui::Selectable("(automatic)")) Apply(T, -1, pick::automatic);
                ImGui::Separator();
                for (int s = 0; s < static_cast<int>(m_Live.m_Source.m_Names.size()); ++s)
                {
                    const std::string Name(m_Live.m_Source.m_Names[s]);
                    if (!m_SourceFilter.PassFilter(Name.c_str())) continue;
                    if (ImGui::Selectable(Name.c_str(), s == S)) Apply(T, s, pick::source);
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) m_iHover = T;

            ImGui::TableNextColumn(); ImGui::TextUnformatted(Row.m_Slot.c_str()); if (!Row.m_Side.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", Row.m_Side.c_str()); }
            ImGui::TableNextColumn();
            if (Row.m_Level == "reviewed") ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "yours");
            else if (!Row.m_Level.empty()) ImGui::TextDisabled("%s", Row.m_Level.c_str());
            ImGui::PopID();
        }

        void RenderMapping() noexcept
        {
            m_iHover = -1;
            if (m_ScrollRow < 0) m_LastScrolledRow = -1;
            auto* pDesc = Desc();
            if (!pDesc) { ImGui::TextWrapped("The descriptor could not be read."); return; }
            RefreshLive();
            if (!m_bLive)
            {
                if (pDesc->m_Source.empty() || pDesc->m_Destination.empty()) ImGui::TextWrapped("%s", "Pick the Source and the Destination skeleton in the Description panel: the bones are mapped as soon as both are set (and compiled).");
                else ImGui::TextWrapped("%s", "The two skeletons must be compiled before their bones can be mapped.");
                return;
            }
            const auto& D = m_Live.m_Details;
            ImGui::Text("%d of %d destination bones driven, %s", D.m_nMapped, D.m_nMapped + D.m_nUnmapped, D.m_bUsable ? "usable" : "NOT usable (no hips or no limb)");
            ImGui::TextDisabled("real size (radius): source %.3f, destination %.3f; hip scale %.3f; drawn at the same size", m_SourceRadius, m_DestinationRadius, D.m_HipScale);
            for (const auto& W : D.m_Warnings) ImGui::TextWrapped("! %s", W.c_str());

            ImGui::Separator();
            ImGui::TextWrapped("In the viewport: hover a bone of either skeleton to see its name and its links; press on a bone of one skeleton and drag onto a bone of the other to map them. In the list: hover a row to light its two bones; open its Source box to pick another bone, (none) (the bone stays at rest) or (automatic) (takes your correction back).");
            ImGui::Checkbox("smart mirror", &m_bSmartMirror); ImGui::SameLine(); ImGui::TextDisabled("(the mirrored pair is mapped too: left <-> right, found by name)");
            if (ImGui::GetTime() < m_NoticeUntil) ImGui::TextColored(ImVec4(0.55f, 1.0f, 0.65f, 1.0f), "%s", m_Notice.c_str());
            m_DestinationFilter.Draw("find a destination bone", 220.0f);
            ImGui::SameLine(); ImGui::Checkbox("only unmapped", &m_bOnlyUnmapped);
            ImGui::SameLine(); ImGui::Checkbox("show all links", &m_bShowAllLinks);
            if (!pDesc->m_Overrides.empty())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("take all my corrections back")) xeditor::Run(m_Undo, std::format("SetProperty -Path {} -Value 0 -Before {}", xeditor::Quote("Retarget/Overrides[]"), pDesc->m_Overrides.size()));
                ImGui::SameLine(); ImGui::TextDisabled("(%d yours)", int(pDesc->m_Overrides.size()));
            }

            if (ImGui::BeginTable("##Mapping", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Destination bone", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Driven by (source bone)", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("How", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();
                for (int T = 0; T < static_cast<int>(D.m_Rows.size()); ++T)
                {
                    if (m_bOnlyUnmapped && m_Live.m_Auto.m_Map.m_SourceOf[T] >= 0) continue;
                    if (!m_DestinationFilter.PassFilter(D.m_Rows[T].m_Target.c_str())) continue;
                    RenderRow(T);
                }
                ImGui::EndTable();
            }
        }
    };

    inline const xeditor::auto_register_resource_editor g_Registration
    { xretarget_desc::resource_type_guid_v
    , [](xresource::full_guid Guid, xresource_editor::library::guid LibraryGuid, xgpu::device* pDevice) -> std::unique_ptr<xeditor::resource_editor>
      { return std::make_unique<session>(Guid, LibraryGuid, pDevice); }
    };
}

#endif
