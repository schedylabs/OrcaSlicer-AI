# OrcaSlicer — build em Linux.
#
#   make install   # instala deps do SO, compila tudo e gera o AppImage
#   make desktop   # cria ícone no lançador do Ubuntu
#   make run       # executa o binário
#   make clean     # remove build/

SHELL := /bin/bash
REPO  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BIN   := $(REPO)/build/src/OrcaSlicer
ICON_SRC      := $(REPO)/resources/images/OrcaSlicer_192px.png
DESKTOP_DIR   := $(HOME)/.local/share/applications
ICON_DIR      := $(HOME)/.local/share/icons/hicolor/192x192/apps
DESKTOP_FILE  := $(DESKTOP_DIR)/OrcaSlicer.desktop

.DEFAULT_GOAL := install
.PHONY: install build desktop run clean

install: build
	$(MAKE) desktop

build:
	./build_linux.sh -u
	./build_linux.sh -dsi

desktop:
	@set -e; \
	APPIMAGE=$$(ls $(REPO)/build/OrcaSlicer_*.AppImage 2>/dev/null | head -1); \
	WRAPPER=$(REPO)/build/package/orca-slicer; \
	if [ -x "$$WRAPPER" ]; then EXEC="$$WRAPPER %U"; \
	elif [ -n "$$APPIMAGE" ]; then EXEC="$$APPIMAGE %U"; \
	elif [ -x "$(BIN)" ]; then EXEC="$(BIN) %U"; \
	else echo "Nada compilado ainda. Rode 'make install' primeiro."; exit 1; fi; \
	mkdir -p "$(DESKTOP_DIR)" "$(ICON_DIR)"; \
	cp -f "$(ICON_SRC)" "$(ICON_DIR)/OrcaSlicer.png"; \
	printf '%s\n' \
		'[Desktop Entry]' \
		'Name=OrcaSlicer' \
		'GenericName=3D Printing Software' \
		'Icon=OrcaSlicer' \
		"Exec=$$EXEC" \
		'Terminal=false' \
		'Type=Application' \
		'MimeType=model/stl;model/3mf;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;model/step;' \
		'Categories=Graphics;3DGraphics;Engineering;' \
		'Keywords=3D;Printing;Slicer;gcode;stl;obj;amf;' \
		'StartupNotify=false' \
		'StartupWMClass=orca-slicer' \
		> "$(DESKTOP_FILE)"; \
	chmod +x "$(DESKTOP_FILE)"; \
	command -v update-desktop-database >/dev/null && update-desktop-database "$(DESKTOP_DIR)" || true; \
	command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f "$(HOME)/.local/share/icons/hicolor" 2>/dev/null || true; \
	echo "Ícone criado: $(DESKTOP_FILE)"; \
	echo "Exec: $$EXEC"

run:
	@[ -x "$(BIN)" ] || { echo "Rode 'make install' primeiro."; exit 1; }
	$(BIN)

clean:
	rm -rf build deps/build
