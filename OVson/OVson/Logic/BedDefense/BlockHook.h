#pragma once

namespace BedDefense
{
    class BlockHook
    {
    private:
        static BlockHook* s_instance;
        bool m_enabled;
        
        BlockHook();
        ~BlockHook();

    public:
        static BlockHook* getInstance();
        static void destroy();
        void enable();
        void disable();
        bool isEnabled() const { return m_enabled; }
        void onBlockChange(int x, int y, int z);

        BlockHook(const BlockHook&) = delete;
        BlockHook& operator=(const BlockHook&) = delete;
    };
}
