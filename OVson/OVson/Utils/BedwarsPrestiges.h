#pragma once
#include <string>

namespace BedwarsStars
{
    inline std::string GetFormattedLevel(int level)
    {
        std::string sLevel = std::to_string(level);
        std::string star = (level >= 1100 && level < 2100) ? "✪" : (level >= 2100 ? "⚝" : "✫");

        // ✪
        // ⚝
        // ✥
        
        if (level >= 3100) star = "✥";
        else if (level >= 2100) star = "⚝";
        else if (level >= 1100) star = "✪";
        
        if (level < 100) return "§7[" + sLevel + star + "]";
        if (level < 200) return "§f[" + sLevel + star + "]"; // Iron (White)
        if (level < 300) return "§6[" + sLevel + star + "]"; // Gold
        if (level < 400) return "§b[" + sLevel + star + "]"; // Diamond (Aqua)
        if (level < 500) return "§2[" + sLevel + star + "]"; // Emerald (Dark Green)
        if (level < 600) return "§3[" + sLevel + star + "]"; // Sapphire (Dark Aqua)
        if (level < 700) return "§4[" + sLevel + star + "]"; // Ruby (Dark Red)
        if (level < 800) return "§d[" + sLevel + star + "]"; // Crystal (Pink)
        if (level < 900) return "§9[" + sLevel + star + "]"; // Opal (Blue)
        if (level < 1000) return "§5[" + sLevel + star + "]"; // Amethyst (Purple)
        
        auto format = [&](const std::string& b1, const std::string& d1, const std::string& d2, const std::string& d3, const std::string& d4, const std::string& st, const std::string& b2) {
            std::string s = sLevel;
            while(s.length() < 4) s = "0" + s; 
            return b1 + "[" + d1 + s.substr(0,1) + d2 + s.substr(1,1) + d3 + s.substr(2,1) + d4 + s.substr(3) + st + star + b2 + "]";
        };

        // 1000: Rainbow (c, 6, e, a, b, d, 5)
        if (level < 1100) return format("§c", "§6", "§e", "§a", "§b", "§d", "§5");

        // 1100: Iron Prime (7, f, f, f, f, 7, 7) - White/Gray
        if (level < 1200) return format("§7", "§f", "§f", "§f", "§f", "§7", "§7");

        // 1200: Gold Prime (7, e, 6, 6, e, 6, 7) - Mostly Gold/Yellow
        if (level < 1300) return format("§7", "§e", "§6", "§6", "§e", "§6", "§7");

        // 1300: Diamond Prime (7, b, 3, 3, b, 3, 7)
        if (level < 1400) return format("§7", "§b", "§3", "§3", "§b", "§3", "§7");

        // 1400: Emerald Prime (7, a, 2, 2, a, 2, 7)
        if (level < 1500) return format("§7", "§a", "§2", "§2", "§a", "§2", "§7");

        // 1500: Sapphire Prime (7, 3, 9, 9, 3, 9, 7)
        if (level < 1600) return format("§7", "§3", "§9", "§9", "§3", "§9", "§7");

        // 1600: Ruby Prime (7, c, 4, 4, c, 4, 7)
        if (level < 1700) return format("§7", "§c", "§4", "§4", "§c", "§4", "§7");

        // 1700: Crystal Prime (7, d, 5, 5, d, 5, 7)
        if (level < 1800) return format("§7", "§d", "§5", "§5", "§d", "§5", "§7");

        // 1800: Opal Prime (7, 9, 1, 1, 9, 1, 7)
        if (level < 1900) return format("§7", "§9", "§1", "§1", "§9", "§1", "§7");

        // 1900: Amethyst Prime (7, 5, 8, 8, 5, 8, 7)
        if (level < 2000) return format("§7", "§5", "§8", "§8", "§5", "§8", "§7");
        
        // 2000: Mirror (8, 7, f, f, 7, 8, 8)
        if (level < 2100) return format("§8", "§7", "§f", "§f", "§7", "§8", "§8");

        // 2100: Light (7, f, e, 6, 6, 6, 7)
        if (level < 2200) return format("§f", "§f", "§e", "§6", "§6", "§6", "§7"); // approx
        
        // 2200: Dawn (Orange/Teal?) HTML: 2 is Gray, 2 is White, 0 is Teal?
        if (level < 2300) return format("§7", "§f", "§f", "§3", "§3", "§3", "§7");
        
        // 2300 Dusk: 5, d, 6, e, e 
        if(level < 2400) return format("§5", "§5", "§d", "§6", "§e", "§e", "§6");
        
        // 2400 Air: b, f, f, 8, 8
        if(level < 2500) return format("§b", "§b", "§f", "§f", "§8", "§8", "§8");

        // 2500 Wind: f, a, a, 2, 2
        if(level < 2600) return format("§f", "§f", "§a", "§a", "§2", "§2", "§8");

        // 2600 Nebula: 4, c, c, d, d
        if(level < 2700) return format("§4", "§4", "§c", "§c", "§d", "§d", "§5");

        // 2700 Thunder: e, f, f, 8, 8
        if(level < 2800) return format("§e", "§e", "§f", "§f", "§8", "§8", "§8");

        // 2800 Earth: a, 2, 2, 6, e
        if(level < 2900) return format("§a", "§a", "§2", "§2", "§6", "§e", "§a");

        // 2900 Water: b, 3, 3, 9, 1
        if(level < 3000) return format("§b", "§b", "§3", "§3", "§9", "§1", "§9");

        // 3000 Fire: e, 6, 6, c, 4
        if(level < 3100) return format("§e", "§6", "§6", "§c", "§4", "§c", "§c");

        // 3100: 9, 3, 6, e
        if(level < 3200) return format("§9", "§3", "§6", "§e", "§e", "§6", "§e");

        // 3200: c, 4, 7, 4, c
        if(level < 3300) return format("§c", "§4", "§7", "§4", "§c", "§4", "§c");

        // 3300: 9, d, c, 4
        if(level < 3400) return format("§9", "§9", "§d", "§c", "§4", "§4", "§4");

        // 3400: 2, a, d, 5, 2
        if(level < 3500) return format("§2", "§a", "§d", "§5", "§2", "§2", "§2");

        // 3500: c, 4, 2, a
        if(level < 3600) return format("§c", "§4", "§2", "§a", "§a", "§a", "§a");

        // 3600: a, b, 9, 1
        if(level < 3700) return format("§a", "§a", "§b", "§9", "§1", "§1", "§1");
        
        // 3700: 4, c, b, 3
        if(level < 3800) return format("§4", "§c", "§b", "§3", "§3", "§3", "§3");

        // 3800: 1, 9, 5, d, 1
        if(level < 3900) return format("§1", "§9", "§5", "§d", "§1", "§1", "§1");

        // 3900: c, a, 3, 9
        if(level < 4000) return format("§c", "§a", "§3", "§9", "§1", "§9", "§1");

        // 4000: 5, c, 6, e
        if(level < 4100) return format("§5", "§c", "§6", "§e", "§e", "§6", "§e");

        // 4100: e, 6, c, d, 5
        if(level < 4200) return format("§e", "§6", "§c", "§d", "§5", "§5", "§5");

        // 4200: 1, 5, 3, b, ?
        if(level < 4300) return format("§1", "§9", "§3", "§b", "§f", "§7", "§1");

        // 4300: 0, 5, 8, 5, 0
        if(level < 4400) return format("§0", "§5", "§8", "§5", "§0", "§5", "§0");

        // 4400: 2, a, e, 6, 5
        if(level < 4500) return format("§2", "§a", "§e", "§6", "§5", "§d", "§2");

        // 4500: f, b, b, 3, 3
        if(level < 4600) return format("§f", "§b", "§b", "§3", "§3", "§3", "§3");

        // 4600: 3, b, e, 6, 5
        if(level < 4700) return format("§3", "§b", "§e", "§6", "§d", "§5", "§5");

        // 4700: f, 4, c, 1, 9
        if(level < 4800) return format("§f", "§4", "§c", "§9", "§1", "§9", "§1");

        // 4800: 5, c, 6, e, b
        if(level < 4900) return format("§5", "§c", "§6", "§e", "§b", "§3", "§3");

        // 4900: 0, 2, a, f, 8
        if(level < 5000) return format("§0", "§2", "§a", "§a", "§f", "§7", "§8");

        // 5000: 4, 5, 9, 1
        return format("§4", "§5", "§9", "§1", "§1", "§1", "§1");
    }
}
