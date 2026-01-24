#include "Hypixel.h"
#include "../Net/Http.h"
#include "../Utils/Logger.h"
#include "../Config/Config.h"
#include <string>
#include <vector>
#include <algorithm>

static bool findJsonString(const std::string &json, const char *key, std::string &out)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t k = json.find(pat);
    if (k == std::string::npos)
        return false;
    size_t q1 = json.find('"', json.find(':', k));
    if (q1 == std::string::npos)
        return false;
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return false;
    out = json.substr(q1 + 1, q2 - (q1 + 1));
    return true;
}

static bool findJsonInt(const std::string &json, const char *key, int &out)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t k = json.find(pat);
    if (k == std::string::npos)
        return false;
    size_t c = json.find(':', k);
    if (c == std::string::npos)
        return false;
    size_t end = c + 1;
    while (end < json.size() && (json[end] == ' ' || json[end] == '\t'))
        ++end;
    size_t e = end;
    while (e < json.size() && isdigit((unsigned char)json[e]))
        ++e;
    if (e == end)
        return false;
    out = atoi(json.substr(end, e - end).c_str());
    return true;
}

std::optional<std::string> Hypixel::getUuidByName(const std::string &name)
{
    std::string body;
    std::string url = "https://api.mojang.com/users/profiles/minecraft/" + name;
    
    if (!Http::get(url, body))
        return std::nullopt;
        
    std::string id;
    if (!findJsonString(body, "id", id))
        return std::nullopt;
        
    return id;
}

std::optional<Hypixel::PlayerStats> Hypixel::getPlayerStats(const std::string &apiKey, const std::string &uuid)
{
    std::string body;
    std::string url = "https://api.hypixel.net/player?uuid=" + uuid;
    
    if (!Http::get(url, body, "API-Key", apiKey))
        return std::nullopt;
        
    PlayerStats ps;
    ps.uuid = uuid;
    findJsonString(body, "displayname", ps.displayName);
    
    int level = 0;
    if (findJsonInt(body, "networkLevel", level))
        ps.networkLevel = level;
        
    size_t pAch = body.find("\"achievements\"");
    if (pAch != std::string::npos)
    {
        int bwStar = 0;
        if (findJsonInt(body.substr(pAch), "bedwars_level", bwStar))
            ps.bedwarsStar = bwStar;
    }
    
    size_t pStats = body.find("\"stats\"");
    if (pStats != std::string::npos)
    {
        size_t pBw = body.find("\"Bedwars\"", pStats);
        if (pBw != std::string::npos)
        {
            int fk = 0, fd = 0, wins = 0, losses = 0;
            std::string bwJson = body.substr(pBw);
            
            findJsonInt(bwJson, "final_kills_bedwars", fk);
            findJsonInt(bwJson, "final_deaths_bedwars", fd);
            findJsonInt(bwJson, "wins_bedwars", wins);
            findJsonInt(bwJson, "losses_bedwars", losses);
            
            ps.bedwarsFinalKills = fk;
            ps.bedwarsFinalDeaths = fd;
            ps.bedwarsWins = wins;
            ps.bedwarsLosses = losses;
        }
    }
    
    return ps;
}
