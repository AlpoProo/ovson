#include "Hypixel.h"
#include "../Net/Http.h"
#include "../Utils/Logger.h"
#include "../Config/Config.h"
#include <string>
#include <vector>
static std::string makeStr(const int *a, size_t n)
{
    std::string s;
    s.resize(n);
    for (size_t i = 0; i < n; ++i)
        s[i] = (char)a[i];
    return s;
}

static void secureZero(std::string &s)
{
    volatile char *p = s.empty() ? nullptr : &s[0];
    if (!p)
        return;
    for (size_t i = 0; i < s.size(); ++i)
        p[i] = 0;
}

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
    const int P_https[] = {104, 116, 116, 112, 115, 58, 47, 47};
    const int P_api[] = {97, 112, 105, 46};
    const int P_moj[] = {109, 111, 106, 97, 110, 103};
    const int P_dotcom[] = {46, 99, 111, 109};
    const int P_users[] = {47, 117, 115, 101, 114, 115, 47};
    const int P_prof[] = {112, 114, 111, 102, 105, 108, 101, 115, 47};
    const int P_mc[] = {109, 105, 110, 101, 99, 114, 97, 102, 116, 47};
    std::string url = makeStr(P_https, sizeof(P_https) / sizeof(P_https[0]));
    url += makeStr(P_api, sizeof(P_api) / sizeof(P_api[0]));
    url += makeStr(P_moj, sizeof(P_moj) / sizeof(P_moj[0]));
    url += makeStr(P_dotcom, sizeof(P_dotcom) / sizeof(P_dotcom[0]));
    url += makeStr(P_users, sizeof(P_users) / sizeof(P_users[0]));
    url += makeStr(P_prof, sizeof(P_prof) / sizeof(P_prof[0]));
    url += makeStr(P_mc, sizeof(P_mc) / sizeof(P_mc[0]));
    url += name;
    bool ok = Http::get(url, body);
    secureZero(url);
    if (!ok)
        return std::nullopt;
    std::string id;
    if (!findJsonString(body, "id", id))
        return std::nullopt;
    return id;
}

std::optional<Hypixel::PlayerStats> Hypixel::getPlayerStats(const std::string &apiKey, const std::string &uuid)
{
    std::string body;
    // Build obfuscated URL and header "API-Key"
    const int H_https[] = {104, 116, 116, 112, 115, 58, 47, 47};
    const int H_api[] = {97, 112, 105, 46};
    const int H_host[] = {104, 121, 112, 105, 120, 101, 108};
    const int H_dotnet[] = {46, 110, 101, 116};
    const int H_path[] = {47, 112, 108, 97, 121, 101, 114, 63, 117, 117, 105, 100, 61};
    std::string url = makeStr(H_https, sizeof(H_https) / sizeof(H_https[0]));
    url += makeStr(H_api, sizeof(H_api) / sizeof(H_api[0]));
    url += makeStr(H_host, sizeof(H_host) / sizeof(H_host[0]));
    url += makeStr(H_dotnet, sizeof(H_dotnet) / sizeof(H_dotnet[0]));
    url += makeStr(H_path, sizeof(H_path) / sizeof(H_path[0]));
    url += uuid;
    const int H_hdr[] = {65, 80, 73, 45, 75, 101, 121}; // "API-Key"
    std::string hdr = makeStr(H_hdr, sizeof(H_hdr) / sizeof(H_hdr[0]));
    bool ok = Http::get(url, body, hdr.c_str(), apiKey);
    secureZero(url);
    secureZero(hdr);
    if (!ok)
        return std::nullopt;
    PlayerStats ps;
    ps.uuid = uuid;
    findJsonString(body, "displayname", ps.displayName);
    int level = 0;
    if (findJsonInt(body, "networkLevel", level))
        ps.networkLevel = level;
    std::string::size_type pAch = body.find("\"achievements\"");
    if (pAch != std::string::npos)
    {
        int bwStar = 0;
        if (findJsonInt(body.substr(pAch), "bedwars_level", bwStar))
            ps.bedwarsStar = bwStar;
    }
    std::string::size_type pStats = body.find("\"stats\"");
    if (pStats != std::string::npos)
    {
        std::string::size_type pBw = body.find("\"Bedwars\"", pStats);
        if (pBw != std::string::npos)
        {
            int fk = 0, fd = 0, wins = 0, losses = 0;
            findJsonInt(body.substr(pBw), "final_kills_bedwars", fk);
            findJsonInt(body.substr(pBw), "final_deaths_bedwars", fd);
            findJsonInt(body.substr(pBw), "wins_bedwars", wins);
            findJsonInt(body.substr(pBw), "losses_bedwars", losses);
            ps.bedwarsFinalKills = fk;
            ps.bedwarsFinalDeaths = fd;
            ps.bedwarsWins = wins;
            ps.bedwarsLosses = losses;
        }
    }
    return ps;
}
