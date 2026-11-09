#include "ProjectMRenderer.hpp"
#include <projectM-4/projectM.h>
#include <projectM-4/playlist.h>
#include <stdexcept>
#include <utility>

ProjectMRenderer::~ProjectMRenderer() { Destroy(); }
void ProjectMRenderer::Create()
{
    Destroy();
    handle = projectm_create();
    if (!handle) throw std::runtime_error("projectM could not create its OpenGL renderer");
    projectm_set_mesh_size(handle, 32, 24);
    projectm_set_preset_locked(handle, false);
    projectm_set_preset_duration(handle, 10.0);
    projectm_set_easter_egg(handle, 0.0f);
    projectm_set_hard_cut_enabled(handle, false);
    projectm_set_soft_cut_duration(handle, 2.7);
    projectm_set_aspect_correction(handle, true);
    playlist = projectm_playlist_create(handle);
    if (!playlist) throw std::runtime_error("projectM could not create its playlist");
    projectm_playlist_set_shuffle(playlist, true);
    projectm_playlist_set_retry_count(playlist, 0);
    // projectM's built-in shuffle may pick the current entry again. Keep its
    // playlist API and failure handling but ensure an automatic change is visible.
    projectm_set_preset_switch_requested_event_callback(handle,
        [](bool, void *user) {
            auto *self = static_cast<ProjectMRenderer *>(user);
            const auto count = projectm_playlist_size(self->playlist);
            if (!count) return;
            const auto current = projectm_playlist_get_position(self->playlist);
            if (count == 1) { projectm_playlist_set_position(self->playlist, 0, false); return; }
            std::uniform_int_distribution<unsigned> distribution(0, count - 2);
            auto next = distribution(self->random);
            if (next >= current) ++next;
            projectm_playlist_set_position(self->playlist, next, false);
        }, this);
    projectm_playlist_set_preset_switched_event_callback(playlist,
        [](bool, unsigned, void *user) { ++static_cast<ProjectMRenderer *>(user)->changes; }, this);
    projectm_playlist_set_preset_switch_failed_event_callback(playlist,
        [](const char *, const char *message, void *user) {
            static_cast<ProjectMRenderer *>(user)->error = message ? message : "Preset failed";
        }, this);
}
void ProjectMRenderer::Destroy()
{
    if (playlist) projectm_playlist_destroy(playlist);
    playlist = nullptr;
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
void ProjectMRenderer::SetFiles(const std::vector<std::string> &paths, const std::string &textures)
{
    if (!playlist) return;
    projectm_playlist_clear(playlist);
    projectm_playlist_set_retry_count(playlist, paths.empty() ? 0 : 3);
    for (const auto &path : paths) projectm_playlist_add_preset(playlist, path.c_str(), true);
    const char *texturePath = textures.c_str();
    projectm_set_texture_search_paths(handle, textures.empty() ? nullptr : &texturePath, textures.empty() ? 0 : 1);
    if (!paths.empty()) projectm_playlist_play_next(playlist, true);
}
void ProjectMRenderer::Select(unsigned index)
{
    if (playlist && index < projectm_playlist_size(playlist)) projectm_playlist_set_position(playlist, index, false);
}
void ProjectMRenderer::Next(bool previous)
{
    if (!playlist || !projectm_playlist_size(playlist)) return;
    if (previous) projectm_playlist_play_previous(playlist, false);
    else projectm_playlist_play_next(playlist, false);
}
unsigned ProjectMRenderer::Current() const { return playlist ? projectm_playlist_get_position(playlist) : 0; }
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
