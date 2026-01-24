#include "BlockHook.h"
#include "BedDefenseManager.h"
#include "../../Utils/Logger.h"

using namespace BedDefense;

BlockHook* BlockHook::s_instance = nullptr;

BlockHook::BlockHook()
    : m_enabled(false)
{
    Logger::log(Config::DebugCategory::BedDefense, "BlockHook initialized");
}

BlockHook::~BlockHook()
{
}

BlockHook* BlockHook::getInstance()
{
    if (!s_instance) {
        s_instance = new BlockHook();
    }
    return s_instance;
}

void BlockHook::destroy()
{
    if (s_instance) {
        delete s_instance;
        s_instance = nullptr;
    }
}

void BlockHook::enable()
{
    m_enabled = true;
    Logger::log(Config::DebugCategory::BedDefense, "BlockHook enabled");
}

void BlockHook::disable()
{
    m_enabled = false;
    Logger::log(Config::DebugCategory::BedDefense, "BlockHook disabled");
}

void BlockHook::onBlockChange(int x, int y, int z)
{
    if (!m_enabled) return;
    
    BedDefenseManager* manager = BedDefenseManager::getInstance();
    if (manager && manager->isEnabled()) {
        manager->onBlockChange(x, y, z);
    }
}
