#ifndef XRETARGET_EDITOR_H
#define XRETARGET_EDITOR_H
#pragma once

// The Retarget resource editor (version 0): the descriptor (source and destination skeletons, travel policy, registration, the user's overrides), the
// mapping the compiler found (read only for now: every destination bone, the source bone that drives it, how that was decided, the warnings) and the two
// skeletons side by side in the shared skeleton viewport. Compiling the resource refreshes the mapping.
// Next: pick a row and change its source in the table, hover a row to light both bones, play a clip through the mapping (the animation editor's job).
#include "source/Tools/Editor/xeditor_descriptor_editor.h"
#include "source/Tools/Editor/xeditor_camera_toolbar.h"
#include "dependencies/xeditor/include/xeditor/hint.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_inspector_pickers.h"
#include "dependencies/xresource_pipeline_v2/source/editor/xresource_editor_resources.h"
#include "plugins/xskeleton.plugin/source/Editor/xskeleton_viewport.h"
#include "plugins/xretarget.plugin/source/xretarget_descriptor.h"
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
        float                                           m_SourceRadius = 0, m_DestinationRadius = 0;      // how big each skeleton is (a unit mismatch shows here)
        std::uint64_t                                   m_LoadedSource = 0, m_LoadedDestination = 0;     // the skeletons the viewport shows: an edit of either reloads it

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

        void OnDescriptorReplaced() noexcept override { Reload(); }
        void OnCompileStarted()     noexcept override { LetGo(); }
        void OnCompiled()           noexcept override { LoadDetails(); Reload(); }

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
            m_DestinationOffset = xmath::fvec3(2.6f, 0.0f, 0.0f);                       // the destination stands to the right of the source, both of radius 1
            m_Viewport.m_Scene.m_Center   = xmath::fvec3(1.3f, 0.9f, 0.0f);
            m_Viewport.m_Scene.m_Radius   = 2.0f;
            m_Viewport.m_Scene.m_bReframe = true;
            m_bShowable = true;
        }

        void RenderViewport() noexcept
        {
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
            const xskeleton_editor::viewport_item Items[2] =
            { { .m_pSkeleton = pSource,      .m_Bones = m_SourceBones,      .m_NormalColor = IM_COL32(255, 200, 120, 255) }       // source: warm
            , { .m_pSkeleton = pDestination, .m_Bones = m_DestinationBones, .m_Offset = m_DestinationOffset, .m_NormalColor = IM_COL32(150, 220, 255, 255) } };   // destination: cool
            m_Viewport.Render(Items);
        }

        void RenderMapping() noexcept
        {
            if (m_Details.m_Rows.empty()) { ImGui::TextWrapped("%s", "No mapping yet: set both skeletons and compile the resource."); return; }
            if (m_bShowable) ImGui::Text("real size (radius): source %.3f, destination %.3f (drawn at the same size)", m_SourceRadius, m_DestinationRadius);
            ImGui::Text("%d mapped, %d unmapped (they stay at rest), hip scale %.3f, %s", m_Details.m_nMapped, m_Details.m_nUnmapped, m_Details.m_HipScale, m_Details.m_bUsable ? "usable" : "NOT usable");
            for (const auto& W : m_Details.m_Warnings) ImGui::TextWrapped("! %s", W.c_str());
            if (ImGui::BeginTable("##Mapping", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Destination"); ImGui::TableSetupColumn("Source"); ImGui::TableSetupColumn("Slot"); ImGui::TableSetupColumn("Side"); ImGui::TableSetupColumn("How");
                ImGui::TableHeadersRow();
                for (const auto& R : m_Details.m_Rows)
                {
                    ImGui::TableNextRow();
                    const bool bMapped = !R.m_Source.empty();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(R.m_Target.c_str());
                    ImGui::TableNextColumn(); if (bMapped) ImGui::TextUnformatted(R.m_Source.c_str()); else ImGui::TextDisabled("(stays at rest)");
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(R.m_Slot.c_str());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(R.m_Side.c_str());
                    ImGui::TableNextColumn();
                    if (R.m_Level == "reviewed") ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "%s", R.m_Note.c_str());
                    else if (!R.m_Level.empty()) ImGui::TextDisabled("%s%s%s", R.m_Level.c_str(), R.m_Note.empty() ? "" : " - ", R.m_Note.c_str());
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
