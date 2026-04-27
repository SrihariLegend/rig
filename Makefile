CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS += -I src -I deps -I deps/lua5.4 -I deps/libyaml -I deps/md4c
CFLAGS += -DLUA_USE_POSIX -DHAVE_CONFIG_H
LDFLAGS = -lcurl -lssl -lcrypto -lm -ldl -lpthread -lz

DEPS_SRC = deps/cjson/cJSON.c deps/md4c/md4c.c

LUA_SRC = $(filter-out deps/lua5.4/lua.c deps/lua5.4/luac.c deps/lua5.4/onelua.c, $(wildcard deps/lua5.4/*.c))

YAML_SRC = $(wildcard deps/libyaml/*.c)

DEPS_OBJ = $(DEPS_SRC:.c=.o) $(LUA_SRC:.c=.o) $(YAML_SRC:.c=.o)

UTIL_SRC = src/util/arena.c src/util/str.c src/util/hashmap.c \
           src/util/json.c src/util/http.c src/util/fs.c src/util/process.c \
           src/util/log.c
UTIL_OBJ = $(UTIL_SRC:.c=.o)

AI_SRC = src/ai/types.c src/ai/registry.c src/ai/validation.c \
         src/ai/json_parse.c src/ai/transform.c src/ai/models.c \
         src/ai/providers/anthropic.c src/ai/providers/openai.c \
         src/ai/providers/google.c src/ai/providers/bedrock.c \
         src/ai/providers/mistral.c src/ai/providers/sigv4.c \
         src/ai/providers/aws_eventstream.c
AI_OBJ = $(AI_SRC:.c=.o)

AGENT_SRC = src/agent/agent.c
AGENT_OBJ = $(AGENT_SRC:.c=.o)

HARNESS_SRC = src/harness/config.c src/harness/auth.c src/harness/system_prompt.c \
              src/harness/session.c src/harness/skills.c src/harness/prompts.c \
              src/harness/themes.c src/harness/packages.c src/harness/settings.c \
              src/harness/slash_commands.c src/harness/output_guard.c \
              src/harness/path_sandbox.c src/harness/permissions.c \
              src/harness/signals.c \
              src/harness/tools/bash_tool.c src/harness/tools/read_tool.c \
              src/harness/tools/write_tool.c src/harness/tools/edit_tool.c \
              src/harness/tools/grep_tool.c src/harness/tools/ls_tool.c \
              src/harness/tools/introspect_tool.c \
              src/harness/modes/print.c src/harness/modes/interactive.c \
              src/harness/model_registry.c src/harness/compaction.c \
              src/harness/export.c src/harness/migrations.c \
              src/harness/turnlog.c
HARNESS_OBJ = $(HARNESS_SRC:.c=.o)

EXT_SRC = src/harness/extensions/hooks.c src/harness/extensions/event_bus.c \
          src/harness/extensions/extension.c src/harness/extensions/lua_ext.c
EXT_OBJ = $(EXT_SRC:.c=.o)

WORKFLOW_SRC = src/harness/workflow/workflow.c src/harness/workflow/expr.c
WORKFLOW_OBJ = $(WORKFLOW_SRC:.c=.o)

TUI_SRC = src/tui/ansi.c src/tui/keys.c src/tui/terminal.c \
          src/tui/lantern.c src/tui/lantern_render.c \
          src/tui/linestore.c src/tui/md_render.c
TUI_OBJ = $(TUI_SRC:.c=.o)

RPC_SRC = src/harness/modes/rpc.c
RPC_OBJ = $(RPC_SRC:.c=.o)

SDK_SRC = src/rig.c
SDK_OBJ = $(SDK_SRC:.c=.o)

ALL_OBJ = $(DEPS_OBJ) $(UTIL_OBJ) $(AI_OBJ) $(AGENT_OBJ) $(HARNESS_OBJ) \
           $(EXT_OBJ) $(WORKFLOW_OBJ) $(TUI_OBJ) $(RPC_OBJ) $(SDK_OBJ)

TEST_SRC = $(wildcard test/test_*.c)
TEST_BIN = $(TEST_SRC:.c=)

PREFIX ?= /usr/local

.PHONY: all clean test install

all: librig.a rig

librig.a: $(ALL_OBJ)
	ar rcs $@ $^

rig: src/main.o librig.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lrig $(LDFLAGS)

# Suppress warnings for vendored deps
deps/lua5.4/%.o: deps/lua5.4/%.c
	$(CC) -std=c11 -O2 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DLUA_USE_POSIX -I deps/lua5.4 -w -c -o $@ $<

deps/libyaml/%.o: deps/libyaml/%.c
	$(CC) -std=c11 -O2 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DHAVE_CONFIG_H -I deps/libyaml -w -c -o $@ $<

deps/md4c/%.o: deps/md4c/%.c
	$(CC) -std=c11 -O2 -g -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -I deps/md4c -w -c -o $@ $<

# Auto-generate header dependencies
DEPFLAGS = -MMD -MP
%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(ALL_OBJ:.o=.d) src/main.d

test/%: test/%.c librig.a
	$(CC) $(CFLAGS) -I test -o $@ $< -L. -lrig $(LDFLAGS)

test: $(TEST_BIN)
	@for t in $(TEST_BIN); do echo "Running $$t..."; ./$$t || exit 1; done

install: rig
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 rig $(DESTDIR)$(PREFIX)/bin/rig

clean:
	rm -f $(ALL_OBJ) src/main.o librig.a rig $(TEST_BIN)
