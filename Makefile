SHELL := /bin/zsh

ROOT_DIR := $(abspath .)
BUILD_DIR ?= $(ROOT_DIR)/build
CONFIG_DIR ?= $(ROOT_DIR)/resources/config
RUN_DIR ?= $(ROOT_DIR)/.run
PID_DIR := $(RUN_DIR)/pid
LOG_DIR := $(RUN_DIR)/log

SIGNAL_BIN := $(BUILD_DIR)/pj-signal-server
NET_TEST_BIN := $(BUILD_DIR)/pj-net-test
PEER_BIN := $(BUILD_DIR)/pj-peer-probe

LOCAL_SERVER_CONFIG ?= $(CONFIG_DIR)/local-server-public.conf
LOCAL_TESTER_CONFIG ?= $(CONFIG_DIR)/local-tester-public.conf
REMOTE_TESTER_CONFIG ?= $(CONFIG_DIR)/remote-tester-public.conf
NET_TEST_CONFIG ?= $(CONFIG_DIR)/net-test.conf
PEER_CONFIG ?= $(CONFIG_DIR)/peer-probe.conf

SERVER_PID_FILE := $(PID_DIR)/pj-signal-server.pid
SERVER_LOG_FILE := $(LOG_DIR)/pj-signal-server.log

.PHONY: help build clean dirs server-start server-stop server-status server-logs \
	net-test-local peer-local remote-test-hint ports-check clean-run

help:
	@echo "Targets:"
	@echo "  make build"
	@echo "  make clean"
	@echo "  make server-start"
	@echo "  make server-stop"
	@echo "  make server-status"
	@echo "  make server-logs"
	@echo "  make ports-check"
	@echo "  make net-test-local"
	@echo "  make peer-local"
	@echo "  make remote-test-hint"
	@echo ""
	@echo "Config overrides:"
	@echo "  LOCAL_SERVER_CONFIG=$(LOCAL_SERVER_CONFIG)"
	@echo "  LOCAL_TESTER_CONFIG=$(LOCAL_TESTER_CONFIG)"
	@echo "  NET_TEST_CONFIG=$(NET_TEST_CONFIG)"
	@echo "  PEER_CONFIG=$(PEER_CONFIG)"

build:
	cmake -S $(ROOT_DIR) -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j 4

clean: server-stop
	@rm -rf "$(BUILD_DIR)"
	@rm -rf "$(RUN_DIR)"
	@echo "removed build and runtime artifacts"

dirs:
	@mkdir -p $(PID_DIR)
	@mkdir -p $(LOG_DIR)

server-start: dirs
	@if [ ! -x "$(SIGNAL_BIN)" ]; then \
		echo "missing binary: $(SIGNAL_BIN)"; \
		echo "run: make build"; \
		exit 1; \
	fi
	@if [ -f "$(SERVER_PID_FILE)" ] && kill -0 "$$(cat "$(SERVER_PID_FILE)")" 2>/dev/null; then \
		echo "pj-signal-server already running with pid $$(cat "$(SERVER_PID_FILE)")"; \
		exit 0; \
	fi
	@rm -f "$(SERVER_PID_FILE)"
	@nohup "$(SIGNAL_BIN)" --config "$(LOCAL_SERVER_CONFIG)" >"$(SERVER_LOG_FILE)" 2>&1 </dev/null & echo $$! >"$(SERVER_PID_FILE)"
	@sleep 1
	@if [ -f "$(SERVER_PID_FILE)" ] && kill -0 "$$(cat "$(SERVER_PID_FILE)")" 2>/dev/null; then \
		echo "started pj-signal-server pid $$(cat "$(SERVER_PID_FILE)")"; \
		echo "log: $(SERVER_LOG_FILE)"; \
	else \
		echo "pj-signal-server failed to start"; \
		echo "log: $(SERVER_LOG_FILE)"; \
		rm -f "$(SERVER_PID_FILE)"; \
		tail -n 20 "$(SERVER_LOG_FILE)" 2>/dev/null || true; \
		exit 1; \
	fi

server-stop:
	@if [ ! -f "$(SERVER_PID_FILE)" ]; then \
		echo "pj-signal-server is not running"; \
		exit 0; \
	fi
	@if kill -0 "$$(cat "$(SERVER_PID_FILE)")" 2>/dev/null; then \
		kill "$$(cat "$(SERVER_PID_FILE)")"; \
		echo "stopped pj-signal-server pid $$(cat "$(SERVER_PID_FILE)")"; \
	else \
		echo "stale pid file found for pj-signal-server"; \
	fi
	@rm -f "$(SERVER_PID_FILE)"

server-status:
	@if [ -f "$(SERVER_PID_FILE)" ] && kill -0 "$$(cat "$(SERVER_PID_FILE)")" 2>/dev/null; then \
		echo "pj-signal-server running with pid $$(cat "$(SERVER_PID_FILE)")"; \
	else \
		echo "pj-signal-server is not running"; \
	fi

server-logs:
	@if [ -f "$(SERVER_LOG_FILE)" ]; then \
		tail -n 50 "$(SERVER_LOG_FILE)"; \
	else \
		echo "no server log file yet: $(SERVER_LOG_FILE)"; \
	fi

ports-check:
	@server_port="$$(awk -F= '/^[[:space:]]*server.port[[:space:]]*=/{gsub(/[[:space:]]/,"",$$2); print $$2; exit}' "$(LOCAL_SERVER_CONFIG)")"; \
	candidate_port="$$(awk -F= '/^[[:space:]]*server.candidate_exchange_port[[:space:]]*=/{gsub(/[[:space:]]/,"",$$2); print $$2; exit}' "$(LOCAL_SERVER_CONFIG)")"; \
	if [ -z "$$server_port" ] || [ -z "$$candidate_port" ]; then \
		echo "could not read server ports from $(LOCAL_SERVER_CONFIG)"; \
		exit 1; \
	fi; \
	echo "checking server port $$server_port"; \
	lsof -nP -iTCP:$$server_port -sTCP:LISTEN || true; \
	lsof -nP -iUDP:$$server_port || true; \
	echo "checking candidate exchange port $$candidate_port"; \
	lsof -nP -iTCP:$$candidate_port -sTCP:LISTEN || true

net-test-local:
	@if [ ! -x "$(NET_TEST_BIN)" ]; then \
		echo "missing binary: $(NET_TEST_BIN)"; \
		echo "run: make build"; \
		exit 1; \
	fi
	@"$(NET_TEST_BIN)" --config "$(LOCAL_TESTER_CONFIG)"

peer-local:
	@if [ ! -x "$(PEER_BIN)" ]; then \
		echo "missing binary: $(PEER_BIN)"; \
		echo "run: make build"; \
		exit 1; \
	fi
	@"$(PEER_BIN)" --config "$(PEER_CONFIG)"

remote-test-hint:
	@echo "Run this on the remote machine:"
	@echo "  ./build/pj-net-test --config resources/config/remote-tester-public.conf"
	@echo ""
	@echo "If needed, copy and edit:"
	@echo "  $(REMOTE_TESTER_CONFIG)"

clean-run:
	@rm -f "$(SERVER_PID_FILE)"
