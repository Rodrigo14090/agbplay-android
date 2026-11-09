#include "ProjectMRenderer.hpp"
#include <projectM-4/projectM.h>
#include <stdexcept>
#include <utility>

ProjectMRenderer::~ProjectMRenderer() { Destroy(); }
void ProjectMRenderer::Create()
{
    Destroy();
    handle = projectm_create();
    if (!handle) throw std::runtime_error("projectM could not create its OpenGL renderer");
    projectm_set_mesh_size(handle, 32, 24);
    projectm_set_preset_locked(handle, true); // UI owns preset selection.
    projectm_set_soft_cut_duration(handle, 1.0);
    projectm_set_aspect_correction(handle, true);
    projectm_set_preset_switch_failed_event_callback(handle,
        [](const char *, const char *message, void *user) {
            static_cast<ProjectMRenderer *>(user)->error = message ? message : "Preset failed";
        }, this);
}
void ProjectMRenderer::Destroy()
{
    if (handle) projectm_destroy(handle);
    handle = nullptr;
    width = height = 0;
}
void ProjectMRenderer::Load(std::string_view preset)
{
    if (!handle) return;
    const std::string text(preset);
    projectm_load_preset_data(handle, text.c_str(), false);
}
void ProjectMRenderer::Render(std::span<const float> stereo, int w, int h, int fps)
{
    if (!handle || w <= 0 || h <= 0) return;
    if (w != width || h != height) {
        projectm_set_window_size(handle, static_cast<size_t>(w), static_cast<size_t>(h));
        width = w; height = h;
    }
    projectm_set_fps(handle, fps);
    projectm_pcm_add_float(handle, stereo.data(), static_cast<unsigned>(stereo.size() / 2), PROJECTM_STEREO);
    projectm_opengl_render_frame(handle);
}
std::string ProjectMRenderer::TakeError() { return std::exchange(error, {}); }
