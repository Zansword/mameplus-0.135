###########################################################################
#
#   emu.mak
#
#   MAME emulation core makefile
#
#   Copyright Nicola Salmoria and the MAME Team.
#   Visit http://mamedev.org for licensing and usage restrictions.
#
###########################################################################


EMUSRC = $(SRC)/emu
EMUOBJ = $(OBJ)/emu

EMUAUDIO = $(EMUOBJ)/audio
EMUDRIVERS = $(EMUOBJ)/drivers
EMULAYOUT = $(EMUOBJ)/layout
EMUMACHINE = $(EMUOBJ)/machine
EMUVIDEO = $(EMUOBJ)/video

OBJDIRS += \
	$(EMUOBJ)/cpu \
	$(EMUOBJ)/sound \
	$(EMUOBJ)/debug \
	$(EMUOBJ)/audio \
	$(EMUOBJ)/drivers \
	$(EMUOBJ)/machine \
	$(EMUOBJ)/layout \
	$(EMUOBJ)/video \



#-------------------------------------------------
# emulator core objects
#-------------------------------------------------

EMUOBJS = \
	$(EMUOBJ)/attotime.o \
	$(EMUOBJ)/audit.o \
	$(EMUOBJ)/cheat.o \
	$(EMUOBJ)/clifront.o \
	$(EMUOBJ)/config.o \
	$(EMUOBJ)/cpuexec.o \
	$(EMUOBJ)/crsshair.o \
	$(EMUOBJ)/debugger.o \
	$(EMUOBJ)/devcb.o \
	$(EMUOBJ)/devintrf.o \
	$(EMUOBJ)/drawgfx.o \
	$(EMUOBJ)/driver.o \
	$(EMUOBJ)/emuopts.o \
	$(EMUOBJ)/emupal.o \
	$(EMUOBJ)/fileio.o \
	$(EMUOBJ)/hash.o \
	$(EMUOBJ)/info.o \
	$(EMUOBJ)/input.o \
	$(EMUOBJ)/inputseq.o \
	$(EMUOBJ)/inptport.o \
	$(EMUOBJ)/mame.o \
	$(EMUOBJ)/mamecore.o \
	$(EMUOBJ)/mconfig.o \
	$(EMUOBJ)/memory.o \
	$(EMUOBJ)/output.o \
	$(EMUOBJ)/render.o \
	$(EMUOBJ)/rendfont.o \
	$(EMUOBJ)/rendlay.o \
	$(EMUOBJ)/rendutil.o \
	$(EMUOBJ)/restrack.o \
	$(EMUOBJ)/romload.o \
	$(EMUOBJ)/sound.o \
	$(EMUOBJ)/state.o \
	$(EMUOBJ)/streams.o \
	$(EMUOBJ)/tilemap.o \
	$(EMUOBJ)/timer.o \
	$(EMUOBJ)/ui.o \
	$(EMUOBJ)/uigfx.o \
	$(EMUOBJ)/uiinput.o \
	$(EMUOBJ)/uimenu.o \
	$(EMUOBJ)/validity.o \
	$(EMUOBJ)/video.o \
	$(EMUOBJ)/watchdog.o \
	$(EMUOBJ)/debug/debugcmd.o \
	$(EMUOBJ)/debug/debugcmt.o \
	$(EMUOBJ)/debug/debugcon.o \
	$(EMUOBJ)/debug/debugcpu.o \
	$(EMUOBJ)/debug/debughlp.o \
	$(EMUOBJ)/debug/debugvw.o \
	$(EMUOBJ)/debug/express.o \
	$(EMUOBJ)/debug/textbuf.o \
	$(EMUOBJ)/uilang.o

ifdef USE_CMD_LIST
EMUOBJS += \
	$(EMUOBJ)/cmddata.o
endif

ifdef USE_IPS
EMUOBJS += \
	$(EMUOBJ)/ips.o
endif

ifdef USE_HISCORE
EMUOBJS += \
	$(EMUOBJ)/hiscore.o
endif

ifdef PROFILER
EMUOBJS += \
	$(EMUOBJ)/profiler.o
endif

EMUSOUNDOBJS = \
	$(EMUOBJ)/sound/filter.o \
	$(EMUOBJ)/sound/flt_vol.o \
	$(EMUOBJ)/sound/flt_rc.o \
	$(EMUOBJ)/sound/wavwrite.o \

EMUAUDIOOBJS = \
	$(EMUAUDIO)/generic.o \

EMUDRIVEROBJS = \
	$(EMUDRIVERS)/empty.o \

EMUMACHINEOBJS = \
	$(EMUMACHINE)/53c810.o \
	$(EMUMACHINE)/6532riot.o \
	$(EMUMACHINE)/6522via.o \
	$(EMUMACHINE)/6526cia.o \
	$(EMUMACHINE)/6821pia.o \
	$(EMUMACHINE)/6840ptm.o \
	$(EMUMACHINE)/6850acia.o \
	$(EMUMACHINE)/68681.o \
	$(EMUMACHINE)/7474.o \
	$(EMUMACHINE)/74123.o \
	$(EMUMACHINE)/74148.o \
	$(EMUMACHINE)/74153.o \
	$(EMUMACHINE)/74181.o \
	$(EMUMACHINE)/8042kbdc.o \
	$(EMUMACHINE)/8237dma.o \
	$(EMUMACHINE)/8257dma.o \
	$(EMUMACHINE)/8255ppi.o \
	$(EMUMACHINE)/adc083x.o \
	$(EMUMACHINE)/adc1213x.o \
 	$(EMUMACHINE)/am53cf96.o \
 	$(EMUMACHINE)/at28c16.o \
 	$(EMUMACHINE)/cdp1852.o \
	$(EMUMACHINE)/ds1302.o \
	$(EMUMACHINE)/ds2401.o \
	$(EMUMACHINE)/ds2404.o \
	$(EMUMACHINE)/eeprom.o \
	$(EMUMACHINE)/eepromdev.o \
	$(EMUMACHINE)/f3853.o \
	$(EMUMACHINE)/generic.o \
	$(EMUMACHINE)/i8243.o \
	$(EMUMACHINE)/i8255a.o \
	$(EMUMACHINE)/i2cmem.o \
	$(EMUMACHINE)/i2cmemdev.o \
 	$(EMUMACHINE)/idectrl.o \
 	$(EMUMACHINE)/intelfsh.o \
	$(EMUMACHINE)/latch8.o \
	$(EMUMACHINE)/ldcore.o \
	$(EMUMACHINE)/ldpr8210.o \
	$(EMUMACHINE)/ldv1000.o \
	$(EMUMACHINE)/ldvp931.o \
	$(EMUMACHINE)/mb3773.o \
	$(EMUMACHINE)/mb87078.o \
	$(EMUMACHINE)/mc146818.o \
	$(EMUMACHINE)/microtch.o \
	$(EMUMACHINE)/msm6242.o \
	$(EMUMACHINE)/pc16552d.o \
	$(EMUMACHINE)/pci.o \
	$(EMUMACHINE)/pic8259.o \
	$(EMUMACHINE)/pit8253.o \
	$(EMUMACHINE)/pd4990a.o \
	$(EMUMACHINE)/roc10937.o \
	$(EMUMACHINE)/rp5h01.o \
	$(EMUMACHINE)/rtc65271.o \
	$(EMUMACHINE)/scsi.o \
	$(EMUMACHINE)/scsicd.o \
	$(EMUMACHINE)/scsidev.o \
	$(EMUMACHINE)/scsihd.o \
	$(EMUMACHINE)/cr589.o \
 	$(EMUMACHINE)/smc91c9x.o \
	$(EMUMACHINE)/timekpr.o \
	$(EMUMACHINE)/tmp68301.o \
	$(EMUMACHINE)/upd4701.o \
	$(EMUMACHINE)/wd33c93.o \
	$(EMUMACHINE)/x2212.o \
	$(EMUMACHINE)/x76f041.o \
	$(EMUMACHINE)/x76f100.o \
	$(EMUMACHINE)/z80ctc.o \
	$(EMUMACHINE)/z80dma.o \
	$(EMUMACHINE)/z80pio.o \
	$(EMUMACHINE)/z80sio.o \

EMUVIDEOOBJS = \
	$(EMUVIDEO)/generic.o \
 	$(EMUVIDEO)/mc6845.o \
	$(EMUVIDEO)/poly.o \
	$(EMUVIDEO)/resnet.o \
	$(EMUVIDEO)/rgbutil.o \
	$(EMUVIDEO)/s2636.o \
	$(EMUVIDEO)/tlc34076.o \
	$(EMUVIDEO)/tms34061.o \
 	$(EMUVIDEO)/tms9927.o \
 	$(EMUVIDEO)/tms9928a.o \
	$(EMUVIDEO)/v9938.o \
 	$(EMUVIDEO)/vector.o \
 	$(EMUVIDEO)/voodoo.o \

$(LIBEMU): $(EMUOBJS) $(EMUSOUNDOBJS) $(EMUAUDIOOBJS) $(EMUDRIVEROBJS) $(EMUMACHINEOBJS) $(EMUVIDEOOBJS)



#-------------------------------------------------
# CPU core objects
#-------------------------------------------------

include $(EMUSRC)/cpu/cpu.mak

$(LIBCPU): $(CPUOBJS)

$(LIBDASM): $(DASMOBJS)



#-------------------------------------------------
# sound core objects
#-------------------------------------------------

include $(EMUSRC)/sound/sound.mak

$(LIBSOUND): $(SOUNDOBJS)



#-------------------------------------------------
# additional dependencies
#-------------------------------------------------

$(EMUOBJ)/rendfont.o:		$(EMUOBJ)/uismall11.fh $(EMUOBJ)/uismall14.fh $(EMUOBJ)/uicmd11.fh $(EMUOBJ)/uicmd14.fh

$(EMUOBJ)/video.o:		$(EMUSRC)/rendersw.c



#-------------------------------------------------
# core layouts
#-------------------------------------------------

$(EMUOBJ)/rendlay.o:	$(EMULAYOUT)/dualhovu.lh \
						$(EMULAYOUT)/dualhsxs.lh \
						$(EMULAYOUT)/dualhuov.lh \
						$(EMULAYOUT)/horizont.lh \
						$(EMULAYOUT)/triphsxs.lh \
						$(EMULAYOUT)/vertical.lh \
						$(EMULAYOUT)/ho20ffff.lh \
						$(EMULAYOUT)/ho2eff2e.lh \
						$(EMULAYOUT)/ho4f893d.lh \
						$(EMULAYOUT)/ho88ffff.lh \
						$(EMULAYOUT)/hoa0a0ff.lh \
						$(EMULAYOUT)/hoffe457.lh \
						$(EMULAYOUT)/hoffff20.lh \
						$(EMULAYOUT)/voffff20.lh \

$(EMUOBJ)/video.o:		$(EMULAYOUT)/snap.lh



#-------------------------------------------------
# embedded font
#-------------------------------------------------

$(EMUOBJ)/uismall11.bdc: $(PNG2BDC) \
		$(SRC)/emu/font/uismall.png \
		$(SRC)/emu/font/cp1250.png
	@echo Generating $@...
	@$^ $@

$(EMUOBJ)/uismall14.bdc: $(PNG2BDC) \
		$(SRC)/emu/font/cp1252.png \
		$(SRC)/emu/font/cp932.png \
		$(SRC)/emu/font/cp932hw.png \
		$(SRC)/emu/font/cp936.png \
		$(SRC)/emu/font/cp949.png \
		$(SRC)/emu/font/cp950.png
	@echo Generating $@...
	@$^ $@

$(EMUOBJ)/uicmd11.bdc: $(PNG2BDC) $(SRC)/emu/font/cmd11.png
	@echo Generating $@...
	@$^ $@

$(EMUOBJ)/uicmd14.bdc: $(PNG2BDC) $(SRC)/emu/font/cmd14.png
	@echo Generating $@...
	@$^ $@
