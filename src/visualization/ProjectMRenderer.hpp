#pragma once
#include <span>
#include <string>
#include <string_view>
struct projectm;

// GL-context-thread only. No Qt, audio device, synthesis, file picker or UI.
class ProjectMRenderer
{
public:
    ProjectMRenderer() = default;
    ~ProjectMRenderer();
    ProjectMRenderer(const ProjectMRenderer &) = delete;
    ProjectMRenderer &operator=(const ProjectMRenderer &) = delete;
    void Create();
    void Destroy();
    void Load(std::string_view preset);
    void Render(std::span<const float> stereo, int width, int height, int fps);
    std::string TakeError();
private:
    projectm *handle = nullptr;
    int width = 0, height = 0;
    std::string error;
};
