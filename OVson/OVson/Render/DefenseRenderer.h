#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "FontRenderer.h"

namespace BedDefense
{
    struct DefenseLayer;
    struct DetectedBed;

    class DefenseRenderer
    {
    private:
        static DefenseRenderer* s_instance;
        FontRenderer m_font;
        
        struct JNICache {
            bool initialized;
            void* mc_theMc;
            void* mc_renderViewEntity;
            void* mc_renderManager; // f5 support (not working 100%)
            void* mc_gameSettings;
            void* mc_timer;
            void* rm_viewerPosX, *rm_viewerPosY, *rm_viewerPosZ;
            void* rm_playerViewX, *rm_playerViewY;
            void* ent_posX, *ent_posY, *ent_posZ;
            void* ent_prevX, *ent_prevY, *ent_prevZ;
            void* ent_yaw, *ent_pitch;
            void* gs_fovSetting;
            void* timer_partialTicks;
            void* mc_getTextureManager;
            void* texMap_locationBlocksTexture;
            void* texMan_bindTexture;
        } m_ids;

        DefenseRenderer();
        ~DefenseRenderer();

        void initIds(void* envPtr);
        void setupBillboard(double camX, double camY, double camZ, double bedX, double bedY, double bedZ);
        void renderBlockIcon(float x, float y, const struct DefenseLayer& layer);

        std::string getTextureNameForBlock(const std::string& blockName, int metadata);

    public:
        static DefenseRenderer* getInstance();
        static void destroy();

        void render(void* hdc, double partialTicksManual);

        DefenseRenderer(const DefenseRenderer&) = delete;
        DefenseRenderer& operator=(const DefenseRenderer&) = delete;
    };
}
