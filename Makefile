CC ?= gcc
V ?= 0

-include config.mk

CFLAGS+=-Wextra -Wall -Wno-unused-parameter \
	-Wstrict-prototypes -Wmissing-prototypes -O0 -g
CPPFLAGS+=$(DEP_CFLAGS) -D_GNU_SOURCE
LDLIBS+=$(DEP_LIBS) -lm

HEADERS := $(wildcard *.h)
OBJS := wesgr.o parse.o graphdata.o handler.o
EXE := wesgr
GENERATED := config.mk

all: $(EXE)
demo: tgraph1.svg tgraph2.svg sample3-overview.svg sample3-detail.svg

.PHONY: clean demo

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

tgraph1.svg: $(EXE) style.css
	./$(EXE) -i testdata/timeline-1.log -o $@ -a 413 -b 620

tgraph2.svg: $(EXE) style.css
	./$(EXE) -i testdata/timeline-2.log -o $@

sample3-overview.svg: $(EXE) style.css
	./$(EXE) -i testdata/timeline-3.log -o $@

sample3-detail.svg: $(EXE) style.css
	./$(EXE) -i testdata/timeline-3.log -o $@ -a 26000 -b 26100

%.o: %.c
	$(M_V_CC)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

.SUFFIXES:

m_v_cc_0 = @echo "  CC    " $@;
M_V_CC = $(m_v_cc_$(V))
m_v_link_0 = @echo "  LINK  " $@;
M_V_LINK = $(m_v_link_$(V))
m_v_gen_0 = @echo "  GEN   " $@;
M_V_GEN = $(m_v_gen_$(V))

