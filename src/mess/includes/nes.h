/*****************************************************************************
 *
 * includes/nes.h
 *
 * Nintendo Entertainment System (Famicom)
 *
 ****************************************************************************/

#ifndef NES_H_
#define NES_H_


/***************************************************************************
    CONSTANTS
***************************************************************************/

#define NTSC_CLOCK           N2A03_DEFAULTCLOCK     /* 1.789772 MHz */
#define PAL_CLOCK	           (26601712.0/16)        /* 1.662607 MHz */


/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _nes_state nes_state;
struct _nes_state
{
	const device_config *ppu;
	const device_config *sound;
	const device_config *cart;
};


/*----------- defined in machine/nes.c -----------*/

WRITE8_HANDLER ( nes_IN0_w );
WRITE8_HANDLER ( nes_IN1_w );

extern unsigned char *nes_battery_ram;

struct nes_struct
{
	/* load-time cart variables which remain constant */
	UINT8 trainer;
	UINT8 battery;
	UINT16 prg_chunks;	// a recently dumped multigame cart has 256 chunks of both PRG & CHR!
	UINT16 chr_chunks;

	UINT8 format;	// 1 = iNES, 2 = UNIF

	/* system variables which don't change at run-time */
	UINT16 mapper;		// for iNES
	const char *board;	// for UNIF
	UINT8 four_screen_vram;
	UINT8 hard_mirroring;
	UINT8 slow_banking;
	UINT8 crc_hack;	// this is needed to detect different boards sharing the same Mappers (shame on .nes format)

	UINT8 *rom;
	UINT8 *vrom;
	UINT8 *vram;
	UINT8 *wram;
	UINT8 *ciram; //PPU nametable RAM - external to PPU!

	/* Variables which can change */
	UINT8 mid_ram_enable;
};

extern struct nes_struct nes;

struct fds_struct
{
	UINT8 *data;
	UINT8 sides;
	UINT8 *ram;

	/* Variables which can change */
	UINT8 motor_on;
	UINT8 door_closed;
	UINT8 current_side;
	UINT32 head_position;
	UINT8 status0;
	UINT8 read_mode;
	UINT8 write_reg;
};

extern struct fds_struct nes_fds;

/* protos */

DEVICE_IMAGE_LOAD(nes_cart);
DEVICE_START(nes_disk);
DEVICE_IMAGE_LOAD(nes_disk);
DEVICE_IMAGE_UNLOAD(nes_disk);

MACHINE_START( nes );
MACHINE_RESET( nes );

READ8_HANDLER( nes_IN0_r );
READ8_HANDLER( nes_IN1_r );

int nes_ppu_vidaccess( const device_config *device, int address, int data );

void nes_partialhash(char *dest, const unsigned char *data,
	unsigned long length, unsigned int functions);

/*----------- defined in video/nes.c -----------*/

extern int nes_vram_sprite[8];

PALETTE_INIT( nes );
VIDEO_START( nes_ntsc );
VIDEO_START( nes_pal );
VIDEO_UPDATE( nes );


#endif /* NES_H_ */
