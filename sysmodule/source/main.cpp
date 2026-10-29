/**
 * @file main.cpp
 * @brief potluck - Nintendo Switch LDN to Relay Server Bridge
 *
 * This sysmodule enables Nintendo Switch games to use the LDN relay
 * servers for online multiplayer, replacing the need for local wireless
 * or complex LAN play setups.
 *
 * Built on Atmosphere's libstratosphere framework.
 *
 * @copyright Copyright (c) 2026 potluck contributors
 * @license GPL-2.0-or-later
 */

#include "potluck_firmware.h"
#include <stratosphere.hpp>

extern "C" {
#include <switch/services/bsd.h>
}

#include "ldn/ldn_mitm_service.hpp"
#include "bsd/bsd_mitm_service.hpp"
#include "bsd/forward_wait.hpp"
#include "config/config.hpp"
#include "config/config_ipc_service.hpp"
#include "config/game_whitelist.hpp"
#include "debug/log.hpp"
#include "ipc_thread_budget.hpp"

namespace ams {

    namespace {

        // ====================================================================
        // Memory Configuration
        // ====================================================================

        /// Main malloc buffer size
        /// NOTE: Switch sysmodules share a tight PoolPartition_Applet budget on
        /// recent HOS firmwares (21.0.0+/23.x). Global operator new/delete and
        /// fs::SetAllocator are routed to mitm::g_heap_memory (384 KB ExpHeap),
        /// so g_malloc_buffer backs only C malloc/calloc/realloc (libnx socket
        /// table, dns_wrap, miniupnpc). 128 KB leaves ~120 KB usable after
        /// TlsHeapCentral metadata while saving 896 KB of static .bss.
        constexpr size_t MallocBufferSize = 128_KB;
        alignas(os::MemoryPageSize) constinit u8 g_malloc_buffer[MallocBufferSize];

        /// Socket buffer configuration
        // codeql[cpp/unused-static-function] — consteval, used at compile-time below
        consteval size_t GetLibnxBsdTransferMemorySize(const ::SocketInitConfig* config) {
            const u32 tcp_tx_buf_max_size = config->tcp_tx_buf_max_size != 0
                ? config->tcp_tx_buf_max_size : config->tcp_tx_buf_size;
            const u32 tcp_rx_buf_max_size = config->tcp_rx_buf_max_size != 0
                ? config->tcp_rx_buf_max_size : config->tcp_rx_buf_size;
            const u32 sum = tcp_tx_buf_max_size + tcp_rx_buf_max_size +
                            config->udp_tx_buf_size + config->udp_rx_buf_size;

            return static_cast<size_t>(config->sb_efficiency) * util::AlignUp(sum, os::MemoryPageSize);
        }

        /// Socket initialization configuration
        constexpr const ::SocketInitConfig LibnxSocketInitConfig = {
            .tcp_tx_buf_size     = 0x800,
            .tcp_rx_buf_size     = 0x1000,
            .tcp_tx_buf_max_size = 0x2000,
            .tcp_rx_buf_max_size = 0x2000,
            .udp_tx_buf_size     = 0x2000,
            .udp_rx_buf_size     = 0x2000,
            .sb_efficiency       = 4,
            // num_bsd_sessions = number of IPC sessions to bsd:s — i.e. the
            // max concurrency for blocking BSD calls. Each blocked recv()/
            // accept()/send() holds one session for the entire IPC round-trip.
            //
            // Worst-case host-side concurrency in P2P mode (LDN = 8 players
            // total = host + 7 joiners):
            //   - master TCP recv          : 1
            //   - P2pProxyServer accept    : 1
            //   - P2pProxyClient (loopback) recv : 1
            //   - 8 P2pProxySession recv   (1 loopback + 7 joiners)
            //   = 11 blocking calls + marge for setsockopt/bind/send.
            //
            // The default of 3 saturated as soon as the loopback session was
            // alive (master recv + accept + 2 loopback recvs = 4 already > 3),
            // and bsd:s on Switch returns errno=113 (EHOSTUNREACH-mapped
            // "no resources") on the 4th concurrent call instead of blocking
            // — which is what made the AcceptLoop spin endlessly.
            //
            // 14 is ConcurrencyLimitMax in libstratosphere
            // (socket_constants.hpp:35), the highest value the kernel will
            // accept; transfer-memory size is independent of this count, so
            // headroom is free.
            .num_bsd_sessions    = 14,
            // bsd:s (System) is required for privileged socket options like
            // IP_MULTICAST_TTL / IP_MULTICAST_IF / IP_ADD_MEMBERSHIP that
            // miniupnpc's upnpDiscover() relies on. bsd:u returned EPERM and
            // miniupnpc crashed instead of handling the error gracefully.
            .bsd_service_type    = BsdServiceType_System,
        };

        /// Socket transfer memory buffer
        alignas(os::MemoryPageSize) constinit u8 g_socket_tmem_buffer[
            GetLibnxBsdTransferMemorySize(std::addressof(LibnxSocketInitConfig))];

        /// BSD initialization configuration
        constexpr const ::BsdInitConfig LibnxBsdInitConfig = {
            .version             = 1,
            .tmem_buffer         = g_socket_tmem_buffer,
            .tmem_buffer_size    = sizeof(g_socket_tmem_buffer),
            .tcp_tx_buf_size     = LibnxSocketInitConfig.tcp_tx_buf_size,
            .tcp_rx_buf_size     = LibnxSocketInitConfig.tcp_rx_buf_size,
            .tcp_tx_buf_max_size = LibnxSocketInitConfig.tcp_tx_buf_max_size,
            .tcp_rx_buf_max_size = LibnxSocketInitConfig.tcp_rx_buf_max_size,
            .udp_tx_buf_size     = LibnxSocketInitConfig.udp_tx_buf_size,
            .udp_rx_buf_size     = LibnxSocketInitConfig.udp_rx_buf_size,
            .sb_efficiency       = LibnxSocketInitConfig.sb_efficiency,
        };

    }

    // ========================================================================
    // MITM Server Configuration
    // ========================================================================

    namespace mitm {

        /// Thread priority for the MITM service
        const s32 ThreadPriority = 6;

        /// Threads always processing requests; spares start on demand (ipc_thread_budget.hpp)
        const size_t TotalThreads = potluck::ipc_threads::BaseThreadCount;
        const size_t NumExtraThreads = TotalThreads - 1;

        /// Thread stack size
        // 32 KB: needed because miniupnpc's upnpDiscover() and minissdpc helpers
        // each allocate ~2 KB of stack frames; cumulative with IPC dispatch +
        // ICommunicationService::CreateNetwork → NatPunch → Discover, 16 KB
        // overflowed and DABRT'd on the very first call into upnpDiscover.
        const size_t ThreadStackSize = 0x8000;

        /// Thread stack
        alignas(os::MemoryPageSize) u8 g_thread_stack[ThreadStackSize];
        os::ThreadType g_thread;

        // Heap for dynamic allocations
        // NOTE: 384KB covers: game whitelist (~40KB), proxy socket receive queues
        // (up to ~45KB under lobby traffic with 900+ byte packets), pending-packet
        // buffer, ExpHeap overhead/fragmentation, and transient std::vector/std::deque
        // allocations. 96KB saturated under real gameplay traffic and caused DABRT
        // 0x101 on allocation failure.
        alignas(0x40) constinit u8 g_heap_memory[384_KB];
        constinit lmem::HeapHandle g_heap_handle;
        constinit bool g_heap_initialized;
        constinit os::SdkMutex g_heap_init_mutex;

        lmem::HeapHandle GetHeapHandle() {
            if (AMS_UNLIKELY(!g_heap_initialized)) {
                std::scoped_lock lk(g_heap_init_mutex);

                if (AMS_LIKELY(!g_heap_initialized)) {
                    g_heap_handle = lmem::CreateExpHeap(g_heap_memory, sizeof(g_heap_memory),
                                                        lmem::CreateOption_ThreadSafe);
                    g_heap_initialized = true;
                }
            }

            return g_heap_handle;
        }

        void* Allocate(size_t size) {
            return lmem::AllocateFromExpHeap(GetHeapHandle(), size);
        }

        size_t GetHeapAllocatableSize() {
            return lmem::GetExpHeapAllocatableSize(GetHeapHandle(), alignof(std::max_align_t));
        }

        void Deallocate(void* p, size_t size) {
            AMS_UNUSED(size);
            return lmem::FreeToExpHeap(GetHeapHandle(), p);
        }

        namespace {

            /// Server manager options
            struct LdnMitmManagerOptions {
                static constexpr size_t PointerBufferSize   = 0x1000;
                static constexpr size_t MaxDomains          = 0x10;
                static constexpr size_t MaxDomainObjects    = 0x100;
                static constexpr bool   CanDeferInvokeRequest = false;
                static constexpr bool   CanManageMitmServers  = true;
            };

            /// Maximum concurrent sessions
            /// Higher value needed when intercepting all applications for BSD MITM
            constexpr size_t MaxSessions = 16;

            /// Port indices for MITM services
            constexpr int PortIndex_LdnMitm = 0;
            constexpr int PortIndex_BsdMitm = 1;

            /// Custom server manager for MITM (2 ports: ldn:u and bsd:u)
            class ServerManager final : public sf::hipc::ServerManager<2, LdnMitmManagerOptions, MaxSessions> {
            private:
                virtual ams::Result OnNeedsToAccept(int port_index, Server* server) override;
            };

            ServerManager g_server_manager;

            // Global counters for session tracking
            static u32 g_ldn_session_counter = 0;
            static u32 g_bsd_session_counter = 0;

            Result ServerManager::OnNeedsToAccept(int port_index, Server* server) {
                LOG_INFO("OnNeedsToAccept: port_index=%d, server=%p", port_index, server);

                // Acknowledge the MITM session
                std::shared_ptr<::Service> fsrv;
                sm::MitmProcessInfo client_info;
                server->AcknowledgeMitmSession(std::addressof(fsrv), std::addressof(client_info));

                LOG_INFO("OnNeedsToAccept: Acknowledged session for pid=%lu, program_id=0x%016lx, fsrv=%p (handle=0x%x)",
                         client_info.process_id.value, client_info.program_id.value,
                         fsrv.get(), fsrv ? fsrv->session : 0);

                Result rc;
                switch (port_index) {
                    case PortIndex_LdnMitm:
                        {
                            u32 session_id = ++g_ldn_session_counter;
                            LOG_INFO("LDN MITM: Creating session #%u for pid=%lu", session_id, client_info.process_id.value);
                            // LDN MITM service (ldn:u)
                            rc = this->AcceptMitmImpl(
                                server,
                                sf::CreateSharedObjectEmplaced<
                                    mitm::ldn::ILdnMitMService,
                                    mitm::ldn::LdnMitMService>(decltype(fsrv)(fsrv), client_info),
                                fsrv);
                            LOG_INFO("LDN AcceptMitmImpl result: 0x%x (session #%u)", rc.GetValue(), session_id);
                        }
                        return rc;

                    case PortIndex_BsdMitm:
                        {
                            u32 session_id = ++g_bsd_session_counter;
                            LOG_INFO("BSD MITM: Creating session #%u for pid=%lu", session_id, client_info.process_id.value);
                            // BSD MITM service (bsd:u)
                            rc = this->AcceptMitmImpl(
                                server,
                                sf::CreateSharedObjectEmplaced<
                                    mitm::bsd::IBsdMitmService,
                                    mitm::bsd::BsdMitmService>(decltype(fsrv)(fsrv), client_info),
                                fsrv);
                            LOG_INFO("BSD AcceptMitmImpl result: 0x%x (session #%u)", rc.GetValue(), session_id);
                        }
                        return rc;

                    default:
                        AMS_ABORT("Unknown port index");
                }
            }

            // Extra threads for parallel request handling
            alignas(os::MemoryPageSize) u8 g_extra_thread_stacks[NumExtraThreads][ThreadStackSize];
            os::ThreadType g_extra_threads[NumExtraThreads];

            void LoopServerThread(void*) {
                g_server_manager.LoopProcess();
            }

            // ----------------------------------------------------------------
            // Spare IPC threads, started while games block on proxy sockets
            // ----------------------------------------------------------------
            namespace spare {

                enum SlotState : u32 { SlotFree, SlotRunning, SlotFinished };

                struct Slot {
                    os::ThreadType thread;
                    void* stack = nullptr;
                    std::atomic<u32> state{SlotFree};
                };

                /// Heap a spare may not dip into, for sockets and std containers
                constexpr size_t HeapReserve = 32_KB;

                constinit Slot g_slots[potluck::ipc_threads::MaxSpareThreadCount];
                constinit os::SdkMutex g_slots_mutex;
                constinit std::atomic<u32> g_running{TotalThreads};
                constinit std::atomic<u32> g_waiters{0};
                constinit std::atomic<bool> g_stop{false};

                /// Leave once the remaining threads keep one free for the blocked calls
                bool TryRetire() {
                    u32 running = g_running.load();
                    while (running > TotalThreads && g_waiters.load() + 1 < running) {
                        if (g_running.compare_exchange_weak(running, running - 1)) {
                            return true;
                        }
                    }
                    return false;
                }

                void LoopSpareThread(void* arg) {
                    auto* slot = static_cast<Slot*>(arg);
                    while (!g_stop.load()) {
                        g_server_manager.WaitAndProcess();
                        if (TryRetire()) {
                            break;
                        }
                    }
                    if (g_stop.load()) {
                        g_running.fetch_sub(1);
                    }
                    LOG_INFO("IPC spare thread exiting (%u threads, %u blocked waits)",
                             g_running.load(), g_waiters.load());
                    slot->state.store(SlotFinished);
                }

                /// Join finished spares and return their stacks; caller holds g_slots_mutex
                void ReapLocked() {
                    for (auto& slot : g_slots) {
                        if (slot.state.load() != SlotFinished) {
                            continue;
                        }
                        os::WaitThread(&slot.thread);
                        os::DestroyThread(&slot.thread);
                        Deallocate(slot.stack, ThreadStackSize);
                        slot.stack = nullptr;
                        slot.state.store(SlotFree);
                    }
                }

                void StartIfNeeded(uint16_t port) {
                    std::scoped_lock lk(g_slots_mutex);
                    ReapLocked();
                    // One thread must stay free beyond the blocked ones
                    if (g_stop.load() || g_running.load() > g_waiters.load()) {
                        return;
                    }

                    Slot* free_slot = nullptr;
                    for (auto& slot : g_slots) {
                        if (slot.state.load() == SlotFree) {
                            free_slot = &slot;
                            break;
                        }
                    }
                    if (free_slot == nullptr) {
                        LOG_WARN("IPC threads: all %u busy with %u blocked waits (port %u) and no spare left; "
                                 "other calls wait until data arrives", g_running.load(), g_waiters.load(), port);
                        return;
                    }
                    if (lmem::GetExpHeapAllocatableSize(GetHeapHandle(), os::ThreadStackAlignment) <
                        ThreadStackSize + HeapReserve) {
                        LOG_WARN("IPC threads: heap too low for a spare thread (port %u, %u blocked waits)",
                                 port, g_waiters.load());
                        return;
                    }
                    void* stack = lmem::AllocateFromExpHeap(GetHeapHandle(), ThreadStackSize,
                                                            os::ThreadStackAlignment);
                    if (stack == nullptr) {
                        LOG_WARN("IPC threads: spare stack allocation failed (port %u)", port);
                        return;
                    }
                    const s32 priority = os::GetThreadCurrentPriority(os::GetCurrentThread());
                    if (R_FAILED(os::CreateThread(&free_slot->thread, LoopSpareThread, free_slot, stack,
                                                  ThreadStackSize, priority))) {
                        Deallocate(stack, ThreadStackSize);
                        LOG_WARN("IPC threads: could not create a spare thread (port %u)", port);
                        return;
                    }
                    os::SetThreadNamePointer(&free_slot->thread, "potluck::Spare");
                    free_slot->stack = stack;
                    free_slot->state.store(SlotRunning);
                    g_running.fetch_add(1);
                    os::StartThread(&free_slot->thread);
                    LOG_INFO("IPC spare thread started: %u threads for %u blocked waits (port %u)",
                             g_running.load(), g_waiters.load(), port);
                }

                /// Before RequestStopProcessing: spares leave their loop
                void RequestStop() {
                    g_stop.store(true);
                }

                /// After the base threads stopped: join every spare and free its stack
                void JoinAll() {
                    std::scoped_lock lk(g_slots_mutex);
                    for (auto& slot : g_slots) {
                        if (slot.state.load() == SlotFree) {
                            continue;
                        }
                        os::WaitThread(&slot.thread);
                        os::DestroyThread(&slot.thread);
                        Deallocate(slot.stack, ThreadStackSize);
                        slot.stack = nullptr;
                        slot.state.store(SlotFree);
                    }
                }

            }

            void ProcessForServerOnAllThreads(void*) {
                // Initialize extra threads
                if constexpr (NumExtraThreads > 0) {
                    const s32 priority = os::GetThreadCurrentPriority(os::GetCurrentThread());
                    for (size_t i = 0; i < NumExtraThreads; i++) {
                        R_ABORT_UNLESS(os::CreateThread(g_extra_threads + i, LoopServerThread,
                                                         nullptr, g_extra_thread_stacks[i],
                                                         ThreadStackSize, priority));
                        os::SetThreadNamePointer(g_extra_threads + i, "potluck::Thread");
                    }
                }

                // Start extra threads
                if constexpr (NumExtraThreads > 0) {
                    for (size_t i = 0; i < NumExtraThreads; i++) {
                        os::StartThread(g_extra_threads + i);
                    }
                }

                // Loop this thread
                LoopServerThread(nullptr);

                // Spares saw g_stop with the same stop request
                spare::JoinAll();

                // Wait for extra threads to finish
                if constexpr (NumExtraThreads > 0) {
                    for (size_t i = 0; i < NumExtraThreads; i++) {
                        os::WaitThread(g_extra_threads + i);
                        os::DestroyThread(g_extra_threads + i);
                    }
                }
            }

        }

    }

    // ========================================================================
    // Configuration IPC Service (potluck)
    // ========================================================================

    namespace cfg {

        /// Thread priority for config service
        const s32 ThreadPriority = 10;

        /// Thread stack size
        const size_t ThreadStackSize = 0x2000;

        /// Thread stack
        alignas(os::MemoryPageSize) u8 g_thread_stack[ThreadStackSize];
        os::ThreadType g_thread;

        /// Server manager options for config service
        struct ConfigServerManagerOptions {
            static constexpr size_t PointerBufferSize   = 0x100;
            static constexpr size_t MaxDomains          = 0;
            static constexpr size_t MaxDomainObjects    = 0;
            static constexpr bool   CanDeferInvokeRequest = false;
            static constexpr bool   CanManageMitmServers  = false;
        };

        /// Maximum concurrent sessions for config service
        constexpr size_t MaxSessions = 2;

        /// Server manager for potluck service
        using ConfigServerManager = sf::hipc::ServerManager<1, ConfigServerManagerOptions, MaxSessions>;
        ConfigServerManager g_config_server_manager;

        /// Config service thread entry point
        void LoopConfigServerThread(void*) {
            g_config_server_manager.LoopProcess();
        }

        /// Log maintenance thread stack
        alignas(os::MemoryPageSize) u8 g_log_thread_stack[0x1000];
        os::ThreadType g_log_thread;

        /// Log idle check runs every this many maintenance ticks (2 seconds)
        constexpr u32 LogCheckTicks = 2000 / mitm::bsd::forward::SliceMs;

        /// Maintenance thread: reads the running game from pm for socket waits, closes the idle log
        void LoopLogMaintenanceThread(void*) {
            for (u32 tick = 1;; tick++) {
                mitm::bsd::forward::RefreshApplicationPid();
                svc::SleepThread(TimeSpan::FromMilliSeconds(mitm::bsd::forward::SliceMs).GetNanoSeconds());
                if (tick % LogCheckTicks == 0) {
                    potluck::debug::g_logger.check_idle_timeout();
                }
            }
        }

    }

    // ========================================================================
    // System Module Initialization
    // ========================================================================

    namespace mitm {
        bool StopRuntimeMitmWorkers() {
            spare::RequestStop();
            g_server_manager.RequestStopProcessing();
            const svc::Handle handle = g_thread.thread_impl->handle;
            s32 index = 0;
            const bool stopped = R_SUCCEEDED(svc::WaitSynchronization(&index, &handle, 1,
                                               TimeSpan::FromSeconds(2).GetNanoSeconds()));
            if (stopped) {
                os::WaitThread(&g_thread);
                os::DestroyThread(&g_thread);
            }
            return stopped;
        }
    }

    namespace init {

        void InitializeSystemModule() {
            // Initialize service manager connection
            R_ABORT_UNLESS(sm::Initialize());

            // Initialize filesystem
            fs::InitializeForSystem();
            fs::SetAllocator(mitm::Allocate, mitm::Deallocate);
            fs::SetEnabledAutoAbort(false);

            // Mount SD card for configuration
            R_ABORT_UNLESS(fs::MountSdCard("sdmc"));

            // Ensure config file exists: carry over the pre-rename settings, else defaults
            bool config_migrated = false;
            const auto config_ready = potluck::config::ensure_config_exists(
                potluck::config::CONFIG_PATH, potluck::config::LEGACY_CONFIG_PATH, &config_migrated);

            // Load configuration
            potluck::config::Config config = potluck::config::get_default_config();
            potluck::config::load_config(potluck::config::CONFIG_PATH, config);

            // A sysmodule has no console: logging on means writing the log file
            potluck::config::DebugConfig log_config = config.debug;
            log_config.log_to_file = log_config.enabled;
            potluck::debug::g_logger.init(log_config, potluck::config::LOG_PATH);
            LOG_INFO("potluck sysmodule starting");
            if (potluckFirmwareIsNewer(hosversionGet())) {
                LOG_WARN("Firmware %u.%u.%u is newer than this build supports (up to %s): update PotLuck",
                         HOSVER_MAJOR(hosversionGet()), HOSVER_MINOR(hosversionGet()), HOSVER_MICRO(hosversionGet()),
                         POTLUCK_MAX_FIRMWARE_TEXT);
            }
            LOG_INFO("Config loaded from %s", potluck::config::CONFIG_PATH);
            if (config_migrated) {
                LOG_INFO("Settings migrated from %s", potluck::config::LEGACY_CONFIG_PATH);
            }
            if (config_ready != potluck::config::ConfigResult::Success) {
                LOG_WARN("Config: could not read or create %s (%s), running on defaults without saving over it",
                         potluck::config::CONFIG_PATH, potluck::config::config_result_to_string(config_ready));
            }

            const auto server_check = potluck::config::validate_server_config(config.server);
            if (server_check.host_reset) {
                LOG_WARN("Config: [server] host is not a hostname or IPv4 address, using default %s",
                         potluck::config::DEFAULT_HOST);
            }
            if (server_check.port_reset) {
                LOG_WARN("Config: [server] port is missing or not 1-65535, using default %u",
                         potluck::config::DEFAULT_PORT);
            }

            // Load game whitelist from file (once at startup)
            potluck::config::LoadWhitelist();
            LOG_VERBOSE("Server: %s:%u, TLS: %s", config.server.host, config.server.port,
                        config.server.use_tls ? "enabled" : "disabled");

            // Initialize network services
            R_ABORT_UNLESS(nifmInitialize(NifmServiceType_Admin));
            R_ABORT_UNLESS(bsdInitialize(&LibnxBsdInitConfig,
                                          LibnxSocketInitConfig.num_bsd_sessions,
                                          LibnxSocketInitConfig.bsd_service_type));
            R_ABORT_UNLESS(socketInitialize(&LibnxSocketInitConfig));
        }

        void FinalizeSystemModule() {
            LOG_INFO("potluck sysmodule shutting down");

            // Flush logs first, then tear down in reverse init order:
            // socket → bsd → nifm. The socket layer must exit before BSD
            // because libnx socket cleanup closes file descriptors that the
            // BSD service still tracks.
            potluck::debug::g_logger.flush();
            socketExit();
            bsdExit();
            nifmExit();
            fs::Unmount("sdmc");
        }

        void Startup() {
            // Initialize the global malloc allocator
            init::InitializeAllocator(g_malloc_buffer, sizeof(g_malloc_buffer));
        }

    }

    // ========================================================================
    // Exit Handler (should never be called)
    // ========================================================================

    void NORETURN Exit(int rc) {
        AMS_UNUSED(rc);
        AMS_ABORT("Exit called by immortal process");
    }

    // ========================================================================
    // Main Entry Point
    // ========================================================================

    void Main() {
        // Initialize global configuration for IPC service
        potluck::ipc::InitializeConfig();

        // ====================================================================
        // Register potluck configuration service
        // ====================================================================
        LOG_INFO("Registering potluck config service");
        constexpr sm::ServiceName ConfigServiceName = sm::ServiceName::Encode("potluck");

        // Create the config service object and register it
        auto config_service = sf::CreateSharedObjectEmplaced<
            potluck::ipc::IConfigService,
            potluck::ipc::ConfigService>();

        R_ABORT_UNLESS(cfg::g_config_server_manager.RegisterObjectForServer(
            std::move(config_service), ConfigServiceName, cfg::MaxSessions));
        LOG_INFO("Config service potluck registered successfully");

        // Create config service thread
        R_ABORT_UNLESS(os::CreateThread(
            &cfg::g_thread,
            cfg::LoopConfigServerThread,
            nullptr,
            cfg::g_thread_stack,
            cfg::ThreadStackSize,
            cfg::ThreadPriority));

        os::SetThreadNamePointer(&cfg::g_thread, "potluck::CfgThread");
        os::StartThread(&cfg::g_thread);

        // Forwarded socket waits learn from pm, through the maintenance thread,
        // whether their game still runs; the session stays open for good
        if (R_FAILED(::pmdmntInitialize())) {
            LOG_ERROR("pm:dmnt unavailable; forwarded socket waits cannot see a game exit");
        }

        // Create the maintenance thread (pm for socket waits, log idle timeout)
        R_ABORT_UNLESS(os::CreateThread(
            &cfg::g_log_thread,
            cfg::LoopLogMaintenanceThread,
            nullptr,
            cfg::g_log_thread_stack,
            sizeof(cfg::g_log_thread_stack),
            cfg::ThreadPriority + 5));  // Lower priority than config service

        os::SetThreadNamePointer(&cfg::g_log_thread, "potluck::LogThread");
        os::StartThread(&cfg::g_log_thread);

        // ====================================================================
        // Register MITM services
        // ====================================================================

        // Off at boot: register nothing, so no game ever passes through PotLuck.
        // The config service stays up so the overlay can turn it back on.
        if (potluck::ipc::GetLdnMode() == potluck::config::LdnMode::Off) {
            LOG_INFO("Mode is off; MITM services are not registered until the next boot");
            potluck::ipc::g_runtime_phase.store(potluck::runtime::Phase::Disabled);
            os::WaitThread(&cfg::g_thread);
            return;
        }

        // Register ldn:u MITM service (port 0)
        LOG_INFO("Registering ldn:u MITM service");
        constexpr sm::ServiceName LdnMitmServiceName = sm::ServiceName::Encode("ldn:u");
        const Result ldn_rc = mitm::g_server_manager.RegisterMitmServer<
            mitm::ldn::LdnMitMService>(mitm::PortIndex_LdnMitm, LdnMitmServiceName);
        if (R_FAILED(ldn_rc)) {
            // Another module (ldn_mitm) already holds ldn:u. Aborting here
            // would stop the console at boot, so stay idle and let it work;
            // the overlay still answers through the config service.
            LOG_ERROR("ldn:u is held by another module (0x%x); PotLuck stays idle", ldn_rc.GetValue());
            potluck::ipc::g_runtime_phase.store(potluck::runtime::Phase::Yielded);
            os::WaitThread(&cfg::g_thread);
            return;
        }
        LOG_INFO("ldn:u MITM service registered successfully");

        // Register bsd:u MITM service (port 1)
        // This allows us to intercept game sockets that target LDN addresses (10.114.x.x)
        LOG_INFO("Registering bsd:u MITM service");
        constexpr sm::ServiceName BsdMitmServiceName = sm::ServiceName::Encode("bsd:u");
        const Result bsd_rc = mitm::g_server_manager.RegisterMitmServer<
            mitm::bsd::BsdMitmService>(mitm::PortIndex_BsdMitm, BsdMitmServiceName);
        if (R_FAILED(bsd_rc)) {
            // Local mode does not need it; remote rooms will not carry game traffic
            LOG_ERROR("bsd:u is held by another module (0x%x); only local mode will work", bsd_rc.GetValue());
        } else {
            LOG_INFO("bsd:u MITM service registered successfully");
        }

        // Create MITM processing thread
        R_ABORT_UNLESS(os::CreateThread(
            &mitm::g_thread,
            mitm::ProcessForServerOnAllThreads,
            nullptr,
            mitm::g_thread_stack,
            mitm::ThreadStackSize,
            mitm::ThreadPriority));

        os::SetThreadNamePointer(&mitm::g_thread, "potluck::MainThread");
        os::StartThread(&mitm::g_thread);
        potluck::ipc::g_runtime_phase.store(potluck::runtime::Phase::Ready);

        // Config owns the process lifetime; PrepareStop alone joins the MITM worker.
        os::WaitThread(&cfg::g_thread);
    }

}

// ============================================================================
// Custom Memory Allocators
// ============================================================================

void* operator new(size_t size) {
    return ams::mitm::Allocate(size);
}

void* operator new(size_t size, const std::nothrow_t&) {
    return ams::mitm::Allocate(size);
}

void operator delete(void* p) {
    return ams::mitm::Deallocate(p, 0);
}

void operator delete(void* p, size_t size) {
    return ams::mitm::Deallocate(p, size);
}

void* operator new[](size_t size) {
    return ams::mitm::Allocate(size);
}

void* operator new[](size_t size, const std::nothrow_t&) {
    return ams::mitm::Allocate(size);
}

void operator delete[](void* p) {
    return ams::mitm::Deallocate(p, 0);
}

void operator delete[](void* p, size_t size) {
    return ams::mitm::Deallocate(p, size);
}

// ============================================================================
// Secure Monitor Wrapper for HOS 23.0.0+ Firmware Compatibility
// ============================================================================
//
// Pre-built libstratosphere.a compiles ams::hos::InitializeVersionInternal()
// with hos::Version_Max = 22.1.0 (0x16010000). On HOS 23.0.0 / 23.0.1+,
// Exosphere's GetConfig(ExosphereApiVersion) returns 0x17000100 (major=23),
// causing InitializeVersionInternal() to trigger AMS_ABORT_UNLESS inside
// __appInit() before main() is ever reached (hanging boot2 with no log file).
// Clamping the reported TargetFirmware field to hos::Version_Max satisfies
// both InitializeVersionInternal() and CMIF service dispatch version checks.
extern "C" {

void __real_svcCallSecureMonitor(::SecmonArgs* args);

void __wrap_svcCallSecureMonitor(::SecmonArgs* args) {
    const uint64_t func_id = (args != nullptr) ? args->X[0] : 0;
    const uint64_t config_item = (args != nullptr) ? args->X[1] : 0;

    __real_svcCallSecureMonitor(args);

    constexpr uint64_t kSmcGetConfig = 0xC3000002ULL;
    constexpr uint64_t kExosphereApiVersion = 65000ULL;
    constexpr uint64_t kExosphereApproximateApiVersion = 65012ULL;

    if (args != nullptr && args->X[0] == 0 && func_id == kSmcGetConfig &&
        (config_item == kExosphereApiVersion || config_item == kExosphereApproximateApiVersion)) {
        const uint32_t max_fw = static_cast<uint32_t>(ams::hos::Version_Max);
        const uint32_t reported_fw = static_cast<uint32_t>(args->X[1] & 0xFFFFFFFFULL);
        if (reported_fw > max_fw) {
            args->X[1] = (args->X[1] & 0xFFFFFFFF00000000ULL) | static_cast<uint64_t>(max_fw);
        }
    }
}

}

namespace potluck::ipc_threads {

void OnUnboundedWaitBegin(uint16_t port) {
    const u32 waiting = ams::mitm::spare::g_waiters.fetch_add(1) + 1;
    if (ams::mitm::spare::g_running.load() <= waiting) {
        ams::mitm::spare::StartIfNeeded(port);
    }
}

void OnUnboundedWaitEnd() {
    ams::mitm::spare::g_waiters.fetch_sub(1);
}

}  // namespace potluck::ipc_threads
