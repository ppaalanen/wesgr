CC ?= gcc
V ?= 0

-include config.mk

CFLAGS+=-Wextra -Wall -Wno-unused-parameter \
	-Wstrict-prototypes -Wmissing-prototypes -O0 -g
CPPFLAGS+=$(DEP_CFLAGS)
LDLIBS+=$(DEP_LIBS) -lm

HEADERS := $(wildcard *.h)
OBJS := wesgr.o parse.o graphdata.o handler.o
EXE := wesgr
GENERATED := config.mk

all: $(EXE)

.PHONY: clean test

clean:
	rm -f *.o $(EXE) $(GENERATED)

$(EXE): $(OBJS)
	$(M_V_LINK)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJS): $(HEADERS) config.mk

PKG_DEPS := json-c >= 0.11
config.mk: Makefile
	$(M_V_GEN)\
	echo "DEP_CFLAGS=`pkg-config --cflags '$(PKG_DEPS)'`" > $@ && \
	echo "DEP_LIBS=`pkg-config --libs '$(PKG_DEPS)'`" >> $@

%.o: %.c
	$(M_V_CC)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

.SUFFIXES:

m_v_cc_0 = @echo "  CC    " $@;
M_V_CC = $(m_v_cc_$(V))
m_v_link_0 = @echo "  LINK  " $@;
M_V_LINK = $(m_v_link_$(V))
m_v_gen_0 = @echo "  GEN   " $@;
M_V_GEN = $(m_v_gen_$(V))

