CROSS_COMPILER = $(EASY_SDK)/sysroots/x86_64-arago-linux/usr/bin/arm-linux-gnueabihf
CC = $(CROSS_COMPILER)-gcc --sysroot=$(EASY_SDK)/sysroots/armv7ahf-neon-linux-gnueabi
CFLAGS = -g -O0 -march=armv7-a -marm -mfpu=neon -mfloat-abi=hard
CLIBS = -L./lib/ -ldrm -ldrm_omap

USR_INC = ${J7_SDK}/ti-processor-sdk-linux-automotive-j7-evm-06_01_00_05/linux-devkit/sysroots/aarch64-linux/
INCLUDE_DIRS = -I./include -I$(USR_INC)/libdrm -I$(USR_INC)/omap

TARGET = drm_test
OBJECTS = drm_test.o

$(TARGET) : $(OBJECTS)
	$(CC) $< -o $@ $(CLIBS)
	
$(OBJECTS) : %.o : %.c 
	$(CC) -c $(CFLAGS) $< -o $@ $(INCLUDE_DIRS)

.PHONY : clean
clean:
	rm -rf $(TARGET) $(OBJECTS)