# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds OunceShell, VitaShell's Switch 2 port, with the Switch 2 devkit
# (https://github.com/ouncebrew/ounce-devkit). From the repository root:
#
#   make -f switch2/Makefile            ->  build/switch2/ounceshell-pro.nro
#
# or in the devkit's Docker image:
#
#   docker run --rm -v "$PWD:/src" -w /src ounce-devkit make -f switch2/Makefile
#
# It compiles VitaShell's portable sources and this port's platform layer
# (switch2/src), and links the theme, language and fallback font files in as
# binary objects, as upstream's CMakeLists.txt does. The devkit supplies
# libounce in libnx's place and OunceVK's EGL and OpenGL ES, which SDL's
# renderer uses; SDL 2 and the codecs are devkitPro's Switch portlibs.
DEVKITPRO ?= /opt/devkitpro
LIBOUNCE := $(DEVKITPRO)/libounce
PORTLIBS := $(DEVKITPRO)/portlibs/switch
PORTLIBS_SWITCH2 := $(DEVKITPRO)/portlibs/switch2
TOOLS := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
# The Switch 2 CPU flags (SWITCH2_CPU).
include $(LIBOUNCE)/switch2_cpu.mk
OUT := build/switch2

NAME := ounceshell-pro
TITLE := OunceShell Pro
AUTHOR := TheFloW (VitaShell), kurumuz
VERSION := 0.1.0-alpha
# The homebrew menu's icon, a 256x256 JPEG.
ICON := switch2/resources/icon.jpg

# Upstream sources that build for Switch 2 (see switch2/PORT.md for the
# files left out and why).
UPSTREAM_SOURCES := \
	main.c main_context.c browser.c utils.c file.c \
	archive.c photo.c audioplayer.c text.c hex.c \
	settings.c config.c language.c theme.c \
	uncommon_dialog.c message_dialog.c property_dialog.c \
	context_menu.c io_process.c makezip.c sfo.c bm.c \
	strnatcmp.c sha1.c \
	minizip/zip.c minizip/ioapi.c \
	audio/player.c audio/id3.c audio/oggplayer.c \
	audio/mp3player.c audio/mp3xing.c audio/lrcparse.c \
	libmad/bit.c libmad/decoder.c libmad/fixed.c libmad/frame.c \
	libmad/huffman.c libmad/layer12.c libmad/layer3.c \
	libmad/stream.c libmad/synth.c libmad/timer.c

# This port's platform layer, its replacements for init.c, ime_dialog.c and
# audio/vita_audio.c, and the FTP server VitaShell starts with SELECT
# (ftpd's, vendored, serving the SD card at its root).
PORT_SOURCES := \
	switch2/src/psp2_io.c switch2/src/psp2_kernel.c \
	switch2/src/psp2_rtc.c switch2/src/psp2_ctrl.c \
	switch2/src/psp2_misc.c switch2/src/vita2d_sdl.c \
	switch2/src/vita2d_font.c switch2/src/ime_dialog.c \
	switch2/src/vita_audio_sdl.c switch2/src/init.c \
	switch2/src/dropped_features.c switch2/src/psp2_net.c \
	switch2/src/ftp_server.c switch2/third_party/ftpd/ftp.c

# Linked in as _binary_<path>_start/_end, paths relative to the root.
RESOURCES := \
	resources/english_us.txt resources/theme.txt \
	resources/default/colors.txt \
	switch2/resources/NotoSans-Regular.ttf \
	$(sort $(wildcard resources/default/*.png))

# gnu11: VitaShell predates C23, where bool, true and false are keywords.
# Unsigned char, as on the Vita (and AArch64's default): the text viewer
# indexes a width table with characters. The warning switches are
# upstream's (CMakeLists.txt). -iquote: VitaShell's own archive.h must not
# hide libarchive's. OunceVK's EGL/GLES headers come before switch-mesa's,
# as in libounce's switch2_rules.
ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS := -std=gnu11 -O2 -funsigned-char $(ARCH) \
	-ffunction-sections -fdata-sections \
	-Wall -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-format-truncation \
	-D__SWITCH__ -D__SWITCH2__ -D_REENTRANT \
	-Iswitch2/include -Iswitch2/third_party/ftpd -iquote . \
	-I$(PORTLIBS_SWITCH2)/include -isystem $(LIBOUNCE)/include \
	-I$(PORTLIBS)/include/SDL2 -I$(PORTLIBS)/include

# The Switch 2 libraries come first: OunceVK's libEGL in place of
# switch-mesa's, and libounce's libnx in place of libnx. sdl2-config is not
# used, because it adds devkitPro's libnx directory. The portlibs follow SDL
# itself, with their own dependencies.
LIBPATHS := -L$(PORTLIBS_SWITCH2)/lib -L$(LIBOUNCE)/lib -L$(PORTLIBS)/lib
LIBS := -lSDL2_ttf -lharfbuzz -lfreetype \
	-lSDL2_image -lpng16 -ljpeg -lwebpdemux -lwebp \
	-larchive -lexpat -llz4 -lzstd -llzma -lbz2 -lz \
	-lvorbisfile -lvorbis -logg \
	-lonig \
	-lSDL2 -lEGL -lglapi -ldrm_nouveau -lstdc++ -lpthread \
	-lnx -lm

# a/b.c -> $(OUT)/obj/a__b.o; a/b.png -> $(OUT)/res/a__b.png.o
object = $(OUT)/obj/$(subst /,__,$(basename $(1))).o
resource_object = $(OUT)/res/$(subst /,__,$(1)).o
OBJECTS := $(foreach s,$(UPSTREAM_SOURCES) $(PORT_SOURCES),$(call object,$(s)))
RESOURCE_OBJECTS := $(foreach r,$(RESOURCES),$(call resource_object,$(r)))

all: $(OUT)/$(NAME).nro

define compile
$(call object,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	$(TOOLS)gcc $(CFLAGS) -MMD -MP -c $$< -o $$@
endef
$(foreach s,$(UPSTREAM_SOURCES) $(PORT_SOURCES),$(eval $(call compile,$(s))))

# Run from the root, so that the symbols carry the relative path.
define embed
$(call resource_object,$(1)): $(1)
	@mkdir -p $$(dir $$@)
	$(TOOLS)ld -r -b binary -o $$@ $(1)
endef
$(foreach r,$(RESOURCES),$(eval $(call embed,$(r))))

$(OUT)/$(NAME).elf: $(OBJECTS) $(RESOURCE_OBJECTS)
	$(TOOLS)gcc -specs=$(LIBOUNCE)/switch.specs -g $(ARCH) \
	    -Wl,-Map,$(OUT)/$(NAME).map $^ $(LIBPATHS) $(LIBS) -o $@

$(OUT)/$(NAME).nacp: switch2/Makefile
	@mkdir -p $(OUT)
	$(DEVKITPRO)/tools/bin/nacptool --create "$(TITLE)" "$(AUTHOR)" "$(VERSION)" $@

$(OUT)/$(NAME).nro: $(OUT)/$(NAME).elf $(OUT)/$(NAME).nacp $(ICON)
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=$(OUT)/$(NAME).nacp --icon=$(ICON)

clean:
	rm -rf $(OUT)

.PHONY: all clean

-include $(OBJECTS:.o=.d)
