USE_JACK=1

OS = $(shell uname -s)

ifeq ($(OS), Darwin)
CFLAGS += -mmacosx-version-min=10.7
CFLAGS += -arch x86_64
CFLAGS += -mmmx -mfpmath=sse -msse2
CFLAGS += -I/Library/Frameworks/SDL2.framework/Headers
CFLAGS += -F/Library/Frameworks -D__MACOSX_CORE__

LIBS += /usr/lib/libiconv.dylib
LIBS += -lm -Winit-self

CFLAGS += -mno-ms-bitfields
CFLAGS += -Wno-missing-field-initializers
CFLAGS += -Wswitch-default
CFLAGS += -stdlib=libc++
FRAMEWORKS += -framework CoreMidi -framework CoreAudio
FRAMEWORKS += -framework Cocoa
endif

ifeq ($(OS), Linux)
# Alpine linux has FTS as an external lib
OS_NAME=$(shell \
if [ -f /etc/os-release ];\
then \
cat /etc/os-release | egrep -e ^ID= | cut -d= -f2; \
fi)
ifeq ($(OS_NAME), alpine)
LIBS += -lfts
endif
endif

#CFLAGS += -Wno-deprecated -Wextra -Wunused
CFLAGS += -O3 -g
LIBS += -lpthread -lm -lstdc++
LIBS += -lSDL2

ifdef USE_JACK
CFLAGS += -DFT2_JACK
LIBS += -ljack
endif

default: ft2

OBJ += sysreqs.o \
edit.o \
checkboxes.o \
main.o \
help.o \
gui.o \
module_saver.o \
pattern_ed.o \
scrollbars.o \
midi.o \
pushbuttons.o \
pattern_draw.o \
scopes.o \
sample_ed.o \
video.o \
diskop.o \
mouse.o \
nibbles.o \
trim.o \
radiobuttons.o \
keyboard.o \
inst_ed.o \
unicode.o \
audio.o \
module_loader.o \
sample_saver.o \
sample_ed_features.o \
events.o \
replayer.o \
textboxes.o \
gfxdata/gfx_midi.o \
gfxdata/gfx_fonts.o \
gfxdata/gfx_logo.o \
gfxdata/gfx_instr.o \
gfxdata/gfx_nibbles.o \
gfxdata/gfx_mouse.o \
gfxdata/gfx_scopes.o \
gfxdata/gfx_sampler.o \
mix.o \
sample_loader.o \
config.o \
audioselector.o \
sampling.o \
wav_renderer.o \
about.o \
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
