// SPDX-License-Identifier: MS-PL

#include "SceneRegistry.hpp"

namespace CnaBenchmark
{
    std::vector<std::unique_ptr<Scene>> CreateAllScenes()
    {
        std::vector<std::unique_ptr<Scene>> scenes;
        scenes.push_back(CreateSpritesScene());
        scenes.push_back(CreateParticlesScene());
        scenes.push_back(CreateSkeletalScene());
        scenes.push_back(CreateTerrainScene());
        scenes.push_back(CreateLightsScene());
        scenes.push_back(CreateInstancingScene());
        scenes.push_back(CreateStreamingScene());
        return scenes;
    }

    bool IsKnownSceneId(std::string_view id)
    {
        for (const std::unique_ptr<Scene>& scene : CreateAllScenes())
            if (scene->Id() == id)
                return true;
        return false;
    }

    std::vector<std::string> AllSceneIds()
    {
        std::vector<std::string> ids;
        for (const std::unique_ptr<Scene>& scene : CreateAllScenes())
            ids.emplace_back(scene->Id());
        return ids;
    }
} // namespace CnaBenchmark
