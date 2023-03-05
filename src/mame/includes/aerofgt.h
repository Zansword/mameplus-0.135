/*----------- defined in video/aerofgt.c -----------*/

extern UINT16 *aerofgt_rasterram;
extern UINT16 *aerofgt_bg1videoram,*aerofgt_bg2videoram;
extern UINT16 *aerofgt_spriteram1,*aerofgt_spriteram2,*aerofgt_spriteram3;
extern UINT16 *wbbc97_bitmapram;
extern size_t aerofgt_spriteram1_size,aerofgt_spriteram2_size,aerofgt_spriteram3_size;
extern UINT16 *spikes91_tx_tilemap_ram;

WRITE16_HANDLER( aerofgt_bg1videoram_w );
WRITE16_HANDLER( aerofgt_bg2videoram_w );
WRITE16_HANDLER( pspikes_gfxbank_w );
WRITE16_HANDLER( pspikesb_gfxbank_w );
WRITE16_HANDLER( spikes91_lookup_w );
WRITE16_HANDLER( karatblz_gfxbank_w );
WRITE16_HANDLER( spinlbrk_gfxbank_w );
WRITE16_HANDLER( turbofrc_gfxbank_w );
WRITE16_HANDLER( aerofgt_gfxbank_w );
WRITE16_HANDLER( aerofgt_bg1scrollx_w );
WRITE16_HANDLER( aerofgt_bg1scrolly_w );
WRITE16_HANDLER( aerofgt_bg2scrollx_w );
WRITE16_HANDLER( aerofgt_bg2scrolly_w );
WRITE16_HANDLER( pspikes_palette_bank_w );
WRITE16_HANDLER( wbbc97_bitmap_enable_w );
VIDEO_START( pspikes );
VIDEO_START( karatblz );
VIDEO_START( spinlbrk );
VIDEO_START( turbofrc );
VIDEO_UPDATE( pspikes );
VIDEO_UPDATE( pspikesb );
VIDEO_UPDATE( spikes91 );
VIDEO_UPDATE( karatblz );
VIDEO_UPDATE( spinlbrk );
VIDEO_UPDATE( turbofrc );
VIDEO_UPDATE( aerofgt );
VIDEO_UPDATE( aerfboot );
VIDEO_UPDATE( aerfboo2 );
VIDEO_START( wbbc97 );
VIDEO_UPDATE( wbbc97 );
