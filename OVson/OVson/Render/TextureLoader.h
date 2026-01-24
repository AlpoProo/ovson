#pragma once
#include <string>
#include <unordered_map>
#include <Windows.h>

namespace BedDefense
{
    class TextureLoader
    {
    private:
        static TextureLoader* s_instance;
        static HMODULE s_hModule;
        std::unordered_map<std::string, unsigned int> m_textures; // texture name -> OpenGL texture ID
        bool m_initialized;

        TextureLoader();
        unsigned int loadFromResource(int resourceId);
        int getResourceIdForBlock(const std::string& textureName);

    public:
        static TextureLoader* getInstance();
        static void destroy();
        static void setModule(HMODULE hModule);

        void init();
        unsigned int getTexture(const std::string& textureName);
        bool hasTexture(const std::string& textureName);

        TextureLoader(const TextureLoader&) = delete;
        TextureLoader& operator=(const TextureLoader&) = delete;
    };
}
