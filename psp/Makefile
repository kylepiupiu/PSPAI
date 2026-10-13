TARGET = pspai_v03
OBJS = main.o

CFLAGS = -O2 -G0 -Wall -Wextra
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBS = -lpspctrl -lpsputility -lpspnet -lpspnet_inet -lpspnet_apctl -lpspsdk
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPAI v0.3.1 Stability
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
