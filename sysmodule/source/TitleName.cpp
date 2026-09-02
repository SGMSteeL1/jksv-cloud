#include "TitleName.hpp"

#include "Config.hpp"
#include "Log.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <json-c/json.h>
#include <memory>
#include <switch.h>
#include <unordered_map>
#include <unordered_set>

namespace
{
    Mutex g_titleMutex{};
    bool g_serviceReady{};
    std::unordered_map<std::uint64_t, std::string> g_names{};
    std::unordered_set<std::uint64_t> g_fallbackLogged{};

    std::string hex16(std::uint64_t value)
    {
        char text[17]{};
        std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
        return text;
    }

    std::string extract_name(NacpStruct &nacp) noexcept
    {
        NacpLanguageEntry *entry{};
        if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&nacp, &entry)) && entry && entry->name[0])
        {
            return entry->name;
        }

        // A seleção pela linguagem do sistema pode falhar em um processo
        // boot2 mesmo quando o NACP foi obtido corretamente. Nesse caso,
        // percorra diretamente todas as traduções e use a primeira disponível.
        for (NacpLanguageEntry &candidate : nacp.lang)
        {
            if (candidate.name[0]) { return candidate.name; }
        }
        return {};
    }

    std::string load_name(std::uint64_t titleId,
                          Result &resultOut,
                          std::size_t &actualSizeOut,
                          bool &usedV2Out) noexcept
    {
        resultOut = 0;
        actualSizeOut = 0;
        usedV2Out = false;
        if (!g_serviceReady) { return {}; }

        auto control = std::make_unique<NsApplicationControlData>();
        if (hosversionAtLeast(19, 0, 0))
        {
            u32 unknown{};
            usedV2Out = true;
            resultOut = nsGetApplicationControlData2(NsApplicationControlSource_Storage,
                                                     titleId,
                                                     control.get(),
                                                     sizeof(*control),
                                                     0xFF,
                                                     0,
                                                     &actualSizeOut,
                                                     &unknown);
            if (R_SUCCEEDED(resultOut))
            {
                std::string name = extract_name(control->nacp);
                if (!name.empty()) { return name; }
            }
        }

        // Mantenha a API tradicional como segunda fonte. Além da
        // compatibilidade, ela cobre instalações em que o comando novo existe,
        // mas não entrega o NACP para determinado tipo de conteúdo.
        std::memset(control.get(), 0, sizeof(*control));
        usedV2Out = false;
        actualSizeOut = 0;
        resultOut = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                titleId,
                                                control.get(),
                                                sizeof(*control),
                                                &actualSizeOut);
        if (R_FAILED(resultOut)) { return {}; }
        return extract_name(control->nacp);
    }

    bool read_mapped_value(json_object *titles, std::uint64_t titleId, std::string &result)
    {
        json_object *name{};
        const std::string key = hex16(titleId);
        if (!json_object_object_get_ex(titles, key.c_str(), &name) || !name ||
            !json_object_is_type(name, json_type_string))
        {
            return false;
        }
        const char *text = json_object_get_string(name);
        if (!text || !text[0] || key == text) { return false; }
        result = text;
        return true;
    }

    std::string load_mapped_name(std::uint64_t titleId, bool &mapAvailableOut) noexcept
    {
        mapAvailableOut = false;
        json_object *root = json_object_from_file(sync::TITLE_MAP_PATH);
        if (!root) { return {}; }
        json_object *titles{};
        std::string result{};
        if (json_object_object_get_ex(root, "titles", &titles) && titles &&
            json_object_is_type(titles, json_type_object))
        {
            mapAvailableOut = true;
            // The running program normally has the base application ID. Also
            // accept update/content IDs by matching their base application ID.
            if (!read_mapped_value(titles, titleId, result))
            {
                read_mapped_value(titles, titleId & ~0xFFFULL, result);
            }
        }
        json_object_put(root);
        return result;
    }

    std::string sanitize(std::string value, std::uint64_t titleId)
    {
        for (char &character : value)
        {
            const unsigned char byte = static_cast<unsigned char>(character);
            if (byte < 0x20 || character == '/' || character == '\\' || character == ':' ||
                character == '*' || character == '?' || character == '"' || character == '<' ||
                character == '>' || character == '|')
            {
                character = '_';
            }
        }
        while (!value.empty() && (value.front() == ' ' || value.front() == '.')) { value.erase(value.begin()); }
        while (!value.empty() && (value.back() == ' ' || value.back() == '.')) { value.pop_back(); }
        constexpr std::size_t MAX_BYTES = 96;
        if (value.size() > MAX_BYTES)
        {
            value.resize(MAX_BYTES);
            while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80)
            {
                value.pop_back();
            }
        }
        return value.empty() ? hex16(titleId) : value;
    }
}

void sync::titles::set_service_ready(bool ready) noexcept
{
    mutexLock(&g_titleMutex);
    g_serviceReady = ready;
    mutexUnlock(&g_titleMutex);
}

std::string sync::titles::display_name(std::uint64_t titleId) noexcept
{
    mutexLock(&g_titleMutex);
    const auto found = g_names.find(titleId);
    if (found != g_names.end())
    {
        const std::string result = found->second;
        mutexUnlock(&g_titleMutex);
        return result;
    }
    bool mapAvailable{};
    std::string name = load_mapped_name(titleId, mapAvailable);
    if (!name.empty())
    {
        sync::log::write("Resolved title %016llX as '%s' from the JKSV Cloud title map.",
                         static_cast<unsigned long long>(titleId),
                         name.c_str());
        g_names.emplace(titleId, name);
    }
    else
    {
        Result nsResult{};
        std::size_t actualSize{};
        bool usedV2{};
        name = load_name(titleId, nsResult, actualSize, usedV2);
        if (!name.empty())
        {
            sync::log::write("Resolved title %016llX as '%s' from Nintendo metadata.",
                             static_cast<unsigned long long>(titleId),
                             name.c_str());
            g_names.emplace(titleId, name);
        }
        else
        {
            // Do not cache an ID fallback: the updated homebrew may publish
            // title-map.json later, after which the very next event can resolve.
            name = hex16(titleId);
            if (g_fallbackLogged.insert(titleId).second)
            {
                if (R_FAILED(nsResult))
                {
                    sync::log::write(
                        "Title %016llX unresolved: title map %s; Nintendo metadata API %s failed: 0x%08X.",
                        static_cast<unsigned long long>(titleId),
                        mapAvailable ? "has no matching entry" : "is unavailable",
                        usedV2 ? "v2" : "legacy",
                        static_cast<unsigned int>(nsResult));
                }
                else
                {
                    sync::log::write(
                        "Title %016llX unresolved: title map %s; Nintendo metadata API %s returned %llu bytes without a name.",
                        static_cast<unsigned long long>(titleId),
                        mapAvailable ? "has no matching entry" : "is unavailable",
                        usedV2 ? "v2" : "legacy",
                        static_cast<unsigned long long>(actualSize));
                }
            }
        }
    }
    mutexUnlock(&g_titleMutex);
    return name;
}

std::string sync::titles::path_name(std::uint64_t titleId) noexcept
{
    return sanitize(display_name(titleId), titleId);
}
