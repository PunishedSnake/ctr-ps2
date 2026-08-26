EE_BIN = ctr_ps2_renderer.elf
EE_OBJS = vu1_native_track.o main.o \
	native_renderer.o native_texture_fixture.o \
	native_geometry_stq.o p2trk.o native_track_fixture.o native_track_renderer.o
EE_LIBS = -ldraw -lgraph -lpacket2 -ldma
EE_DVP = dvp-as

# 0 = N1c conservative FLUSH+MSCAL before every batch.
# 1 = N1d reuse-aware policy: MSCAL-only while output ownership is disjoint,
#     FLUSH only before a TOP/TOPS output buffer is reused two batches later.
NATIVE_VIF_REUSE_AWARE ?= 0

EE_CFLAGS += -DCTR_PS2 -std=gnu17 \
	-DCTRPS2_NATIVE_VIF_REUSE_AWARE=$(NATIVE_VIF_REUSE_AWARE)
EE_INCS += -I. -I../include

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

n1d:
	$(MAKE) clean
	$(MAKE) NATIVE_VIF_REUSE_AWARE=1 all

%.o: %.vsm
	$(EE_DVP) $< -o $@

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

reset:
	ps2client reset

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
