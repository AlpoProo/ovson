#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <Windows.h>
#include <mutex>
#include <atomic>

namespace BedDefense
{
    class Block;
    struct BlockPos;
    enum class EnumDyeColor;

    struct DefenseLayer
    {
        std::string blockName;
        int metadata;
        float minU, maxU, minV, maxV;
        uint32_t color;
        bool hasTexture;
        
        DefenseLayer() : metadata(0), minU(0), maxU(1), minV(0), maxV(1), color(0xFFFFFFFF), hasTexture(false) {}
        DefenseLayer(const std::string& name, int meta) : blockName(name), metadata(meta), minU(0), maxU(1), minV(0), maxV(1), color(0xFFFFFFFF), hasTexture(false) {}
    };

    struct DetectedBed
    {
        int x, y, z;
        std::string teamColor;
        std::vector<DefenseLayer> layers;
        bool dirty;
        ULONGLONG lastScan;

        DetectedBed() : x(0), y(0), z(0), dirty(true), lastScan(0) {}
        DetectedBed(int px, int py, int pz, const std::string& team)
            : x(px), y(py), z(pz), teamColor(team), dirty(true), lastScan(0) {}

        std::string getKey() const {
            return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
        }

        int distanceSquared(int px, int py, int pz) const {
            int dx = x - px;
            int dy = y - py;
            int dz = z - pz;
            return dx * dx + dy * dy + dz * dz;
        }
    };

    class BedDefenseManager
    {
    private:
        static BedDefenseManager* s_instance;
        
        std::unordered_map<std::string, DetectedBed> m_beds;
        bool m_enabled;
        ULONGLONG m_lastRevalidation;
        
        std::mutex m_bedMutex;
        std::atomic<bool> m_isScanning;

        BedDefenseManager();
        ~BedDefenseManager();
        std::vector<DefenseLayer> scanDirection(const DetectedBed& bed, int dx, int dy, int dz);
        std::vector<DefenseLayer> selectBestLayers(DetectedBed& bed);
        void fillRenderData(DefenseLayer& layer);
        bool hasObsidian(const std::vector<DefenseLayer>& layers);

        bool isAir(int x, int y, int z);
        bool isBed(int x, int y, int z);
        bool isChunkLoaded(int x, int z);
        std::string getBedTeamColor(int x, int y, int z);
        std::string getTeamFromProximity(int bx, int by, int bz);
        
        void scanChunkInto(int chunkX, int chunkZ, std::unordered_map<std::string, DetectedBed>& targetMap);

        void asyncScanTask();

    public:
        static BedDefenseManager* getInstance();
        static void destroy();
        std::mutex& getMutex() { return m_bedMutex; }

        std::string getBlockName(int x, int y, int z);
        int getBlockMetadata(int x, int y, int z);
        void enable();
        void disable();
        bool isEnabled() const { return m_enabled; }
        void detectBed(int x, int y, int z);
        void removeBed(int x, int y, int z);
        void markBedDirty(int x, int y, int z, int radius = 5);
        void clearAllBeds();
        void tick();
        void forceScan();
        void onChunkLoad(int chunkX, int chunkZ);
        void onBlockChange(int x, int y, int z);
        void onWorldChange();

        const std::unordered_map<std::string, DetectedBed>& getBeds() const { return m_beds; }
        
        BedDefenseManager(const BedDefenseManager&) = delete;
        BedDefenseManager& operator=(const BedDefenseManager&) = delete;
    };
}
