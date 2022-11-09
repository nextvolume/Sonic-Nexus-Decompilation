ifeq ($(STATIC),1)
  PKG_CONFIG_STATIC_FLAG = --static
  CXXFLAGS_ALL += -static
endif

ifeq ($(DOS), 1)
USE_ALLEGRO4=1
endif

ifeq ($(USE_ALLEGRO4),1)
  ifeq ($(DOS), 1)
      CXXFLAGS_ALL += -DRETRO_USING_ALLEGRO4 -DRETRO_DOS
      CXX = i586-pc-msdosdjgpp-g++
      CC = i586-pc-msdosdjgpp-gcc
      LIBS_ALL += -lalleg
      
	ifneq ($(WSSAUDIO),)
		CXXFLAGS_ALL += -DRETRO_WSSAUDIO
		LIBS_ALL += -lwss
	endif
		
	ifneq ($(DOSSOUND),)
		CXXFLAGS_ALL += -DRETRO_DOSSOUND
	endif
  else 
      CXXFLAGS_ALL += -DRETRO_USING_ALLEGRO4 $(shell allegro-config --cflags)
      LIBS_ALL += $(shell allegro-config --libs)
  endif
else
  CXXFLAGS_ALL += $(shell pkg-config --cflags $(PKG_CONFIG_STATIC_FLAG) sdl2)
  LIBS_ALL += $(shell pkg-config --libs $(PKG_CONFIG_STATIC_FLAG) sdl2)
endif

CXXFLAGS_ALL += -MMD -MP -MF objects/$*.d 

ifeq ($(DOS), 1)
  CXXFLAGS_ALL += $(CXXFLAGS)
  LIBS_ALL += -lvorbisfile -lvorbis -logg
else
  CXXFLAGS_ALL += $(shell pkg-config --cflags $(PKG_CONFIG_STATIC_FLAG) vorbisfile vorbis) $(CXXFLAGS)
  LIBS_ALL += $(shell pkg-config --libs $(PKG_CONFIG_STATIC_FLAG) vorbisfile vorbis) -pthread $(LIBS)
endif

LDFLAGS_ALL += $(LDFLAGS)

SOURCES = \
  Nexus/Animation.cpp \
  Nexus/Audio.cpp \
  Nexus/Collision.cpp \
  Nexus/Debug.cpp \
  Nexus/Drawing.cpp \
  Nexus/Ini.cpp \
  Nexus/Input.cpp \
  Nexus/main.cpp \
  Nexus/Math.cpp \
  Nexus/Object.cpp \
  Nexus/Palette.cpp \
  Nexus/Player.cpp \
  Nexus/Reader.cpp \
  Nexus/RetroEngine.cpp \
  Nexus/Scene.cpp \
  Nexus/Script.cpp \
  Nexus/Sprite.cpp \
  Nexus/String.cpp \
  Nexus/Text.cpp \
  Nexus/Userdata.cpp \
  Nexus/Video.cpp

	  
ifeq ($(FORCE_CASE_INSENSITIVE),1)
  CXXFLAGS_ALL += -DFORCE_CASE_INSENSITIVE
  SOURCES += Nexus/fcaseopen.c
endif

ifeq ($(USE_HW_REN),1)
  CXXFLAGS_ALL += -DUSE_HW_REN
  LIBS_ALL += -lGL -lGLEW
endif

OBJECTS = $(SOURCES:%=objects/%.o)
DEPENDENCIES = $(SOURCES:%=objects/%.d)

all: bin/nexus

include $(wildcard $(DEPENDENCIES))

objects/%.o: %
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_ALL) -std=c++17 $< -o $@ -c

bin/nexus: $(OBJECTS)
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS_ALL) $(LDFLAGS_ALL) $^ -o $@ $(LIBS_ALL)

install: bin/nexus
	install -Dp -m755 bin/nexus $(prefix)/bin/nexus

clean:
	 rm -r -f bin && rm -r -f objects
