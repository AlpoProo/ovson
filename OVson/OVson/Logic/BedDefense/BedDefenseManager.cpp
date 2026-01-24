// probably the shittiest bedplates 
// logic sucks but im no cheat developer
#include "BedDefenseManager.h"
#include "../../Java.h"
#include "../../Utils/Logger.h"
#include "../../Chat/ChatSDK.h"
#include "../../Config/Config.h"
#include <algorithm>
#include <thread>

namespace BedDefense
{
    BedDefenseManager* BedDefenseManager::s_instance = nullptr;

BedDefenseManager::BedDefenseManager()
    : m_enabled(Config::isBedDefenseEnabled()), m_lastRevalidation(0), m_isScanning(false)
{
    Logger::log(Config::DebugCategory::BedDefense, "BedDefenseManager initialized");
}

BedDefenseManager::~BedDefenseManager()
{
    clearAllBeds();
}

BedDefenseManager* BedDefenseManager::getInstance()
{
    if (!s_instance) {
        s_instance = new BedDefenseManager();
    }
    return s_instance;
}

void BedDefenseManager::destroy()
{
    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }
}

void BedDefenseManager::enable()
{
    m_enabled = true;
    Logger::log(Config::DebugCategory::BedDefense, "Bed defense detection enabled");
}

void BedDefenseManager::disable()
{
    m_enabled = false;
    Logger::info("Bed defense detection disabled");
}

void BedDefenseManager::clearAllBeds()
{
    std::lock_guard<std::mutex> lock(m_bedMutex);
    m_beds.clear();
    Logger::log(Config::DebugCategory::BedDetection, "Cleared all detected beds");
}

void BedDefenseManager::onWorldChange()
{
    clearAllBeds();
    m_lastRevalidation = 0;
}

// ============================================================================
// im a genie in a bottle
// ============================================================================

bool BedDefenseManager::isChunkLoaded(int x, int z)
{
    if (!lc) return false;
    
    JNIEnv* env = lc->getEnv();
    if (!env) return false;

    try {
        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        if (!mcCls) { if (Config::isDebugging()) ChatSDK::showPrefixed("§cDebug: mcCls fail"); return false; }

        jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
        if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
        if (!theMc) { if (Config::isDebugging()) ChatSDK::showPrefixed("§cDebug: theMc fail"); return false; }

        jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
        if (!mcObj) { if (Config::isDebugging()) ChatSDK::showPrefixed("§cDebug: mcObj fail"); return false; }

        jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
        if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
        if (!f_world) { env->DeleteLocalRef(mcObj); if (Config::isDebugging()) ChatSDK::showPrefixed("§cDebug: f_world fail"); return false; }

        jobject world = env->GetObjectField(mcObj, f_world);
        if (!world) { env->DeleteLocalRef(mcObj); if (Config::isDebugging()) ChatSDK::showPrefixed("§cDebug: worldObj fail"); return false; }

        jclass worldCls = lc->GetClass("net.minecraft.world.World");
        jmethodID m_isChunkLoaded = env->GetMethodID(worldCls, "isChunkLoaded", "(IIZ)Z");
        if (!m_isChunkLoaded) m_isChunkLoaded = env->GetMethodID(worldCls, "func_175680_a", "(IIZ)Z");

        bool loaded = false;
        if (m_isChunkLoaded) {
            loaded = env->CallBooleanMethod(world, m_isChunkLoaded, x >> 4, z >> 4, false);
        } else {
            if (Config::isDebugging()) ChatSDK::showPrefixed("§cDebug: isChunkLoaded method fail");
        }

        env->DeleteLocalRef(world);
        env->DeleteLocalRef(mcObj);
        
        return loaded;
    }
    catch (...) {
        if (Config::isDebugging()) ChatSDK::showPrefixed("§cException in isChunkLoaded");
        return false;
    }
}

std::string BedDefenseManager::getBlockName(int x, int y, int z)
{
    if (!lc) return "minecraft:air";
    JNIEnv* env = lc->getEnv();
    if (!env) return "minecraft:air";

    try {
        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
        if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
        jobject mcObj = theMc ? env->GetStaticObjectField(mcCls, theMc) : nullptr;
        if (!mcObj) return "minecraft:air";

        jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
        if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
        jobject world = f_world ? env->GetObjectField(mcObj, f_world) : nullptr;
        if (!world) { env->DeleteLocalRef(mcObj); return "minecraft:air"; }

        jclass bposCls = lc->GetClass("net.minecraft.util.BlockPos");
        jmethodID bposInit = env->GetMethodID(bposCls, "<init>", "(III)V");
        jobject bpos = env->NewObject(bposCls, bposInit, x, y, z);

        jclass worldCls = lc->GetClass("net.minecraft.world.World");
        jmethodID m_getState = env->GetMethodID(worldCls, "getBlockState", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
        if (!m_getState) m_getState = env->GetMethodID(worldCls, "func_180495_p", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
        
        jobject state = m_getState ? env->CallObjectMethod(world, m_getState, bpos) : nullptr;
        if (!state) {
            env->DeleteLocalRef(bpos); env->DeleteLocalRef(world); env->DeleteLocalRef(mcObj);
            return "minecraft:air";
        }

        jclass stateCls = lc->GetClass("net.minecraft.block.state.IBlockState");
        jmethodID m_getBlock = env->GetMethodID(stateCls, "getBlock", "()Lnet/minecraft/block/Block;");
        if (!m_getBlock) m_getBlock = env->GetMethodID(stateCls, "func_177230_c", "()Lnet/minecraft/block/Block;");
        
        jobject block = m_getBlock ? env->CallObjectMethod(state, m_getBlock) : nullptr;
        if (!block) {
            env->DeleteLocalRef(state); env->DeleteLocalRef(bpos); env->DeleteLocalRef(world); env->DeleteLocalRef(mcObj);
            return "minecraft:air";
        }

        std::string blockName = "minecraft:air";
        bool nameFound = false;

        jclass blockCls = lc->GetClass("net.minecraft.block.Block");
        jmethodID m_getId = env->GetStaticMethodID(blockCls, "getIdFromBlock", "(Lnet/minecraft/block/Block;)I");
        if (!m_getId) m_getId = env->GetStaticMethodID(blockCls, "func_149682_b", "(Lnet/minecraft/block/Block;)I");
        int id = m_getId ? env->CallStaticIntMethod(blockCls, m_getId, block) : -1;

        if      (id == 0)   { blockName = "minecraft:air";      nameFound = true; }
        else if (id == 26)  { blockName = "minecraft:bed";      nameFound = true; }
        else if (id == 49)  { blockName = "minecraft:obsidian"; nameFound = true; }
        else if (id == 121) { blockName = "minecraft:end_stone";nameFound = true; }
        else if (id == 35)  { blockName = "minecraft:wool";     nameFound = true; }
        else if (id == 24)  { blockName = "minecraft:sandstone";nameFound = true; }
        else if (id == 5)   { blockName = "minecraft:planks";   nameFound = true; }
        else if (id == 159) { blockName = "minecraft:stained_hardened_clay"; nameFound = true; }
        else if (id == 172) { blockName = "minecraft:hardened_clay";         nameFound = true; }
        else if (id == 80)  { blockName = "minecraft:snow";     nameFound = true; }

        if (!nameFound) {
            jmethodID m_getRegistryName = env->GetMethodID(blockCls, "getRegistryName", "()Lnet/minecraft/util/ResourceLocation;");
            if (m_getRegistryName) {
                jobject resLoc = env->CallObjectMethod(block, m_getRegistryName);
                if (resLoc) {
                    jclass resLocCls = lc->GetClass("net.minecraft.util.ResourceLocation");
                    jmethodID m_toString = env->GetMethodID(resLocCls, "toString", "()Ljava/lang/String;");
                    jstring jstr = m_toString ? (jstring)env->CallObjectMethod(resLoc, m_toString) : nullptr;
                    if (jstr) {
                        const char* utf = env->GetStringUTFChars(jstr, 0);
                        if (utf) { blockName = utf; nameFound = true; env->ReleaseStringUTFChars(jstr, utf); }
                        env->DeleteLocalRef(jstr);
                    }
                    env->DeleteLocalRef(resLoc);
                }
            }

            if (!nameFound) {
                jfieldID f_registry = env->GetStaticFieldID(blockCls, "blockRegistry", "Lnet/minecraft/util/RegistryNamespacedDefaultedByKey;");
                if (!f_registry) f_registry = env->GetStaticFieldID(blockCls, "field_149771_c", "Lnet/minecraft/util/RegistryNamespacedDefaultedByKey;");
                
                if (f_registry) {
                    jobject registry = env->GetStaticObjectField(blockCls, f_registry);
                    if (registry) {
                        jclass registryCls = lc->GetClass("net.minecraft.util.RegistryNamespacedDefaultedByKey");
                        jmethodID m_getName = env->GetMethodID(registryCls, "getNameForObject", "(Ljava/lang/Object;)Ljava/lang/Object;");
                        if (!m_getName) m_getName = env->GetMethodID(registryCls, "func_177774_c", "(Ljava/lang/Object;)Ljava/lang/Object;");
                        
                        if (m_getName) {
                            jobject resLoc = env->CallObjectMethod(registry, m_getName, block);
                            if (resLoc) {
                                jclass resLocCls = lc->GetClass("net.minecraft.util.ResourceLocation");
                                jmethodID m_toString = env->GetMethodID(resLocCls, "toString", "()Ljava/lang/String;");
                                jstring jstr = (jstring)env->CallObjectMethod(resLoc, m_toString);
                                if (jstr) {
                                    const char* utf = env->GetStringUTFChars(jstr, 0);
                                    if (utf) { blockName = utf; nameFound = true; env->ReleaseStringUTFChars(jstr, utf); }
                                    env->DeleteLocalRef(jstr);
                                }
                                env->DeleteLocalRef(resLoc);
                            }
                        }
                        env->DeleteLocalRef(registry);
                    }
                }
            }
        }

        if (Config::isDebugging() && !nameFound && id > 0) {
            ChatSDK::showPrefixed("§7[Debug] Unmapped ID: §f" + std::to_string(id));
        }

        env->DeleteLocalRef(block); env->DeleteLocalRef(state); env->DeleteLocalRef(bpos); env->DeleteLocalRef(world); env->DeleteLocalRef(mcObj);
        return blockName;
    }
    catch (...) {
        return "minecraft:air";
    }
}

int BedDefenseManager::getBlockMetadata(int x, int y, int z)
{
    if (!lc) return 0;
    JNIEnv* env = lc->getEnv();
    if (!env) return 0;

    try {
        if (!isChunkLoaded(x, z)) return 0;

        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
        if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
        jobject mcObj = theMc ? env->GetStaticObjectField(mcCls, theMc) : nullptr;
        if (!mcObj) return 0;

        jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
        if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
        jobject world = f_world ? env->GetObjectField(mcObj, f_world) : nullptr;
        if (!world) { env->DeleteLocalRef(mcObj); return 0; }

        jclass bposCls = lc->GetClass("net.minecraft.util.BlockPos");
        jmethodID bposInit = env->GetMethodID(bposCls, "<init>", "(III)V");
        jobject bpos = env->NewObject(bposCls, bposInit, x, y, z);
        
        jclass worldCls = lc->GetClass("net.minecraft.world.World");
        jmethodID m_getState = env->GetMethodID(worldCls, "getBlockState", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
        if (!m_getState) m_getState = env->GetMethodID(worldCls, "func_180495_p", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
        
        jobject state = m_getState ? env->CallObjectMethod(world, m_getState, bpos) : nullptr;
        
        int meta = 0;
        if (state) {
            jclass stateCls = lc->GetClass("net.minecraft.block.state.IBlockState");
            jmethodID m_getBlock = env->GetMethodID(stateCls, "getBlock", "()Lnet/minecraft/block/Block;");
            if (!m_getBlock) m_getBlock = env->GetMethodID(stateCls, "func_177230_c", "()Lnet/minecraft/block/Block;");
            
            jobject block = m_getBlock ? env->CallObjectMethod(state, m_getBlock) : nullptr;
            if (block) {
                jclass blockCls = lc->GetClass("net.minecraft.block.Block");
                jmethodID m_getMeta = env->GetMethodID(blockCls, "getMetaFromState", "(Lnet/minecraft/block/state/IBlockState;)I");
                if (!m_getMeta) m_getMeta = env->GetMethodID(blockCls, "func_176201_c", "(Lnet/minecraft/block/state/IBlockState;)I");
                
                if (m_getMeta) {
                    meta = env->CallIntMethod(block, m_getMeta, state);
                }
                env->DeleteLocalRef(block);
            }
            env->DeleteLocalRef(state);
        }

        if (bpos) env->DeleteLocalRef(bpos);
        env->DeleteLocalRef(world);
        env->DeleteLocalRef(mcObj);

        return meta;
    }
    catch (...) {
        return 0;
    }
}

bool BedDefenseManager::isAir(int x, int y, int z)
{
    return getBlockName(x, y, z) == "minecraft:air";
}

bool BedDefenseManager::isBed(int x, int y, int z)
{
    std::string name = getBlockName(x, y, z);
    return name == "minecraft:bed";
}

std::string BedDefenseManager::getBedTeamColor(int x, int y, int z)
{
    int meta = getBlockMetadata(x, y, z);
    
    switch (meta) {
        case 14: return "RED";
        case 11: return "BLUE";
        case 13: return "GREEN";
        case 4: return "YELLOW";
        case 1: return "ORANGE";
        case 0: return "WHITE";
        case 15: return "BLACK";
        case 9: return "CYAN";
        case 5: return "LIME";
        case 10: return "PURPLE";
        case 2: return "MAGENTA";
        case 6: return "PINK";
        case 12: return "BROWN";
        case 7: return "GRAY";
        case 8: return "LIGHT_GRAY";
        case 3: return "LIGHT_BLUE";
        default: return "UNKNOWN";
    }
}

void BedDefenseManager::detectBed(int x, int y, int z)
{
    if (!m_enabled) return;
    if (!isBed(x, y, z)) return;

    for (auto& pair : m_beds) {
        DetectedBed& existing = pair.second;
        if (existing.y == y && existing.distanceSquared(x, y, z) < 2.5) {
            return;
        }
    }

    std::string team = getBedTeamColor(x, y, z);
    DetectedBed bed(x, y, z, team);
    
    std::string key = bed.getKey();
    m_beds[key] = bed;
    
    Logger::log(Config::DebugCategory::BedDetection, "Detected bed at (%d, %d, %d) - Team: %s", x, y, z, team.c_str());
}

void BedDefenseManager::removeBed(int x, int y, int z)
{
    DetectedBed temp(x, y, z, "");
    std::string key = temp.getKey();
    
    auto it = m_beds.find(key);
    if (it != m_beds.end()) {
        m_beds.erase(it);
        Logger::log(Config::DebugCategory::BedDetection, "Removed bed at (%d, %d, %d)", x, y, z);
    }
}

void BedDefenseManager::markBedDirty(int x, int y, int z, int radius)
{
    int radiusSquared = radius * radius;
    
    for (auto& pair : m_beds) {
        DetectedBed& bed = pair.second;
        if (bed.distanceSquared(x, y, z) <= radiusSquared) {
            bed.dirty = true;
        }
    }
}

void BedDefenseManager::onBlockChange(int x, int y, int z)
{
    if (!m_enabled) return;
    
    if (isBed(x, y, z)) {
        detectBed(x, y, z);
    } else {
        removeBed(x, y, z);
    }
    
    markBedDirty(x, y, z, 5);
}

std::string BedDefenseManager::getTeamFromProximity(int bx, int by, int bz)
{
    JNIEnv* env = lc->getEnv();
    if (!env) return "BED";

    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
    if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
    jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
    if (!mcObj) return "BED";

    jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
    jobject world = env->GetObjectField(mcObj, f_world);
    if (!world) { env->DeleteLocalRef(mcObj); return "BED"; }

    jclass worldCls = lc->GetClass("net.minecraft.world.World");
    jmethodID m_getState = env->GetMethodID(worldCls, "getBlockState", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
    if (!m_getState) m_getState = env->GetMethodID(worldCls, "func_180495_p", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");

    jclass bposCls = lc->GetClass("net.minecraft.util.BlockPos");
    jmethodID bposInit = env->GetMethodID(bposCls, "<init>", "(III)V");

    jclass stateCls = lc->GetClass("net.minecraft.block.state.IBlockState");
    jmethodID m_getBlock = env->GetMethodID(stateCls, "getBlock", "()Lnet/minecraft/block/Block;");
    if (!m_getBlock) m_getBlock = env->GetMethodID(stateCls, "func_177230_c", "()Lnet/minecraft/block/Block;");

    jclass blockCls = lc->GetClass("net.minecraft.block.Block");
    jmethodID m_getId = env->GetStaticMethodID(blockCls, "getIdFromBlock", "(Lnet/minecraft/block/Block;)I");
    if (!m_getId) m_getId = env->GetStaticMethodID(blockCls, "func_149682_b", "(Lnet/minecraft/block/Block;)I");
    jmethodID m_getMeta = env->GetMethodID(blockCls, "getMetaFromState", "(Lnet/minecraft/block/state/IBlockState;)I");
    if (!m_getMeta) m_getMeta = env->GetMethodID(blockCls, "func_176201_c", "(Lnet/minecraft/block/state/IBlockState;)I");

    std::string detectedTeam = "BED";

    for (int x = bx - 1; x <= bx + 1; x++) {
        for (int z = bz - 1; z <= bz + 1; z++) {
            jobject bpos = env->NewObject(bposCls, bposInit, x, by - 1, z);
            jobject state = env->CallObjectMethod(world, m_getState, bpos);
            if (state) {
                jobject block = env->CallObjectMethod(state, m_getBlock);
                if (block) {
                    int id = m_getId ? env->CallStaticIntMethod(blockCls, m_getId, block) : -1;
                    if (id == 35 || id == 159) {
                        int meta = m_getMeta ? env->CallIntMethod(block, m_getMeta, state) : 0;
                        switch(meta) {
                            case 14: detectedTeam = "RED"; break;
                            case 11: detectedTeam = "BLUE"; break;
                            case 13: detectedTeam = "GREEN"; break;
                            case 4: detectedTeam = "YELLOW"; break;
                            case 1: detectedTeam = "ORANGE"; break;
                            case 3: detectedTeam = "AQUA"; break;
                            case 10: detectedTeam = "PURPLE"; break;
                            case 0: detectedTeam = "WHITE"; break;
                        }
                    }
                    env->DeleteLocalRef(block);
                }
                env->DeleteLocalRef(state);
            }
            env->DeleteLocalRef(bpos);
            if (detectedTeam != "BED") break;
        }
        if (detectedTeam != "BED") break;
    }

    env->DeleteLocalRef(world);
    env->DeleteLocalRef(mcObj);
    return detectedTeam;
}

void BedDefenseManager::onChunkLoad(int chunkX, int chunkZ)
{
    scanChunkInto(chunkX, chunkZ, m_beds);
}

void BedDefenseManager::scanChunkInto(int chunkX, int chunkZ, std::unordered_map<std::string, DetectedBed>& targetMap)
{
    if (!m_enabled || !lc) return;
    JNIEnv* env = lc->getEnv();
    if (!env) return;

    if (env->PushLocalFrame(100) != 0) return;
    int bedsFoundInChunk = 0;
    int blocksSampled = 0;

    try {
        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
        if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
        jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
        
        jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
        if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
        jobject world = env->GetObjectField(mcObj, f_world);

        if (!world) {
        } else {
            jclass worldCls = env->GetObjectClass(world);
            jmethodID m_getChunk = env->GetMethodID(worldCls, "getChunkFromChunkCoords", "(II)Lnet/minecraft/world/chunk/Chunk;");
            if (!m_getChunk) m_getChunk = env->GetMethodID(worldCls, "func_72964_e", "(II)Lnet/minecraft/world/chunk/Chunk;");
            
            jobject chunk = env->CallObjectMethod(world, m_getChunk, chunkX, chunkZ);
            if (chunk) {
                jclass chunkCls = env->GetObjectClass(chunk);
                jfieldID f_storage = env->GetFieldID(chunkCls, "storageArrays", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;");
                if (!f_storage) f_storage = env->GetFieldID(chunkCls, "field_76652_q", "[Lnet/minecraft/world/chunk/storage/ExtendedBlockStorage;");
                
                jobjectArray storageArray = (jobjectArray)env->GetObjectField(chunk, f_storage);
                if (storageArray) {
                    jclass bposCls = lc->GetClass("net.minecraft.util.BlockPos");
                    jmethodID bposInit = env->GetMethodID(bposCls, "<init>", "(III)V");
                    
                    jclass blockCls = lc->GetClass("net.minecraft.block.Block");
                    jmethodID m_getId = env->GetStaticMethodID(blockCls, "getIdFromBlock", "(Lnet/minecraft/block/Block;)I");
                    if (!m_getId) m_getId = env->GetStaticMethodID(blockCls, "func_149682_b", "(Lnet/minecraft/block/Block;)I");

                    jmethodID m_getMeta = env->GetMethodID(blockCls, "getMetaFromState", "(Lnet/minecraft/block/state/IBlockState;)I");
                    if (!m_getMeta) m_getMeta = env->GetMethodID(blockCls, "func_176201_c", "(Lnet/minecraft/block/state/IBlockState;)I");

                    jmethodID m_getUnlocalized = env->GetMethodID(blockCls, "getUnlocalizedName", "()Ljava/lang/String;");
                    if (!m_getUnlocalized) m_getUnlocalized = env->GetMethodID(blockCls, "func_149739_a", "()Ljava/lang/String;");

                    jclass worldRealCls = lc->GetClass("net.minecraft.world.World");
                    jmethodID m_getState = env->GetMethodID(worldRealCls, "getBlockState", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
                    if (!m_getState) m_getState = env->GetMethodID(worldRealCls, "func_180495_p", "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");

                    jclass stateCls = lc->GetClass("net.minecraft.block.state.IBlockState");
                    jmethodID m_getBlock = env->GetMethodID(stateCls, "getBlock", "()Lnet/minecraft/block/Block;");
                    if (!m_getBlock) m_getBlock = env->GetMethodID(stateCls, "func_177230_c", "()Lnet/minecraft/block/Block;");

                    jclass bedCls = lc->GetClass("net.minecraft.block.BlockBed");

                    for (int s = 0; s < 16; s++) {
                        jobject storage = env->GetObjectArrayElement(storageArray, s);
                        if (!storage) continue;
                        env->DeleteLocalRef(storage);

                        for (int x = 0; x < 16; x++) {
                            for (int z = 0; z < 16; z++) {
                                env->PushLocalFrame(20);
                                for (int y = s * 16; y < (s * 16) + 16; y++) {
                                    int wx = (chunkX * 16) + x;
                                    int wz = (chunkZ * 16) + z;
                                    
                                    jobject bpos = env->NewObject(bposCls, bposInit, wx, y, wz);
                                    jobject state = env->CallObjectMethod(world, m_getState, bpos);
                                    if (state) {
                                        jobject block = env->CallObjectMethod(state, m_getBlock);
                                        if (block) {
                                            int id = (m_getId) ? env->CallStaticIntMethod(blockCls, m_getId, block) : -1;
                                            bool isABed = (id == 26) || (bedCls && env->IsInstanceOf(block, bedCls));
                                            
                                            if (!isABed && m_getUnlocalized) {
                                                jstring jName = (jstring)env->CallObjectMethod(block, m_getUnlocalized);
                                                if (jName) {
                                                    const char* nameStr = env->GetStringUTFChars(jName, nullptr);
                                                    if (nameStr) {
                                                        if (strstr(nameStr, "tile.bed") && !strstr(nameStr, "bedrock")) isABed = true;
                                                        env->ReleaseStringUTFChars(jName, nameStr);
                                                    }
                                                    env->DeleteLocalRef(jName);
                                                }
                                            }

                                            if (isABed) {
                                                int bedMeta = m_getMeta ? env->CallIntMethod(block, m_getMeta, state) : 0;
                                                bool isHead = (bedMeta & 0x8) != 0;

                                                if (isHead) {
                                                    bool exists = false;
                                                    for (auto& pair : targetMap) {
                                                        if (pair.second.y == y && pair.second.distanceSquared(wx, y, wz) < 2.5) {
                                                            exists = true; break;
                                                        }
                                                    }
                                                    if (!exists) {
                                                        bedsFoundInChunk++;
                                                        std::string team = getTeamFromProximity(wx, y, wz);
                                                        
                                                         DetectedBed bed(wx, y, wz, team);
                                                         bed.layers = selectBestLayers(bed);
                                                         
                                                         targetMap[bed.getKey()] = bed;

                                                        Logger::log(Config::DebugCategory::BedDetection, "Detected %s Bed (Head) at %d, %d, %d", team.c_str(), wx, y, wz);
                                                    }
                                                } else if (Config::isDebugging()) {
                                                }
                                            }
                                            env->DeleteLocalRef(block);
                                        }
                                        env->DeleteLocalRef(state);
                                    }
                                    env->DeleteLocalRef(bpos);
                                }
                                env->PopLocalFrame(nullptr);
                            }
                        }
                    }
                    env->DeleteLocalRef(storageArray);
                }
                env->DeleteLocalRef(chunk);
            }
        }
        env->DeleteLocalRef(mcObj);
    } catch (...) {}

    if (bedsFoundInChunk > 0) {
        Logger::log(Config::DebugCategory::BedDetection, "Scan Result: Found %d beds in chunk %d, %d", bedsFoundInChunk, chunkX, chunkZ);
    }
    env->PopLocalFrame(nullptr);
}

std::vector<DefenseLayer> BedDefenseManager::scanDirection(const DetectedBed& bed, int dx, int dy, int dz)
{
    std::vector<DefenseLayer> layers;
    std::string lastBlock;
    
    const int MAX_DEPTH = 4; 
    
    for (int i = 1; i <= MAX_DEPTH; i++) {
        int x = bed.x + (dx * i);
        int y = bed.y + (dy * i);
        int z = bed.z + (dz * i);
        if (y < bed.y) continue; 
        std::string blockName = getBlockName(x, y, z);
        if (blockName == "minecraft:air" || blockName.empty()) continue;
        if (blockName.find("bed") != std::string::npos) continue;

        bool isDefense = false;
        if      (blockName.find("wool") != std::string::npos) isDefense = true;
        else if (blockName.find("stained_hardened_clay") != std::string::npos) isDefense = true;
        else if (blockName.find("obsidian") != std::string::npos) isDefense = true;
        else if (blockName.find("end_stone") != std::string::npos) isDefense = true;
        else if (blockName.find("wood") != std::string::npos || blockName.find("planks") != std::string::npos) isDefense = true;
        else if (blockName.find("glass") != std::string::npos) isDefense = true;

        if (!isDefense) continue;

        if (blockName != lastBlock) {
            int meta = getBlockMetadata(x, y, z);
            DefenseLayer layer(blockName, meta);
            layer.hasTexture = false;
            layers.push_back(layer);
            lastBlock = blockName;
        }
    }
    
    return layers;
}

void BedDefenseManager::fillRenderData(DefenseLayer& layer)
{
    if (!lc) return;
    JNIEnv* env = lc->getEnv();
    if (!env) return;

    if (layer.blockName.find("wool") != std::string::npos || layer.blockName.find("stained_hardened_clay") != std::string::npos) {
        switch (layer.metadata) {
            case 0:  layer.color = 0xFFFFFFFF; break; // White
            case 1:  layer.color = 0xFFFFA500; break; // Orange
            case 2:  layer.color = 0xFFBF40BF; break; // Magenta
            case 3:  layer.color = 0xFFADD8E6; break; // Light Blue
            case 4:  layer.color = 0xFFFFFF00; break; // Yellow
            case 5:  layer.color = 0xFF32CD32; break; // Lime
            case 6:  layer.color = 0xFFFFC0CB; break; // Pink
            case 7:  layer.color = 0xFF808080; break; // Gray
            case 8:  layer.color = 0xFFC0C0C0; break; // Silver
            case 9:  layer.color = 0xFF00FFFF; break; // Cyan
            case 10: layer.color = 0xFF800080; break; // Purple
            case 11: layer.color = 0xFF0000FF; break; // Blue
            case 12: layer.color = 0xFFA52A2A; break; // Brown
            case 13: layer.color = 0xFF008000; break; // Green
            case 14: layer.color = 0xFFFF0000; break; // Red
            case 15: layer.color = 0xFF000000; break; // Black
        }
    } else if (layer.blockName.find("glass") != std::string::npos) {
        switch (layer.metadata) {
            case 0:  layer.color = 0x80FFFFFF; break; // White/Clear
            case 1:  layer.color = 0x80FFA500; break; // Orange
            case 2:  layer.color = 0x80BF40BF; break; // Magenta
            case 3:  layer.color = 0x80ADD8E6; break; // Light Blue
            case 4:  layer.color = 0x80FFFF00; break; // Yellow
            case 5:  layer.color = 0x8032CD32; break; // Lime
            case 6:  layer.color = 0x80FFC0CB; break; // Pink
            case 7:  layer.color = 0x80808080; break; // Gray
            case 8:  layer.color = 0x80C0C0C0; break; // Silver
            case 9:  layer.color = 0x8000FFFF; break; // Cyan
            case 10: layer.color = 0x80800080; break; // Purple
            case 11: layer.color = 0x800000FF; break; // Blue
            case 12: layer.color = 0x80A52A2A; break; // Brown
            case 13: layer.color = 0x80008000; break; // Green
            case 14: layer.color = 0x80FF0000; break; // Red
            case 15: layer.color = 0x80000000; break; // Black
        }
    }
    else if (layer.blockName.find("wood") != std::string::npos || layer.blockName.find("planks") != std::string::npos) {
        layer.color = 0xFF8B4513; // Brown
    } else if (layer.blockName.find("obsidian") != std::string::npos) {
        layer.color = 0xFF310062; // Deep Purple
    } else if (layer.blockName.find("end_stone") != std::string::npos) {
        layer.color = 0xFFF0E68C; // Khaki
    } else if (layer.blockName.find("glass") != std::string::npos) {
        layer.color = 0xAAFFFFFF; // Translucent White
    } else if (layer.blockName.find("bed") != std::string::npos) {
        layer.color = 0xFFFF0000; // Bright Red
    } else {
        layer.color = 0xFF888888;
    }

    try {
        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
        if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
        jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
        
        jmethodID m_getBlockRenderer = env->GetMethodID(mcCls, "getBlockRendererDispatcher", "()Lnet/minecraft/client/renderer/BlockRendererDispatcher;");
        if (!m_getBlockRenderer) m_getBlockRenderer = env->GetMethodID(mcCls, "func_175602_ab", "()Lnet/minecraft/client/renderer/BlockRendererDispatcher;");
        jobject dispatcher = env->CallObjectMethod(mcObj, m_getBlockRenderer);

        jclass blockCls = lc->GetClass("net.minecraft.block.Block");
        jmethodID m_getBlockFromName = env->GetStaticMethodID(blockCls, "getBlockFromName", "(Ljava/lang/String;)Lnet/minecraft/block/Block;");
        if (!m_getBlockFromName) m_getBlockFromName = env->GetStaticMethodID(blockCls, "func_149684_b", "(Ljava/lang/String;)Lnet/minecraft/block/Block;");
        jstring jBlockName = env->NewStringUTF(layer.blockName.c_str());
        jobject blockObj = env->CallStaticObjectMethod(blockCls, m_getBlockFromName, jBlockName);

        if (blockObj) {
            jmethodID m_getState = env->GetMethodID(blockCls, "getStateFromMeta", "(I)Lnet/minecraft/block/state/IBlockState;");
            if (!m_getState) m_getState = env->GetMethodID(blockCls, "func_176203_a", "(I)Lnet/minecraft/block/state/IBlockState;");
            jobject state = env->CallObjectMethod(blockObj, m_getState, layer.metadata);

            jclass dispatchCls = env->GetObjectClass(dispatcher);
            jmethodID m_getModel = env->GetMethodID(dispatchCls, "getBlockModelShapes", "()Lnet/minecraft/client/renderer/BlockModelShapes;");
            if (!m_getModel) m_getModel = env->GetMethodID(dispatchCls, "func_175023_a", "()Lnet/minecraft/client/renderer/BlockModelShapes;");
            jobject shapes = env->CallObjectMethod(dispatcher, m_getModel);

            jclass shapesCls = env->GetObjectClass(shapes);
            jmethodID m_getQuads = env->GetMethodID(shapesCls, "getTexture", "(Lnet/minecraft/block/state/IBlockState;)Lnet/minecraft/client/renderer/texture/TextureAtlasSprite;");
            if (!m_getQuads) m_getQuads = env->GetMethodID(shapesCls, "func_178122_a", "(Lnet/minecraft/block/state/IBlockState;)Lnet/minecraft/client/renderer/texture/TextureAtlasSprite;");
            jobject sprite = env->CallObjectMethod(shapes, m_getQuads, state);

            if (sprite) {
                jclass spriteCls = env->GetObjectClass(sprite);
                jmethodID m_getU = env->GetMethodID(spriteCls, "getMinU", "()F"); if (!m_getU) m_getU = env->GetMethodID(spriteCls, "func_94209_e", "()F");
                jmethodID m_getMaxU = env->GetMethodID(spriteCls, "getMaxU", "()F"); if (!m_getMaxU) m_getMaxU = env->GetMethodID(spriteCls, "func_94212_f", "()F");
                jmethodID m_getV = env->GetMethodID(spriteCls, "getMinV", "()F"); if (!m_getV) m_getV = env->GetMethodID(spriteCls, "func_94206_g", "()F");
                jmethodID m_getMaxV = env->GetMethodID(spriteCls, "getMaxV", "()F"); if (!m_getMaxV) m_getMaxV = env->GetMethodID(spriteCls, "func_94207_h", "()F");

                layer.minU = env->CallFloatMethod(sprite, m_getU);
                layer.maxU = env->CallFloatMethod(sprite, m_getMaxU);
                layer.minV = env->CallFloatMethod(sprite, m_getV);
                layer.maxV = env->CallFloatMethod(sprite, m_getMaxV);
                
                if (layer.minU == layer.maxU || layer.minV == layer.maxV || (layer.minU == 0 && layer.maxU == 0)) {
                    layer.hasTexture = false;
                } else {
                    layer.hasTexture = true;
                }
                
                env->DeleteLocalRef(sprite); env->DeleteLocalRef(spriteCls);
            } else {
                layer.hasTexture = false;
            }
            env->DeleteLocalRef(shapes); env->DeleteLocalRef(shapesCls);
            env->DeleteLocalRef(state);
        }
        if (jBlockName) env->DeleteLocalRef(jBlockName);
        env->DeleteLocalRef(dispatcher);
        env->DeleteLocalRef(mcObj);
    } catch (...) {}
}

bool BedDefenseManager::hasObsidian(const std::vector<DefenseLayer>& layers)
{
    for (const auto& layer : layers) {
        if (layer.blockName == "minecraft:obsidian") {
            return true;
        }
    }
    return false;
}

std::vector<DefenseLayer> BedDefenseManager::selectBestLayers(DetectedBed& bed)
{
    std::vector<DefenseLayer> best;
    std::unordered_map<std::string, int> uniqueLayers;
    
    struct Direction { int dx, dy, dz; };
    Direction directions[] = {
        {1, 0, 0},   // +X
        {-1, 0, 0},  // -X
        {0, 0, 1},   // +Z
        {0, 0, -1},  // -Z
        {0, 1, 0}    // +Y
    };
    
    for (const auto& dir : directions) {
        std::vector<DefenseLayer> dirLayers = scanDirection(bed, dir.dx, dir.dy, dir.dz);
        for (const auto& l : dirLayers) {
            uniqueLayers[l.blockName] = l.metadata;
        }
    }

    for (auto const& [name, meta] : uniqueLayers) {
        DefenseLayer layer(name, meta);
        fillRenderData(layer);
        best.push_back(layer);
    }
    
    std::sort(best.begin(), best.end(), [](const DefenseLayer& a, const DefenseLayer& b) {
        if (a.blockName.find("obsidian") != std::string::npos) return true;
        if (b.blockName.find("obsidian") != std::string::npos) return false;
        return a.blockName < b.blockName;
    });
    
    return best;
}

void BedDefenseManager::forceScan()
{
    if (!m_enabled || m_isScanning) return;
    
    m_isScanning = true;
    std::thread([this]() {
        asyncScanTask();
    }).detach();
}

void BedDefenseManager::asyncScanTask()
{
    if (!lc) { m_isScanning = false; return; }
    JNIEnv* env = lc->getEnv();
    if (!env) { m_isScanning = false; return; }

    if (Config::isDebugging()) ChatSDK::showPrefixed("§7[Manager] Background scan started...");

    try {
        jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
        jfieldID theMc = env->GetStaticFieldID(mcCls, "theMinecraft", "Lnet/minecraft/client/Minecraft;");
        if (!theMc) theMc = env->GetStaticFieldID(mcCls, "field_71432_P", "Lnet/minecraft/client/Minecraft;");
        jobject mcObj = env->GetStaticObjectField(mcCls, theMc);
        
        if (mcObj) {
            jfieldID f_player = env->GetFieldID(mcCls, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
            if (!f_player) f_player = env->GetFieldID(mcCls, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;");
            jobject player = f_player ? env->GetObjectField(mcObj, f_player) : nullptr;
            
            if (player) {
                jclass entityCls = lc->GetClass("net.minecraft.entity.Entity");
                jfieldID f_px = env->GetFieldID(entityCls, "posX", "D");
                jfieldID f_pz = env->GetFieldID(entityCls, "posZ", "D");
                if (f_px && f_pz) {
                    double px = env->GetDoubleField(player, f_px);
                    double pz = env->GetDoubleField(player, f_pz);
                    int playerCX = (int)px >> 4;
                    int playerCZ = (int)pz >> 4;

                    jfieldID f_world = env->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
                    if (!f_world) f_world = env->GetFieldID(mcCls, "field_71441_e", "Lnet/minecraft/client/multiplayer/WorldClient;");
                    jobject world = env->GetObjectField(mcObj, f_world);

                    if (world) {
                        jclass worldClientCls = lc->GetClass("net.minecraft.client.multiplayer.WorldClient");
                        jclass worldBaseCls = lc->GetClass("net.minecraft.world.World");
                        
                        jfieldID f_cp = env->GetFieldID(worldClientCls, "chunkProvider", "Lnet/minecraft/client/multiplayer/ChunkProviderClient;");
                        if (!f_cp) f_cp = env->GetFieldID(worldClientCls, "field_73033_b", "Lnet/minecraft/client/multiplayer/ChunkProviderClient;");
                        if (!f_cp) f_cp = env->GetFieldID(worldBaseCls, "chunkProvider", "Lnet/minecraft/world/chunk/IChunkProvider;");
                        if (!f_cp) f_cp = env->GetFieldID(worldBaseCls, "field_73012_v", "Lnet/minecraft/world/chunk/IChunkProvider;");
                        
                        jobject cp = f_cp ? env->GetObjectField(world, f_cp) : nullptr;
                        if (cp) {
                            jclass cpCls = env->GetObjectClass(cp);
                            jfieldID f_listing = env->GetFieldID(cpCls, "chunkListing", "Ljava/util/List;");
                            if (!f_listing) f_listing = env->GetFieldID(cpCls, "field_73239_b", "Ljava/util/List;");
                            
                            jobject listing = f_listing ? env->GetObjectField(cp, f_listing) : nullptr;
                            if (listing) {
                                jclass listCls = env->FindClass("java/util/List");
                                jmethodID m_size = env->GetMethodID(listCls, "size", "()I");
                                jmethodID m_get = env->GetMethodID(listCls, "get", "(I)Ljava/lang/Object;");
                                
                                int size = env->CallIntMethod(listing, m_size);
                                jclass chunkCls = lc->GetClass("net.minecraft.world.chunk.Chunk");
                                jfieldID f_cx = env->GetFieldID(chunkCls, "xPosition", "I");
                                jfieldID f_cz = env->GetFieldID(chunkCls, "zPosition", "I");

                                int chunksScanned = 0;
                                const int MAX_CHUNK_RADIUS = 10;

                                std::unordered_map<std::string, DetectedBed> snapshot;

                                for (int i = 0; i < size; i++) {
                                    jobject chunk = env->CallObjectMethod(listing, m_get, i);
                                    if (chunk) {
                                        int cx = env->GetIntField(chunk, f_cx);
                                        int cz = env->GetIntField(chunk, f_cz);
                                        
                                        if (std::abs(cx - playerCX) <= MAX_CHUNK_RADIUS && std::abs(cz - playerCZ) <= MAX_CHUNK_RADIUS) {
                                            scanChunkInto(cx, cz, snapshot);
                                            chunksScanned++;
                                        }
                                        env->DeleteLocalRef(chunk);
                                    }
                                }
                                {
                                    std::lock_guard<std::mutex> lock(m_bedMutex);
                                    m_beds = std::move(snapshot);
                                }

                                if (Config::isDebugging()) ChatSDK::showPrefixed("§7Scan Complete: §f" + std::to_string(chunksScanned) + "§7/§f" + std::to_string(size) + " §7chunks. Total Beds: §a" + std::to_string(m_beds.size()));
                                env->DeleteLocalRef(listing);
                            } else if (Config::isDebugging()) ChatSDK::showPrefixed("§cScan Fail: Listing not found.");
                            env->DeleteLocalRef(cp);
                            env->DeleteLocalRef(cpCls);
                        } else if (Config::isDebugging()) ChatSDK::showPrefixed("§cScan Fail: ChunkProvider not found.");
                        env->DeleteLocalRef(world);
                    }
                }
                env->DeleteLocalRef(player);
            }
            env->DeleteLocalRef(mcObj);
        }
    } catch (...) {}

    m_isScanning = false;
}
void BedDefenseManager::tick()
{
    if (!m_enabled) return;
    ULONGLONG now = GetTickCount64();
    
    static ULONGLONG lastNearbyScan = 0;
    if (now - lastNearbyScan >= 10000) {
        lastNearbyScan = now;
        forceScan();
    }

    if (now - m_lastRevalidation >= 3000) {
        m_lastRevalidation = now;
        std::lock_guard<std::mutex> lock(m_bedMutex);
        for (auto& pair : m_beds) {
            DetectedBed& bed = pair.second;
            if (bed.dirty || (now - bed.lastScan > 10000)) {
                bed.layers = selectBestLayers(bed);
                bed.dirty = false;
                bed.lastScan = now;
            }
        }
    }
}
} // namespace BedDefense
