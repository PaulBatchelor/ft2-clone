
OS = $(shell uname -s)

ifeq ($(OS), Darwin)
CFLAGS += -mmacosx-version-min=10.7
CFLAGS += -arch x86_64
CFLAGS += -mmmx -mfpmath=sse -msse2
CFLAGS += -I/Library/Frameworks/SDL2.framework/Headers
CFLAGS += -F/Library/Frameworks -D__MACOSX_CORE__

LIBS += /usr/lib/libiconv.dylib
LIBS += -lm -Winit-self

CFLAGS += -Wno-deprecated -Wextra -Wunused
CFLAGS += -mno-ms-bitfields
CFLAGS += -Wno-missing-field-initializers
CFLAGS += -Wswitch-default
CFLAGS += -stdlib=libc++
FRAMEWORKS += -framework CoreMidi -framework CoreAudio
FRAMEWORKS += -framework Cocoa
endif

ifeq ($(OS), Linux)
# Alpine linux has FTS as an external lib
LIBS += -lfts
endif

CFLAGS += -O3
LIBS += -lpthread -lm -lstdc++
LIBS += -lSDL2

#-o release/macos/ft2-osx.app/Contents/MacOS/ft2-osx -lSDL2

default: ft2

OBJ += ft2_sysreqs.o \
ft2_edit.o \
ft2_checkboxes.o \
ft2_main.o \
ft2_help.o \
ft2_gui.o \
ft2_module_saver.o \
ft2_pattern_ed.o \
ft2_scrollbars.o \
ft2_midi.o \
ft2_pushbuttons.o \
ft2_pattern_draw.o \
ft2_scopes.o \
ft2_sample_ed.o \
ft2_video.o \
ft2_diskop.o \
ft2_mouse.o \
ft2_nibbles.o \
ft2_trim.o \
ft2_radiobuttons.o \
ft2_keyboard.o \
ft2_inst_ed.o \
ft2_unicode.o \
ft2_audio.o \
ft2_module_loader.o \
ft2_sample_saver.o \
ft2_sample_ed_features.o \
ft2_events.o \
ft2_replayer.o \
ft2_textboxes.o \
gfxdata/ft2_gfx_midi.o \
gfxdata/ft2_gfx_fonts.o \
gfxdata/ft2_gfx_logo.o \
gfxdata/ft2_gfx_instr.o \
gfxdata/ft2_gfx_nibbles.o \
gfxdata/ft2_gfx_mouse.o \
gfxdata/ft2_gfx_scopes.o \
gfxdata/ft2_gfx_sampler.o \
ft2_mix.o \
ft2_sample_loader.o \
ft2_config.o \
ft2_audioselector.o \
ft2_sampling.o \
ft2_wav_renderer.o \
ft2_about.o \
rtmidi/RtMidi.o \
rtmidi/rtmidi_c.o \

ft2: $(OBJ)
	@echo "Linking FT2 Binary"
	@$(CC) $(CFLAGS) $(OBJ) -o $@ $(LIBS) $(FRAMEWORKS)

%.o: %.c
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) $< -o $@

%.o: %.cpp
	@echo "CXX $<"
	@$(CXX) -c $(CFLAGS) $< -o $@

clean:
	$(RM) $(OBJ)
	$(RM) ft2
