TARGET_DLL = WinSCard.dll
DLL_DEF    = exports.def
SRCS       = WinSCard.c scard_stuff.c scard_dll.c version.rc
ifdef DEBUG
CFLAGS = -std=c89 -O2 -Wall -Wextra -D_DEBUG
else
CFLAGS = -std=c89 -O2 -Wall -Wextra -DNDEBUG
endif
LIBS       = -lshlwapi -static
OBJS       = $(filter %.o, $(SRCS:.c=.o) $(SRCS:.cpp=.o) $(SRCS:.rc=.o))
DEPEND     = Makefile.dep

ifdef X86
TOOLPREFIX = i686-w64-mingw32-
else
TOOLPREFIX = x86_64-w64-mingw32-
endif

CC      = $(TOOLPREFIX)gcc
CXX     = $(TOOLPREFIX)g++
AR      = $(TOOLPREFIX)ar
STRIP   = $(TOOLPREFIX)strip
WINDRES = $(TOOLPREFIX)windres
ifneq (,$(filter %.cpp, $(SRCS)))
LD      = $(CXX)
else
LD      = $(CC)
endif

.PHONY: all clean

TARGETS = $(TARGET_BIN) $(TARGET_LIB) $(TARGET_DLL)
all: $(TARGETS)

clean:
	rm -fr $(TARGETS) $(OBJS) $(DEPEND) *.o *.d *~

ifdef TARGET_BIN
$(TARGET_BIN) : $(OBJS)
	$(LD) -g -o $@ $^ $(LIBS)
endif

ifdef TARGET_DLL
$(TARGET_DLL) : $(OBJS) $(DLL_DEF)
	$(LD) -Wl,--enable-stdcall-fixup -shared -o $@ $^ $(LIBS)
	$(STRIP) $@
endif

%.o: %.rc
	$(WINDRES) $< -o $@

$(DEPEND):
	rm -f $@
ifneq (,$(filter %.c, $(SRCS)))
	$(CC) -MM $(CFLAGS) $(CPPFLAGS) $(filter %.c,$(SRCS)) >> $@
endif
ifneq (,$(filter %.cpp, $(SRCS)))
	$(CXX) -MM $(CXXFLAGS) $(CPPFLAGS) $(filter %.cpp,$(SRCS)) >> $@
endif

#-include $(DEPEND)

HGVERSION := $(shell hg log -r . --template '{node|short}' 2> /dev/null || grep node .hg_archival.txt 2> /dev/null || true)

.PHONY: update_hgstamp
hgstamp: update_hgstamp
	@[ -f $@ ] || touch $@
	@echo $(HGVERSION) | cmp -s $@ - || echo $(HGVERSION) > $@

version.rc : version.in hgstamp
	./version.sh version.in
