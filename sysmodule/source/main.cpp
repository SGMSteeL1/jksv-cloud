#include "Config.hpp"
#include "Log.hpp"
#include "Status.hpp"
#include "Sync.hpp"
#include "TitleName.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <ctime>
#include <switch.h>
#include <vector>

namespace
{
    constexpr std::size_t INNER_HEAP_SIZE = 0x400000;
    constexpr int BOOT_SETTLE_SECONDS = 12;
    constexpr int SERVICE_RETRY_SECONDS = 10;
    constexpr int NETWORK_RETRY_SECONDS = 10;
    constexpr int IDLE_RETRY_SECONDS = 60;
    constexpr int UPLOAD_RETRY_SECONDS = 15;
    constexpr std::size_t UPLOAD_WORKER_STACK_SIZE = 0x20000;
    bool g_hosVersionReady{};
    bool g_fsInitialized{};
    bool g_sdMounted{};
    bool g_splInitialized{};
    bool g_timeInitialized{};
    bool g_nsInitialized{};
    bool g_pglInitialized{};
    bool g_pmdmntInitialized{};
    bool g_pminfoInitialized{};
    bool g_nifmInitialized{};
    bool g_socketInitialized{};
    std::atomic_bool g_gameRunning{};
    std::atomic_bool g_homebrewRunning{};
    alignas(0x1000) std::array<std::uint8_t, UPLOAD_WORKER_STACK_SIZE> g_uploadWorkerStack{};
    Thread g_uploadWorker{};

    void upload_worker(void *) noexcept
    {
        sync::log::write("Upload worker started.");
        while (true)
        {
            const sync::Settings settings = sync::load_settings();
            if (settings.enabled && settings.syncOnGameClose && !g_gameRunning.load() &&
                !g_homebrewRunning.load())
            {
                sync::retry_pending_uploads();
            }
            svcSleepThread(UPLOAD_RETRY_SECONDS * 1'000'000'000LL);
        }
    }
}

extern "C"
{
    u32 __nx_applet_type = AppletType_None;
    u32 __nx_fs_num_sessions = 4;
    TimeServiceType __nx_time_service_type = TimeServiceType_System;

    // boot2 sysmodules do not receive libnx's normal applet time glue. mbedTLS
    // calls the C time() symbol while validating X.509 validity periods, so
    // bridge it directly to Horizon's initialized time service.
    std::time_t time(std::time_t *destination)
    {
        u64 timestamp{};
        if (!g_timeInitialized || R_FAILED(timeGetCurrentTime(TimeType_Default, &timestamp)))
        {
            if (destination) { *destination = static_cast<std::time_t>(-1); }
            return static_cast<std::time_t>(-1);
        }
        const std::time_t value = static_cast<std::time_t>(timestamp);
        if (destination) { *destination = value; }
        return value;
    }
    void __libnx_initheap(void)
    {
        alignas(0x1000) static u8 innerHeap[INNER_HEAP_SIZE];
        extern void *fake_heap_start;
        extern void *fake_heap_end;
        fake_heap_start = innerHeap;
        fake_heap_end = innerHeap + sizeof(innerHeap);
    }

    void __appInit(void)
    {
        Result result = smInitialize();
        if (R_FAILED(result)) { return; }

        result = setsysInitialize();
        if (R_SUCCEEDED(result))
        {
            SetSysFirmwareVersion firmware{};
            if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware)))
            {
                hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro));
                g_hosVersionReady = true;
            }
            setsysExit();
        }

        g_fsInitialized = R_SUCCEEDED(fsInitialize());
        if (g_fsInitialized) { g_sdMounted = fsdevMountSdmc() == 0; }
        g_timeInitialized = R_SUCCEEDED(timeInitialize());

        smExit();
    }

    void __appExit(void)
    {
        if (g_socketInitialized) { socketExit(); }
        if (g_nifmInitialized) { nifmExit(); }
        if (g_nsInitialized) { nsExit(); }
        if (g_timeInitialized) { timeExit(); }
        if (g_pminfoInitialized) { pminfoExit(); }
        if (g_pmdmntInitialized) { pmdmntExit(); }
        if (g_pglInitialized) { pglExit(); }
        if (g_splInitialized) { splCryptoExit(); }
        if (g_sdMounted) { fsdevUnmountAll(); }
        if (g_fsInitialized) { fsExit(); }
    }
}

namespace
{
    bool monitoring_services_ready() noexcept
    {
        return g_sdMounted && (g_pmdmntInitialized || (g_pglInitialized && g_pminfoInitialized));
    }

    bool network_services_ready() noexcept
    {
        return g_nifmInitialized && g_socketInitialized && g_timeInitialized;
    }

    void publish_runtime_capabilities() noexcept
    {
        sync::set_runtime_capabilities(network_services_ready(), g_splInitialized);
    }

    void initialize_missing_services(bool includeNetwork) noexcept
    {
        const Result smResult = smInitialize();
        if (R_FAILED(smResult))
        {
            publish_runtime_capabilities();
            return;
        }

        if (!g_hosVersionReady)
        {
            if (R_SUCCEEDED(setsysInitialize()))
            {
                SetSysFirmwareVersion firmware{};
                if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware)))
                {
                    hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro));
                    g_hosVersionReady = true;
                }
                setsysExit();
            }
        }

        if (!g_fsInitialized) { g_fsInitialized = R_SUCCEEDED(fsInitialize()); }
        if (g_fsInitialized && !g_sdMounted) { g_sdMounted = fsdevMountSdmc() == 0; }
        if (!g_splInitialized) { g_splInitialized = R_SUCCEEDED(splCryptoInitialize()); }
        if (!g_nsInitialized)
        {
            const Result nsResult = nsInitialize();
            if (R_FAILED(nsResult)) { sync::log::result("Nintendo title metadata service", nsResult); }
            else { sync::log::write("Nintendo title metadata service ready."); }
            g_nsInitialized = R_SUCCEEDED(nsResult);
            sync::titles::set_service_ready(g_nsInitialized);
        }
        if (!g_timeInitialized)
        {
            const Result timeResult = timeInitialize();
            if (R_FAILED(timeResult)) { sync::log::result("System time service", timeResult); }
            else
            {
                u64 timestamp{};
                if (R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &timestamp)))
                {
                    sync::log::write("System time service ready: Unix %llu.",
                                     static_cast<unsigned long long>(timestamp));
                }
            }
            g_timeInitialized = R_SUCCEEDED(timeResult);
        }
        if (!g_pglInitialized) { g_pglInitialized = R_SUCCEEDED(pglInitialize()); }
        if (!g_pmdmntInitialized) { g_pmdmntInitialized = R_SUCCEEDED(pmdmntInitialize()); }
        if (!g_pminfoInitialized) { g_pminfoInitialized = R_SUCCEEDED(pminfoInitialize()); }

        if (includeNetwork)
        {
            if (!g_nifmInitialized)
            {
                Result nifmResult = nifmInitialize(NifmServiceType_System);
                if (R_FAILED(nifmResult))
                {
                    sync::log::result("NIFM system service", nifmResult);
                    nifmResult = nifmInitialize(NifmServiceType_User);
                    if (R_SUCCEEDED(nifmResult))
                    {
                        sync::log::write("NIFM ready through the user service fallback.");
                    }
                }
                else
                {
                    sync::log::write("NIFM system service ready.");
                }
                if (R_FAILED(nifmResult)) { sync::log::result("NIFM user service", nifmResult); }
                g_nifmInitialized = R_SUCCEEDED(nifmResult);
            }
            if (!g_socketInitialized)
            {
                SocketInitConfig socketConfig = *socketGetDefaultInitConfig();
                socketConfig.tcp_tx_buf_size = 0x20000;
                socketConfig.tcp_rx_buf_size = 0x20000;
                socketConfig.tcp_tx_buf_max_size = 0x40000;
                socketConfig.tcp_rx_buf_max_size = 0x40000;
                socketConfig.sb_efficiency = 1;
                socketConfig.num_bsd_sessions = 2;
                socketConfig.bsd_service_type = BsdServiceType_Auto;
                const Result socketResult = socketInitialize(&socketConfig);
                if (R_FAILED(socketResult)) { sync::log::result("BSD socket service", socketResult); }
                else { sync::log::write("BSD socket service ready."); }
                g_socketInitialized = R_SUCCEEDED(socketResult);
            }
        }

        smExit();
        publish_runtime_capabilities();
    }
}

int main(int, char **)
{
    // Boot2 processes can start before every service and the SD filesystem are ready.
    // Waiting and retrying here prevents a temporary boot dependency from becoming a fatal error.
    svcSleepThread(BOOT_SETTLE_SECONDS * 1'000'000'000LL);
    bool diagnosticsInitialized{};
    while (!monitoring_services_ready())
    {
        initialize_missing_services(false);
        if (g_sdMounted && !diagnosticsInitialized)
        {
            sync::log::initialize();
            sync::status::initialize();
            diagnosticsInitialized = true;
        }
        if (diagnosticsInitialized)
        {
            sync::status::set_phase(sync::status::Phase::Error);
            sync::status::heartbeat();
        }
        if (!monitoring_services_ready())
        {
            svcSleepThread(SERVICE_RETRY_SECONDS * 1'000'000'000LL);
        }
    }

    if (!diagnosticsInitialized)
    {
        sync::log::initialize();
        sync::status::initialize();
    }
    sync::log::write("Title resolver ready: map + NACP v2 + legacy + language scan.");
    if (g_timeInitialized)
    {
        u64 timestamp{};
        if (R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &timestamp)))
        {
            sync::log::write("System time bridge ready: Unix %llu.",
                             static_cast<unsigned long long>(timestamp));
        }
    }

    std::uint64_t activeTitle = sync::get_running_application();
    g_gameRunning.store(activeTitle != 0);
    g_homebrewRunning.store(sync::is_app_active());
    bool initialIdlePass = activeTitle == 0;
    int idleSeconds{};
    int networkRetrySeconds = NETWORK_RETRY_SECONDS;
    std::vector<std::uint64_t> pendingTitles{};

    const Result workerCreate = threadCreate(&g_uploadWorker,
                                             upload_worker,
                                             nullptr,
                                             g_uploadWorkerStack.data(),
                                             g_uploadWorkerStack.size(),
                                             45,
                                             3);
    if (R_FAILED(workerCreate))
    {
        sync::log::result("Upload worker creation", workerCreate);
    }
    else
    {
        const Result workerStart = threadStart(&g_uploadWorker);
        if (R_FAILED(workerStart)) { sync::log::result("Upload worker start", workerStart); }
    }

    const auto queue_title = [&pendingTitles](std::uint64_t titleId) {
        if (titleId != 0 &&
            std::find(pendingTitles.begin(), pendingTitles.end(), titleId) == pendingTitles.end())
        {
            pendingTitles.push_back(titleId);
        }
    };

    while (true)
    {
        const sync::Settings settings = sync::load_settings();
        if (!settings.enabled || !settings.syncOnGameClose)
        {
            sync::status::set_phase(sync::status::Phase::Disabled);
            sync::status::heartbeat();
            activeTitle = sync::get_running_application();
            g_gameRunning.store(activeTitle != 0);
            initialIdlePass = activeTitle == 0;
            idleSeconds = 0;
            pendingTitles.clear();
            svcSleepThread(5'000'000'000LL);
            continue;
        }

        if (!monitoring_services_ready())
        {
            sync::status::set_phase(sync::status::Phase::Error);
            initialize_missing_services(false);
            svcSleepThread(SERVICE_RETRY_SECONDS * 1'000'000'000LL);
            continue;
        }

        if (!network_services_ready() && networkRetrySeconds >= NETWORK_RETRY_SECONDS)
        {
            initialize_missing_services(true);
            if (!network_services_ready())
            {
                sync::log::write("Network services are not ready; backups will remain queued.");
            }
            networkRetrySeconds = 0;
        }

        const std::uint64_t runningTitle = sync::get_running_application();
        g_gameRunning.store(runningTitle != 0);
        g_homebrewRunning.store(sync::is_app_active());
        if (runningTitle != 0)
        {
            sync::status::set_phase(sync::status::Phase::Monitoring, runningTitle);
            if (runningTitle != activeTitle)
            {
                queue_title(activeTitle);
                activeTitle = runningTitle;
                sync::log::write("Monitoring title %016llX.",
                                 static_cast<unsigned long long>(activeTitle));
            }
            initialIdlePass = false;
            idleSeconds = 0;
        }
        else if (activeTitle != 0)
        {
            const std::uint64_t closedTitle = activeTitle;
            activeTitle = 0;
            queue_title(closedTitle);
            sync::log::write("Detected title %016llX close; waiting %d seconds before backup.",
                             static_cast<unsigned long long>(closedTitle),
                             settings.settleSeconds);
            sync::status::set_phase(sync::status::Phase::Waiting, closedTitle);
            svcSleepThread(static_cast<std::int64_t>(settings.settleSeconds) * 1'000'000'000LL);
            if (sync::get_running_application() == 0)
            {
                for (const std::uint64_t titleId : pendingTitles) { sync::synchronize_title(titleId); }
                pendingTitles.clear();
                idleSeconds = 0;
                sync::status::set_phase(sync::status::Phase::Idle);
            }
        }
        else if (initialIdlePass || idleSeconds >= IDLE_RETRY_SECONDS)
        {
            for (const std::uint64_t titleId : pendingTitles) { sync::synchronize_title(titleId); }
            pendingTitles.clear();
            initialIdlePass = false;
            idleSeconds = 0;
            sync::status::set_phase(sync::status::Phase::Idle);
        }
        else { sync::status::set_phase(sync::status::Phase::Idle); }

        sync::status::heartbeat();
        svcSleepThread(static_cast<std::int64_t>(settings.pollSeconds) * 1'000'000'000LL);
        networkRetrySeconds += settings.pollSeconds;
        if (activeTitle == 0) { idleSeconds += settings.pollSeconds; }
    }
    return 0;
}
