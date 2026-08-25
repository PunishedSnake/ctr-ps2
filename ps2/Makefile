EE_BIN = ctr_ps2_renderer.elf
EE_OBJS = vu1_boot.o main.o renderer_ps2.o
EE_LIBS = -ldraw -lgraph -lpacket2 -ldma
EE_DVP = dvp-as

EE_CFLAGS += -DCTR_PS2 -std=gnu17
EE_INCS += -I. -I../include

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

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
