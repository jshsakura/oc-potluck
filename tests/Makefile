#---------------------------------------------------------------------------------
# potluck - Unit Tests Makefile
#---------------------------------------------------------------------------------

# Compiler settings (host, not Switch)
.DEFAULT_GOAL := all
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0
CXXFLAGS += -I../sysmodule/source
CXXFLAGS += -I../common
CXXFLAGS += -DTEST_BUILD
LDFLAGS += -no-pie

P2P_LIFECYCLE_SOURCES := p2p_lifecycle_tests.cpp ../sysmodule/source/p2p/p2p_proxy_server.cpp ../sysmodule/source/p2p/p2p_proxy_client.cpp ../sysmodule/source/p2p/p2p_packet_stream.cpp ../sysmodule/source/debug/log.cpp

run_p2p_lifecycle_tests: $(P2P_LIFECYCLE_SOURCES) host_switch/stratosphere.hpp ../sysmodule/source/p2p/p2p_proxy_server.hpp ../sysmodule/source/p2p/p2p_proxy_client.hpp ../sysmodule/source/p2p/p2p_socket_io.hpp
	$(CXX) $(CXXFLAGS) -std=c++20 -pthread -Ihost_switch $(P2P_LIFECYCLE_SOURCES) $(LDFLAGS) -o $@

run_server_probe_tests: server_probe_tests.cpp ../sysmodule/source/network/server_probe.hpp ../sysmodule/source/p2p/p2p_socket_io.hpp socket.o log.o
	$(CXX) $(CXXFLAGS) -pthread server_probe_tests.cpp socket.o log.o $(LDFLAGS) -o $@

.PHONY: test-p2p-lifecycle
test-p2p-lifecycle: run_p2p_lifecycle_tests run_server_probe_tests
	./run_p2p_lifecycle_tests tokens
	./run_p2p_lifecycle_tests disconnect
	./run_p2p_lifecycle_tests stop
	./run_p2p_lifecycle_tests silent
	./run_p2p_lifecycle_tests create-failure
	./run_p2p_lifecycle_tests client
	./run_p2p_lifecycle_tests client-invalid
	./run_p2p_lifecycle_tests routing
	./run_p2p_lifecycle_tests nat
	./run_server_probe_tests

test: test-p2p-lifecycle

run_runtime_stop_tests: runtime_stop_tests.cpp ../sysmodule/source/runtime/stop_policy.hpp
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

.PHONY: test-runtime-stop
test-runtime-stop: run_runtime_stop_tests
	./run_runtime_stop_tests

test: test-runtime-stop

PROXY_TCP_SOURCES := proxy_tcp_tests.cpp ../sysmodule/source/bsd/proxy_socket.cpp ../sysmodule/source/bsd/proxy_activity.cpp ../sysmodule/source/bsd/proxy_socket_manager.cpp ../sysmodule/source/bsd/ephemeral_port_pool.cpp ../sysmodule/source/debug/log.cpp

run_proxy_tcp_tests: $(PROXY_TCP_SOURCES) host_bsd/stratosphere.hpp ../sysmodule/source/bsd/proxy_socket.hpp ../sysmodule/source/bsd/proxy_socket_manager.hpp
	$(CXX) $(CXXFLAGS) -std=c++20 -pthread -Ihost_bsd $(PROXY_TCP_SOURCES) $(LDFLAGS) -o $@

.PHONY: test-proxy-tcp
test-proxy-tcp: run_proxy_tcp_tests
	./run_proxy_tcp_tests

test: test-proxy-tcp

run_poll_split_tests: poll_split_tests.cpp ../sysmodule/source/bsd/poll_split.hpp ../sysmodule/source/bsd/bsd_types.hpp
	$(CXX) $(CXXFLAGS) poll_split_tests.cpp $(LDFLAGS) -o $@

.PHONY: test-poll-split
test-poll-split: run_poll_split_tests
	./run_poll_split_tests

test: test-poll-split

# send is wrapped so the peer seems to take one byte every 10 ms
run_socket_deadline_tests: socket_deadline_tests.cpp ../sysmodule/source/network/socket.hpp socket.o log.o config.o config_text.o
	$(CXX) $(CXXFLAGS) -pthread socket_deadline_tests.cpp socket.o log.o config.o config_text.o $(LDFLAGS) -Wl,--wrap=send -o $@

.PHONY: test-socket-deadline
test-socket-deadline: run_socket_deadline_tests
	./run_socket_deadline_tests

test: test-socket-deadline

run_lan_protocol_tests: lan_protocol_tests.cpp ../sysmodule/source/lan/lan_protocol.cpp ../sysmodule/source/lan/lan_protocol.hpp
	$(CXX) $(CXXFLAGS) lan_protocol_tests.cpp ../sysmodule/source/lan/lan_protocol.cpp $(LDFLAGS) -o $@

.PHONY: test-lan-protocol
test-lan-protocol: run_lan_protocol_tests
	./run_lan_protocol_tests

test: test-lan-protocol

run_mode_latch_tests: mode_latch_tests.cpp ../sysmodule/source/ldn/mode_latch.hpp
	$(CXX) $(CXXFLAGS) mode_latch_tests.cpp $(LDFLAGS) -o $@

.PHONY: test-mode-latch
test-mode-latch: run_mode_latch_tests
	./run_mode_latch_tests

test: test-mode-latch

# Coverage flags (optional)
ifdef COVERAGE
CXXFLAGS += --coverage
LDFLAGS += --coverage
endif

#---------------------------------------------------------------------------------
# Source files
#---------------------------------------------------------------------------------
# Test sources
TEST_SOURCES := \
	protocol_tests.cpp \
	config_tests.cpp \
	config_manager_tests.cpp \
	log_tests.cpp \
	socket_tests.cpp \
	tcp_client_tests.cpp \
	connection_state_tests.cpp \
	reconnect_tests.cpp \
	client_tests.cpp \
	ldn_types_tests.cpp \
	ldn_state_machine_tests.cpp \
	ldn_proxy_tests.cpp \
	ldn_error_handling_tests.cpp \
	ldn_integration_tests.cpp \
	overlay_tests.cpp \
	ipc_config_tests.cpp \
	config_ipc_service_tests.cpp \
	shared_state_tests.cpp \
	ldn_packet_dispatcher_tests.cpp \
	ldn_session_handler_tests.cpp \
	ldn_proxy_handler_tests.cpp \
	ldn_handler_integration_tests.cpp \
	upnp_tests.cpp \
	p2p_proxy_tests.cpp \
	p2p_proxy_client_tests.cpp \
	p2p_integration_tests.cpp \
	p2p_create_network_tests.cpp

# Implementation sources needed for tests
IMPL_SOURCES := \
	../sysmodule/source/config/config.cpp \
	../sysmodule/source/config/config_text.cpp \
	../sysmodule/source/config/config_manager.cpp \
	../sysmodule/source/debug/log.cpp \
	../sysmodule/source/network/socket.cpp \
	../sysmodule/source/network/tcp_client.cpp \
	../sysmodule/source/network/relay_traffic.cpp \
	../sysmodule/source/network/connection_state.cpp \
	../sysmodule/source/network/reconnect.cpp \
	../sysmodule/source/network/client.cpp \
	../sysmodule/source/p2p/p2p_packet_stream.cpp

TEST_OBJECTS := $(TEST_SOURCES:.cpp=.o)
IMPL_OBJECTS := $(notdir $(IMPL_SOURCES:.cpp=.o))

# Targets
TARGET_PROTOCOL := run_protocol_tests
TARGET_CONFIG := run_config_tests
TARGET_CONFIG_MANAGER := run_config_manager_tests
TARGET_LOG := run_log_tests
TARGET_SOCKET := run_socket_tests
TARGET_TCP_CLIENT := run_tcp_client_tests
TARGET_CONNECTION_STATE := run_connection_state_tests
TARGET_RECONNECT := run_reconnect_tests
TARGET_CLIENT := run_client_tests
TARGET_LDN_TYPES := run_ldn_types_tests
TARGET_LDN_STATE_MACHINE := run_ldn_state_machine_tests
TARGET_LDN_PROXY := run_ldn_proxy_tests
TARGET_LDN_ERROR := run_ldn_error_handling_tests
TARGET_LDN_INTEGRATION := run_ldn_integration_tests
TARGET_OVERLAY := run_overlay_tests
TARGET_IPC_CONFIG := run_ipc_config_tests
TARGET_CONFIG_IPC_SERVICE := run_config_ipc_service_tests
TARGET_SHARED_STATE := run_shared_state_tests
TARGET_PACKET_DISPATCHER := run_packet_dispatcher_tests
TARGET_SESSION_HANDLER := run_session_handler_tests
TARGET_PROXY_HANDLER := run_proxy_handler_tests
TARGET_HANDLER_INTEGRATION := run_handler_integration_tests
TARGET_UPNP := run_upnp_tests
TARGET_P2P_PROXY := run_p2p_proxy_tests
TARGET_P2P_CLIENT := run_p2p_proxy_client_tests
TARGET_P2P_INTEGRATION := run_p2p_integration_tests
TARGET_P2P_CREATE_NETWORK := run_p2p_create_network_tests
TARGET_ALL := run_all_tests

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
.PHONY: all clean test test-protocol test-config test-config-manager test-log test-socket test-tcp-client test-connection-state test-reconnect test-client test-ldn-types test-ldn-state-machine test-ldn-proxy test-ldn-error test-ldn-integration test-overlay test-ipc-config test-config-ipc-service test-shared-state test-packet-dispatcher test-session-handler test-proxy-handler test-handler-integration test-upnp test-p2p-proxy test-p2p-client test-p2p-integration test-p2p-create-network coverage coverage-enforce coverage-full-enforce

all: $(TARGET_PROTOCOL) $(TARGET_CONFIG) $(TARGET_CONFIG_MANAGER) $(TARGET_LOG) $(TARGET_SOCKET) $(TARGET_TCP_CLIENT) $(TARGET_CONNECTION_STATE) $(TARGET_RECONNECT) $(TARGET_CLIENT) $(TARGET_LDN_TYPES) $(TARGET_LDN_STATE_MACHINE) $(TARGET_LDN_PROXY) $(TARGET_LDN_ERROR) $(TARGET_LDN_INTEGRATION) $(TARGET_OVERLAY) $(TARGET_IPC_CONFIG) $(TARGET_CONFIG_IPC_SERVICE) $(TARGET_SHARED_STATE) $(TARGET_PACKET_DISPATCHER) $(TARGET_SESSION_HANDLER) $(TARGET_PROXY_HANDLER) $(TARGET_HANDLER_INTEGRATION) $(TARGET_UPNP) $(TARGET_P2P_PROXY) $(TARGET_P2P_CLIENT) $(TARGET_P2P_INTEGRATION)

# Protocol tests (header-only, no impl needed)
$(TARGET_PROTOCOL): protocol_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Config tests (needs config.cpp impl)
$(TARGET_CONFIG): config_tests.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Config Manager tests (needs config.cpp and config_manager.cpp)
$(TARGET_CONFIG_MANAGER): config_manager_tests.o config_manager.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Log tests (needs log.cpp and config.cpp impl)
$(TARGET_LOG): log_tests.o log.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Socket tests (needs socket.cpp, log.cpp for logging)
$(TARGET_SOCKET): socket_tests.o socket.o log.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# TCP Client tests (needs socket.cpp, tcp_client.cpp, and log.cpp for logging)
$(TARGET_TCP_CLIENT): tcp_client_tests.o tcp_client.o relay_traffic.o socket.o log.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Connection State tests (needs connection_state.cpp)
$(TARGET_CONNECTION_STATE): connection_state_tests.o connection_state.o log.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Reconnect tests (needs reconnect.cpp)
$(TARGET_RECONNECT): reconnect_tests.o reconnect.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Client tests (needs all network modules and log.cpp for logging)
$(TARGET_CLIENT): client_tests.o client.o tcp_client.o relay_traffic.o socket.o connection_state.o reconnect.o config.o config_text.o log.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Types tests (header-only, no impl needed)
$(TARGET_LDN_TYPES): ldn_types_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN State Machine tests (standalone test implementation)
$(TARGET_LDN_STATE_MACHINE): ldn_state_machine_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Proxy tests (header-only, no impl needed)
$(TARGET_LDN_PROXY): ldn_proxy_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Error Handling tests (standalone test implementation)
$(TARGET_LDN_ERROR): ldn_error_handling_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Integration tests (standalone end-to-end tests)
$(TARGET_LDN_INTEGRATION): ldn_integration_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Overlay tests (mocked Switch environment)
$(TARGET_OVERLAY): overlay_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# IPC Config tests (extended config commands)
$(TARGET_IPC_CONFIG): ipc_config_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Config IPC Service tests (potluck standalone service)
$(TARGET_CONFIG_IPC_SERVICE): config_ipc_service_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# SharedState tests (standalone test implementation)
$(TARGET_SHARED_STATE): shared_state_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Packet Dispatcher tests (needs ldn_packet_dispatcher.cpp)
$(TARGET_PACKET_DISPATCHER): ldn_packet_dispatcher_tests.o ldn_packet_dispatcher.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Session Handler tests (needs ldn_session_handler.cpp)
$(TARGET_SESSION_HANDLER): ldn_session_handler_tests.o ldn_session_handler.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Proxy Handler tests (needs ldn_proxy_handler.cpp)
$(TARGET_PROXY_HANDLER): ldn_proxy_handler_tests.o ldn_proxy_handler.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Handler Integration tests (needs all handler implementations)
$(TARGET_HANDLER_INTEGRATION): ldn_handler_integration_tests.o ldn_packet_dispatcher.o ldn_session_handler.o ldn_proxy_handler.o
	$(CXX) $(LDFLAGS) -o $@ $^

# UPnP tests (standalone, no miniupnpc needed for mock tests)
$(TARGET_UPNP): upnp_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# P2P Proxy tests (links production P2P stream reassembly & session dispatch)
$(TARGET_P2P_PROXY): p2p_proxy_tests.o p2p_packet_stream.o log.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# P2P Proxy Client tests (links production P2P stream reassembly & client dispatch)
$(TARGET_P2P_CLIENT): p2p_proxy_client_tests.o p2p_packet_stream.o log.o config.o config_text.o
	$(CXX) $(LDFLAGS) -o $@ $^

# P2P Integration tests (Story 9.7 - HandleExternalProxy integration)
$(TARGET_P2P_INTEGRATION): p2p_integration_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# P2P CreateNetwork tests (Story 9.8 - CreateNetwork with P2P)
$(TARGET_P2P_CREATE_NETWORK): p2p_create_network_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Compile test sources
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile implementation sources
config.o: ../sysmodule/source/config/config.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

config_text.o: ../sysmodule/source/config/config_text.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

config_manager.o: ../sysmodule/source/config/config_manager.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

log.o: ../sysmodule/source/debug/log.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

socket.o: ../sysmodule/source/network/socket.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tcp_client.o: ../sysmodule/source/network/tcp_client.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

relay_traffic.o: ../sysmodule/source/network/relay_traffic.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

connection_state.o: ../sysmodule/source/network/connection_state.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

reconnect.o: ../sysmodule/source/network/reconnect.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

client.o: ../sysmodule/source/network/client.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

ldn_packet_dispatcher.o: ../sysmodule/source/ldn/ldn_packet_dispatcher.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

ldn_session_handler.o: ../sysmodule/source/ldn/ldn_session_handler.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

ldn_proxy_handler.o: ../sysmodule/source/ldn/ldn_proxy_handler.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

p2p_packet_stream.o: ../sysmodule/source/p2p/p2p_packet_stream.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Run all tests
test: $(TARGET_PROTOCOL) $(TARGET_CONFIG) $(TARGET_CONFIG_MANAGER) $(TARGET_LOG) $(TARGET_SOCKET) $(TARGET_TCP_CLIENT) $(TARGET_CONNECTION_STATE) $(TARGET_RECONNECT) $(TARGET_CLIENT) $(TARGET_LDN_TYPES) $(TARGET_LDN_STATE_MACHINE) $(TARGET_LDN_PROXY) $(TARGET_LDN_ERROR) $(TARGET_LDN_INTEGRATION) $(TARGET_OVERLAY) $(TARGET_IPC_CONFIG) $(TARGET_CONFIG_IPC_SERVICE) $(TARGET_SHARED_STATE) $(TARGET_PACKET_DISPATCHER) $(TARGET_SESSION_HANDLER) $(TARGET_PROXY_HANDLER) $(TARGET_HANDLER_INTEGRATION) $(TARGET_UPNP) $(TARGET_P2P_PROXY) $(TARGET_P2P_CLIENT) $(TARGET_P2P_INTEGRATION) $(TARGET_P2P_CREATE_NETWORK)
	@echo "=== Running Protocol Tests ==="
	./$(TARGET_PROTOCOL)
	@echo ""
	@echo "=== Running Config Tests ==="
	./$(TARGET_CONFIG)
	@echo ""
	@echo "=== Running Config Manager Tests ==="
	./$(TARGET_CONFIG_MANAGER)
	@echo ""
	@echo "=== Running Log Tests ==="
	./$(TARGET_LOG)
	@echo ""
	@echo "=== Running Socket Tests ==="
	./$(TARGET_SOCKET)
	@echo ""
	@echo "=== Running TcpClient Tests ==="
	./$(TARGET_TCP_CLIENT)
	@echo ""
	@echo "=== Running Connection State Tests ==="
	./$(TARGET_CONNECTION_STATE)
	@echo ""
	@echo "=== Running Reconnect Tests ==="
	./$(TARGET_RECONNECT)
	@echo ""
	@echo "=== Running Client Tests ==="
	./$(TARGET_CLIENT)
	@echo ""
	@echo "=== Running LDN Types Tests ==="
	./$(TARGET_LDN_TYPES)
	@echo ""
	@echo "=== Running LDN State Machine Tests ==="
	./$(TARGET_LDN_STATE_MACHINE)
	@echo ""
	@echo "=== Running LDN Proxy Tests ==="
	./$(TARGET_LDN_PROXY)
	@echo ""
	@echo "=== Running LDN Error Handling Tests ==="
	./$(TARGET_LDN_ERROR)
	@echo ""
	@echo "=== Running LDN Integration Tests ==="
	./$(TARGET_LDN_INTEGRATION)
	@echo ""
	@echo "=== Running Overlay Tests ==="
	./$(TARGET_OVERLAY)
	@echo ""
	@echo "=== Running IPC Config Tests ==="
	./$(TARGET_IPC_CONFIG)
	@echo ""
	@echo "=== Running Config IPC Service Tests ==="
	./$(TARGET_CONFIG_IPC_SERVICE)
	@echo ""
	@echo "=== Running SharedState Tests ==="
	./$(TARGET_SHARED_STATE)
	@echo ""
	@echo "=== Running Packet Dispatcher Tests ==="
	./$(TARGET_PACKET_DISPATCHER)
	@echo ""
	@echo "=== Running Session Handler Tests ==="
	./$(TARGET_SESSION_HANDLER)
	@echo ""
	@echo "=== Running Proxy Handler Tests ==="
	./$(TARGET_PROXY_HANDLER)
	@echo ""
	@echo "=== Running Handler Integration Tests ==="
	./$(TARGET_HANDLER_INTEGRATION)
	@echo ""
	@echo "=== Running UPnP Tests ==="
	./$(TARGET_UPNP)
	@echo ""
	@echo "=== Running P2P Proxy Tests ==="
	./$(TARGET_P2P_PROXY)
	@echo ""
	@echo "=== Running P2P Proxy Client Tests ==="
	./$(TARGET_P2P_CLIENT)
	@echo ""
	@echo "=== Running P2P Integration Tests ==="
	./$(TARGET_P2P_INTEGRATION)
	@echo ""
	@echo "=== Running P2P CreateNetwork Tests ==="
	./$(TARGET_P2P_CREATE_NETWORK)

test-protocol: $(TARGET_PROTOCOL)
	./$(TARGET_PROTOCOL)

test-config: $(TARGET_CONFIG)
	./$(TARGET_CONFIG)

test-config-manager: $(TARGET_CONFIG_MANAGER)
	./$(TARGET_CONFIG_MANAGER)

test-log: $(TARGET_LOG)
	./$(TARGET_LOG)

test-socket: $(TARGET_SOCKET)
	./$(TARGET_SOCKET)

test-tcp-client: $(TARGET_TCP_CLIENT)
	./$(TARGET_TCP_CLIENT)

test-connection-state: $(TARGET_CONNECTION_STATE)
	./$(TARGET_CONNECTION_STATE)

test-reconnect: $(TARGET_RECONNECT)
	./$(TARGET_RECONNECT)

test-client: $(TARGET_CLIENT)
	./$(TARGET_CLIENT)

test-ldn-types: $(TARGET_LDN_TYPES)
	./$(TARGET_LDN_TYPES)

test-ldn-state-machine: $(TARGET_LDN_STATE_MACHINE)
	./$(TARGET_LDN_STATE_MACHINE)

test-ldn-proxy: $(TARGET_LDN_PROXY)
	./$(TARGET_LDN_PROXY)

test-ldn-error: $(TARGET_LDN_ERROR)
	./$(TARGET_LDN_ERROR)

test-ldn-integration: $(TARGET_LDN_INTEGRATION)
	./$(TARGET_LDN_INTEGRATION)

test-overlay: $(TARGET_OVERLAY)
	./$(TARGET_OVERLAY)

test-ipc-config: $(TARGET_IPC_CONFIG)
	./$(TARGET_IPC_CONFIG)

test-config-ipc-service: $(TARGET_CONFIG_IPC_SERVICE)
	./$(TARGET_CONFIG_IPC_SERVICE)

test-shared-state: $(TARGET_SHARED_STATE)
	./$(TARGET_SHARED_STATE)

test-packet-dispatcher: $(TARGET_PACKET_DISPATCHER)
	./$(TARGET_PACKET_DISPATCHER)

test-session-handler: $(TARGET_SESSION_HANDLER)
	./$(TARGET_SESSION_HANDLER)

test-proxy-handler: $(TARGET_PROXY_HANDLER)
	./$(TARGET_PROXY_HANDLER)

test-handler-integration: $(TARGET_HANDLER_INTEGRATION)
	./$(TARGET_HANDLER_INTEGRATION)

test-upnp: $(TARGET_UPNP)
	./$(TARGET_UPNP)

test-p2p-proxy: $(TARGET_P2P_PROXY)
	./$(TARGET_P2P_PROXY)

test-p2p-client: $(TARGET_P2P_CLIENT)
	./$(TARGET_P2P_CLIENT)

test-p2p-integration: $(TARGET_P2P_INTEGRATION)
	./$(TARGET_P2P_INTEGRATION)

test-p2p-create-network: $(TARGET_P2P_CREATE_NETWORK)
	./$(TARGET_P2P_CREATE_NETWORK)

coverage: clean
	$(MAKE) COVERAGE=1 test
	gcov $(TEST_SOURCES)
	@echo "Coverage report generated"

# Enforce minimum coverage threshold (default 80%)
# Requires: gcovr (pip install gcovr)
# Usage: make COVERAGE=1 coverage-enforce
#        make COVERAGE=1 coverage-enforce MIN_COVERAGE=90
MIN_COVERAGE ?= 80

coverage-enforce: coverage
	@echo "=== Checking per-file coverage (minimum $(MIN_COVERAGE)%) ==="
	cd .. && python3 scripts/check-coverage.py --min $(MIN_COVERAGE) --root .

# Full coverage: unit tests + integration tests against Docker LDN server
# Builds everything with --coverage, runs unit tests, then fires up the
# Docker LdnServer and runs integration tests, finally reports per-file
# line coverage for the 5 network source files.
COV_NETWORK_SOURCES := \
	client.cpp \
	tcp_client.cpp \
	connection_state.cpp \
	reconnect.cpp \
	socket.cpp

coverage-full: clean
	@echo "=== Phase 1: Unit tests with coverage ==="
	$(MAKE) COVERAGE=1 test
	@echo ""
	@echo "=== Phase 2: Integration tests with coverage ==="
	$(MAKE) -C integration COV=1 clean all
	cd integration && bash prepare-compose.sh
	docker compose -f integration/docker-compose.yml up -d
	bash integration/wait-for-server.sh localhost 30456 60
	sleep 2
	INTEGRATION_COMPOSE_FILE="$$(pwd)/integration/docker-compose.yml" \
		./integration/run_integration_tests localhost 30456 ; \
		int_exit=$$? ; \
	echo "" ; \
	echo "=== Stopping LdnServer ===" ; \
	docker compose -f integration/docker-compose.yml down -v ; \
	exit $$int_exit

# Full coverage enforcement: unit + integration tests + per-file threshold check
# Requires: Docker (for LdnServer), gcovr (pip install gcovr)
# Usage: make coverage-full-enforce
#        make coverage-full-enforce MIN_COVERAGE=90
coverage-full-enforce: clean
	@echo "=== Phase 1: Unit tests with coverage ==="
	$(MAKE) COVERAGE=1 test
	@echo ""
	@echo "=== Phase 2: Integration tests with coverage ==="
	$(MAKE) -C integration COV=1 clean all
	cd integration && bash prepare-compose.sh
	docker compose -f integration/docker-compose.yml up -d
	bash integration/wait-for-server.sh localhost 30456 60
	sleep 2
	INTEGRATION_COMPOSE_FILE="$$(pwd)/integration/docker-compose.yml" \
		./integration/run_integration_tests localhost 30456 || true
	@echo ""
	@echo "=== Phase 3: Stopping LdnServer ==="
	docker compose -f integration/docker-compose.yml down -v
	@echo ""
	@echo "=== Phase 4: Checking per-file coverage (minimum $(MIN_COVERAGE)%) ==="
	cd .. && python3 scripts/check-coverage.py --min $(MIN_COVERAGE) --root .

clean:
	rm -f run_p2p_lifecycle_tests run_server_probe_tests
	rm -f run_runtime_stop_tests
	rm -f run_proxy_tcp_tests run_lan_protocol_tests run_mode_latch_tests
	rm -f *.o $(TARGET_PROTOCOL) $(TARGET_CONFIG) $(TARGET_CONFIG_MANAGER) $(TARGET_LOG) $(TARGET_SOCKET) $(TARGET_TCP_CLIENT) $(TARGET_CONNECTION_STATE) $(TARGET_RECONNECT) $(TARGET_CLIENT) $(TARGET_LDN_TYPES) $(TARGET_LDN_STATE_MACHINE) $(TARGET_LDN_PROXY) $(TARGET_LDN_ERROR) $(TARGET_LDN_INTEGRATION) $(TARGET_OVERLAY) $(TARGET_IPC_CONFIG) $(TARGET_CONFIG_IPC_SERVICE) $(TARGET_SHARED_STATE) $(TARGET_PACKET_DISPATCHER) $(TARGET_SESSION_HANDLER) $(TARGET_PROXY_HANDLER) $(TARGET_HANDLER_INTEGRATION) $(TARGET_UPNP) $(TARGET_P2P_PROXY) $(TARGET_P2P_CLIENT) $(TARGET_P2P_INTEGRATION) $(TARGET_P2P_CREATE_NETWORK)
	rm -f *.gcno *.gcda *.gcov

#---------------------------------------------------------------------------------
# Dependencies
#---------------------------------------------------------------------------------
protocol_tests.o: protocol_tests.cpp \
	../sysmodule/source/protocol/types.hpp \
	../sysmodule/source/protocol/relay_protocol.hpp \
	../sysmodule/source/protocol/packet_buffer.hpp

config_tests.o: config_tests.cpp \
	../sysmodule/source/config/config.hpp

config.o: ../sysmodule/source/config/config.cpp ../sysmodule/source/config/config_internal.hpp \
	../sysmodule/source/config/config.hpp

config_manager_tests.o: config_manager_tests.cpp \
	../sysmodule/source/config/config_manager.hpp \
	../sysmodule/source/config/config.hpp

config_manager.o: ../sysmodule/source/config/config_manager.cpp \
	../sysmodule/source/config/config_manager.hpp \
	../sysmodule/source/config/config.hpp

log_tests.o: log_tests.cpp \
	../sysmodule/source/debug/log.hpp \
	../sysmodule/source/config/config.hpp

log.o: ../sysmodule/source/debug/log.cpp \
	../sysmodule/source/debug/log.hpp \
	../sysmodule/source/config/config.hpp

socket_tests.o: socket_tests.cpp \
	../sysmodule/source/network/socket.hpp

socket.o: ../sysmodule/source/network/socket.cpp \
	../sysmodule/source/network/socket.hpp

tcp_client_tests.o: tcp_client_tests.cpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/socket.hpp \
	../sysmodule/source/protocol/relay_protocol.hpp \
	../sysmodule/source/protocol/packet_buffer.hpp

tcp_client.o: ../sysmodule/source/network/tcp_client.cpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/socket.hpp \
	../sysmodule/source/protocol/relay_protocol.hpp \
	../sysmodule/source/protocol/packet_buffer.hpp

connection_state_tests.o: connection_state_tests.cpp \
	../sysmodule/source/network/connection_state.hpp

connection_state.o: ../sysmodule/source/network/connection_state.cpp \
	../sysmodule/source/network/connection_state.hpp

reconnect_tests.o: reconnect_tests.cpp \
	../sysmodule/source/network/reconnect.hpp

reconnect.o: ../sysmodule/source/network/reconnect.cpp \
	../sysmodule/source/network/reconnect.hpp

client_tests.o: client_tests.cpp \
	../sysmodule/source/network/client.hpp \
	../sysmodule/source/network/itcp_client.hpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/connection_state.hpp \
	../sysmodule/source/network/reconnect.hpp \
	mock_tcp_client.hpp

client.o: ../sysmodule/source/network/client.cpp \
	../sysmodule/source/network/client.hpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/connection_state.hpp \
	../sysmodule/source/network/reconnect.hpp

ldn_types_tests.o: ldn_types_tests.cpp \
	../sysmodule/source/protocol/types.hpp

ldn_state_machine_tests.o: ldn_state_machine_tests.cpp

ldn_proxy_tests.o: ldn_proxy_tests.cpp \
	../sysmodule/source/protocol/types.hpp

ldn_error_handling_tests.o: ldn_error_handling_tests.cpp

ldn_integration_tests.o: ldn_integration_tests.cpp

overlay_tests.o: overlay_tests.cpp ../overlay/source/game_list.hpp ../overlay/source/ui_text.hpp ../overlay/source/keypad.hpp

ipc_config_tests.o: ipc_config_tests.cpp

config_ipc_service_tests.o: config_ipc_service_tests.cpp

p2p_proxy_tests.o: p2p_proxy_tests.cpp \
	../sysmodule/source/p2p/p2p_packet_stream.hpp \
	../sysmodule/source/protocol/types.hpp \
	../sysmodule/source/protocol/relay_protocol.hpp

p2p_proxy_client_tests.o: p2p_proxy_client_tests.cpp \
	../sysmodule/source/p2p/p2p_packet_stream.hpp \
	../sysmodule/source/protocol/types.hpp \
	../sysmodule/source/protocol/relay_protocol.hpp

p2p_packet_stream.o: ../sysmodule/source/p2p/p2p_packet_stream.cpp \
	../sysmodule/source/p2p/p2p_packet_stream.hpp \
	../sysmodule/source/protocol/types.hpp \
	../sysmodule/source/protocol/relay_protocol.hpp
