# SFS Super: the Super Synthesis library, ported to VCV Rack.
RACK_DIR ?= ../Rack-SDK

FLAGS +=
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*) NOTICE.md

include $(RACK_DIR)/plugin.mk

# `make install` comes from plugin.mk: it packages the plugin and copies it into
# Rack's plugins folder, where Rack unpacks it at the next launch.

# `make dev`: install with every module visible. Modules stay "hidden": true in
# plugin.json until they are ready, and Rack keeps hidden modules out of its
# browser entirely, so a plain `make install` shows nothing new. This installs
# a copy with the flags off and puts plugin.json back afterwards.
dev:
	cp plugin.json .plugin.json.src
	sed -i.tmp 's/"hidden": *true/"hidden": false/g' plugin.json && rm -f plugin.json.tmp
	$(MAKE) install; status=$$?; mv .plugin.json.src plugin.json; exit $$status

