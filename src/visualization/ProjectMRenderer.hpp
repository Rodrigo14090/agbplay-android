#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <random>
struct projectm_playlist;
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
    void SetFiles(const std::vector<std::string> &paths, const std::string &textures);
    void Select(unsigned index);
    void Next(bool previous);
    unsigned Current() const;
    unsigned Changes() const { return changes; }
    void Render(std::span<const float> stereo, int width, int height, int fps);
    std::string TakeError();
private:
    projectm *handle = nullptr;
    projectm_playlist *playlist = nullptr;
    unsigned changes = 0;
    std::mt19937 random{std::random_device{}()};
    int width = 0, height = 0;
    std::string error;
};
