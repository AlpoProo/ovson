#include "TextureLoader.h"
#include <Windows.h>
#include <gl/GL.h>

// stb_image for PNG loading from memory
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../Utils/stb_image.h"

namespace BedDefense
{
    TextureLoader* TextureLoader::s_instance = nullptr;
    HMODULE TextureLoader::s_hModule = nullptr;

    TextureLoader::TextureLoader() : m_initialized(false) {}

    TextureLoader* TextureLoader::getInstance()
    {
        if (!s_instance) {
            s_instance = new TextureLoader();
        }
        return s_instance;
    }

    void TextureLoader::destroy()
    {
        if (s_instance) {
            for (auto& pair : s_instance->m_textures) {
                if (pair.second != 0) {
                    glDeleteTextures(1, &pair.second);
                }
            }
            delete s_instance;
            s_instance = nullptr;
        }
    }

    void TextureLoader::setModule(HMODULE hModule)
    {
        s_hModule = hModule;
    }

    void TextureLoader::init()
    {
        if (m_initialized) return;

        struct BlockResource {
            const char* name;
            int resourceId;
        };

        m_initialized = true;
    }

    unsigned int TextureLoader::loadFromResource(int resourceId)
    {
        if (!s_hModule) return 0;

        HRSRC hRes = FindResource(s_hModule, MAKEINTRESOURCE(resourceId), RT_RCDATA);
        if (!hRes) return 0;

        HGLOBAL hData = LoadResource(s_hModule, hRes);
        if (!hData) return 0;

        void* pData = LockResource(hData);
        DWORD size = SizeofResource(s_hModule, hRes);
        if (!pData || size == 0) return 0;

        int width, height, channels;
        unsigned char* imgData = stbi_load_from_memory(
            (const stbi_uc*)pData, (int)size,
            &width, &height, &channels, STBI_rgb_alpha
        );

        if (!imgData) return 0;

        unsigned int textureId;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, imgData);

        stbi_image_free(imgData);
        glBindTexture(GL_TEXTURE_2D, 0);

        return textureId;
    }

    int TextureLoader::getResourceIdForBlock(const std::string& textureName)
    {
        static const struct { const char* name; int id; } mapping[] = {
            {"wool_white", 201}, {"wool_orange", 202}, {"wool_magenta", 203}, {"wool_lightblue", 204},
            {"wool_yellow", 205}, {"wool_lime", 206}, {"wool_pink", 207}, {"wool_gray", 208},
            {"wool_silver", 209}, {"wool_cyan", 210}, {"wool_purple", 211}, {"wool_blue", 212},
            {"wool_brown", 213}, {"wool_green", 214}, {"wool_red", 215}, {"wool_black", 216},
            {"glass_white", 217}, {"glass_orange", 218}, {"glass_magenta", 219}, {"glass_lightblue", 220},
            {"glass_yellow", 221}, {"glass_lime", 222}, {"glass_pink", 223}, {"glass_gray", 224},
            {"glass_silver", 225}, {"glass_cyan", 226}, {"glass_purple", 227}, {"glass_blue", 228},
            {"glass_brown", 229}, {"glass_green", 230}, {"glass_red", 231}, {"glass_black", 232},
            {"glass", 233},
            {"planks_oak", 234}, {"planks_spruce", 235}, {"planks_birch", 236},
            {"planks_jungle", 237}, {"planks_acacia", 238}, {"planks_darkoak", 239},
            {"log_oak", 240}, {"log_spruce", 241}, {"log_birch", 242},
            {"log_jungle", 243}, {"log_acacia", 244}, {"log_darkoak", 245},
            {"obsidian", 246}, {"end_stone", 247}, {"terracotta", 248},
            {"terracotta_white", 249}, {"terracotta_orange", 250}, {"terracotta_magenta", 251}, {"terracotta_lightblue", 252},
            {"terracotta_yellow", 253}, {"terracotta_lime", 254}, {"terracotta_pink", 255}, {"terracotta_gray", 256},
            {"terracotta_silver", 257}, {"terracotta_cyan", 258}, {"terracotta_purple", 259}, {"terracotta_blue", 260},
            {"terracotta_brown", 261}, {"terracotta_green", 262}, {"terracotta_red", 263}, {"terracotta_black", 264},
        };

        for (const auto& m : mapping) {
            if (textureName == m.name) return m.id;
        }
        return 0;
    }

    unsigned int TextureLoader::getTexture(const std::string& textureName)
    {
        auto it = m_textures.find(textureName);
        if (it != m_textures.end()) {
            return it->second;
        }

        int resId = getResourceIdForBlock(textureName);
        if (resId == 0) {
            m_textures[textureName] = 0;
            return 0;
        }

        unsigned int texId = loadFromResource(resId);
        m_textures[textureName] = texId;
        return texId;
    }

    bool TextureLoader::hasTexture(const std::string& textureName)
    {
        return getTexture(textureName) != 0;
    }
}
