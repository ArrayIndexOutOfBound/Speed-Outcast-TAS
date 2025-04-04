// leave this as first line for PCH reasons...
//
#include "../server/exe_headers.h"

/*****************************************************************************
 * name:		cl_cin.c
 *
 * desc:		video and cinematic playback
 *
 * $Archive: /MissionPack/code/client/cl_cin.c $
 * $Author: Ttimo $ 
 * $Revision: 82 $
 * $Modtime: 4/13/01 4:48p $
 * $Date: 4/13/01 4:48p $
 *
 * cl_glconfig.hwtype trtypes 3dfx/ragepro need 256x256
 *
 *****************************************************************************/

#include "client.h"
#include "client_ui.h"	// CHC
#include "snd_local.h"

#include "../speedrun/speedrun_timer_q3/timer.h"

#define MAXSIZE				8
#define MINSIZE				4

#define DEFAULT_CIN_WIDTH	512
#define DEFAULT_CIN_HEIGHT	512

#define ROQ_QUAD			0x1000
#define ROQ_QUAD_INFO		0x1001
#define ROQ_CODEBOOK		0x1002
#define ROQ_QUAD_VQ			0x1011
#define ROQ_QUAD_JPEG		0x1012
#define ROQ_QUAD_HANG		0x1013
#define ROQ_PACKET			0x1030
#define ZA_SOUND_MONO		0x1020
#define ZA_SOUND_STEREO		0x1021

#define MAX_VIDEO_HANDLES	16

extern glconfig_t glConfig;
extern	int		s_paintedtime;
extern	int		s_rawend;

extern void S_CIN_StopSound(sfxHandle_t sfxHandle);
static void RoQ_init( void );

/******************************************************************************
*
* Class:		trFMV
*
* Description:	RoQ/RnR manipulation routines
*				not entirely complete for first run
*
******************************************************************************/

typedef struct {
	byte				linbuf[DEFAULT_CIN_WIDTH*DEFAULT_CIN_HEIGHT*4*2];
	byte				file[65536];
	short				sqrTable[256];

	unsigned int		mcomp[256];
	unsigned short		vq2[256*16*4];
	unsigned short		vq4[256*64*4];
	unsigned short		vq8[256*256*4];

	long				ROQ_YY_tab[256];
	long				ROQ_UB_tab[256];
	long				ROQ_UG_tab[256];
	long				ROQ_VG_tab[256];
	long				ROQ_VR_tab[256];

	byte				*qStatus[2][32768];

	long				oldXOff, oldYOff, oldysize, oldxsize;
} cinematics_t;

typedef struct {
	char				fileName[MAX_OSPATH];
	int					CIN_WIDTH, CIN_HEIGHT;
	int					xpos, ypos, width, height;
	qboolean			looping, holdAtEnd, dirty, alterGameState, silent, shader;
	fileHandle_t		iFile;	// 0 = none
	e_status			status;	
	unsigned int		startTime;
	unsigned int		lastTime;
	long				tfps;
	long				RoQPlayed;
	long				ROQSize;
	unsigned int		RoQFrameSize;
	long				onQuad;
	long				numQuads;
	long				samplesPerLine;
	unsigned int		roq_id;
	long				screenDelta;

	void ( *VQ0)(byte *status, void *qdata );
	void ( *VQ1)(byte *status, void *qdata );
	void ( *VQNormal)(byte *status, void *qdata );
	void ( *VQBuffer)(byte *status, void *qdata );

	byte*				gray;
	unsigned int		xsize, ysize, maxsize, minsize;

	qboolean			inMemory;
	long				normalBuffer0;
	long				roq_flags;
	long				roqF0;
	long				roqF1;
	long				t[2];
	long				roqFPS;
	int					playonwalls;
	byte*				buf;
	long				drawX, drawY;
	sfxHandle_t			hSFX;	// 0 = none
	qhandle_t			hCRAWLTEXT;	// 0 = none
} cin_cache;

static cinematics_t		cin;
static cin_cache		cinTable[MAX_VIDEO_HANDLES];
static int				currentHandle = -1;
static int				CL_handle = -1;
static int				CL_iPlaybackStartTime;	// so I can stop users quitting playback <1 second after it starts

extern int				s_soundtime;		// sample PAIRS
extern int   			s_paintedtime; 		// sample PAIRS


void CIN_CloseAllVideos(void) {
	int		i;

	for ( i = 0 ; i < MAX_VIDEO_HANDLES ; i++ ) {
		if (cinTable[i].fileName[0] != 0 ) {
			CIN_StopCinematic(i);
		}
	}
}


static int CIN_HandleForVideo(void) {
	int		i;

	for ( i = 0 ; i < MAX_VIDEO_HANDLES ; i++ ) {
		if ( cinTable[i].fileName[0] == 0 ) {
			return i;
		}
	}
	Com_Error( ERR_DROP, "CIN_HandleForVideo: none free" );
	return -1;
}




//-----------------------------------------------------------------------------
// RllSetupTable
//
// Allocates and initializes the square table.
//
// Parameters:	None
//
// Returns:		Nothing
//-----------------------------------------------------------------------------
static void RllSetupTable()
{
	int z;

	if (currentHandle < 0) return;

	for (z=0;z<128;z++) {
		cin.sqrTable[z] = (short)(z*z);
		cin.sqrTable[z+128] = (short)(-cin.sqrTable[z]);
	}
}



//-----------------------------------------------------------------------------
// RllDecodeMonoToMono
//
// Decode mono source data into a mono buffer.
//
// Parameters:	from -> buffer holding encoded data
//				to ->	buffer to hold decoded data
//				size =	number of bytes of input (= # of shorts of output)
//				signedOutput = 0 for unsigned output, non-zero for signed output
//				flag = flags from asset header
//
// Returns:		Number of samples placed in output buffer
//-----------------------------------------------------------------------------
/*
static long RllDecodeMonoToMono(unsigned char *from,short *to,unsigned int size,char signedOutput ,unsigned short flag)
{
	unsigned int z;
	int prev;
	
	if (currentHandle < 0) return 0;

	if (signedOutput)	
		prev =  flag - 0x8000;
	else 
		prev = flag;

	for (z=0;z<size;z++) {
		prev = to[z] = (short)(prev + cin.sqrTable[from[z]]); 
	}
	return size;	//*sizeof(short));
}
*/

//-----------------------------------------------------------------------------
// RllDecodeMonoToStereo
//
// Decode mono source data into a stereo buffer. Output is 4 times the number
// of bytes in the input.
//
// Parameters:	from -> buffer holding encoded data
//				to ->	buffer to hold decoded data
//				size =	number of bytes of input (= 1/4 # of bytes of output)
//				signedOutput = 0 for unsigned output, non-zero for signed output
//				flag = flags from asset header
//
// Returns:		Number of samples placed in output buffer
//-----------------------------------------------------------------------------
static long RllDecodeMonoToStereo(unsigned char *from,unsigned int size,char signedOutput,unsigned short flag, qboolean bMixedWithCurrentAudio)
{
	unsigned int z;
	int prev, dst;
	portable_samplepair_t	*samps;
	
	if (currentHandle < 0) return 0;

	if (signedOutput)	
		prev =  flag - 0x8000;
	else 
		prev = flag;

	samps = S_GetRawSamplePointer();

	extern cvar_t *s_volume;
	int iVolume = 1/*bMixedWithCurrentAudio*/ ? ((int) (256.0f * (s_volume?s_volume->value:1.0f))) : 256;

	for (z = 0; z < size; z++) {
		dst = s_rawend&(MAX_RAW_SAMPLES-1);
		s_rawend++;
		prev = (short)(prev + cin.sqrTable[from[z]]);
		samps[dst].left = samps[dst].right =  prev*iVolume;
	}
	
	return size;	// * 2 * sizeof(short));
}


//-----------------------------------------------------------------------------
// RllDecodeStereoToStereo
//
// Decode stereo source data into a stereo buffer.
//
// Parameters:	from -> buffer holding encoded data
//				to ->	buffer to hold decoded data
//				size =	number of bytes of input (= 1/2 # of bytes of output)
//				signedOutput = 0 for unsigned output, non-zero for signed output
//				flag = flags from asset header
//
// Returns:		Number of samples placed in output buffer
//-----------------------------------------------------------------------------
static long RllDecodeStereoToStereo(unsigned char *from,unsigned int size,char signedOutput, unsigned short flag, qboolean bMixedWithCurrentAudio)
{
	unsigned int z;
	unsigned char *zz = from;
	int	prevL, prevR, dst;
	portable_samplepair_t	*samps;

	if (currentHandle < 0) return 0;

	if (signedOutput) {
		prevL = (flag & 0xff00) - 0x8000;
		prevR = ((flag & 0x00ff) << 8) -0x8000;
	} else {
		prevL = flag & 0xff00;
		prevR = (flag & 0x00ff) << 8;
	}

	samps = S_GetRawSamplePointer();

	extern cvar_t *s_volume;
	int iVolume = 1/*bMixedWithCurrentAudio*/ ? ((int) (256.0f * (s_volume?s_volume->value:1.0f))) : 256;

	switch (dma.speed)
	{
		case 11025:
		{
			for (z=0;z<size;z+=2) {
				prevL = (short)(prevL + cin.sqrTable[*zz++]); 
				prevR = (short)(prevR + cin.sqrTable[*zz++]);

				if (z&2)
				{
					dst = s_rawend&(MAX_RAW_SAMPLES-1);
					s_rawend++;
					samps[dst].left  = prevL*iVolume;
					samps[dst].right = prevR*iVolume;
				}
			}
		}
		break;

		case 22050:
		{
			// 1:1 case...
			//
			for (z=0;z<size;z+=2) {
				dst = s_rawend&(MAX_RAW_SAMPLES-1);
				s_rawend++;
				prevL = (short)(prevL + cin.sqrTable[*zz++]); 
				prevR = (short)(prevR + cin.sqrTable[*zz++]);
				samps[dst].left  = prevL*iVolume;
				samps[dst].right = prevR*iVolume;
			}
		}
		break;

		case 44100:
		{
			for (z=0;z<size;z+=2) {
				prevL = (short)(prevL + cin.sqrTable[*zz++]); 
				prevR = (short)(prevR + cin.sqrTable[*zz++]);

				dst = s_rawend&(MAX_RAW_SAMPLES-1);
				s_rawend++;
				samps[dst].left  = prevL*iVolume;
				samps[dst].right = prevR*iVolume;

				dst = s_rawend&(MAX_RAW_SAMPLES-1);
				s_rawend++;
				samps[dst].left  = prevL*iVolume;
				samps[dst].right = prevR*iVolume;
			}
		}
		break;
	}
	
	return (size>>1);	//*sizeof(short));
}


//-----------------------------------------------------------------------------
// RllDecodeStereoToMono
//
// Decode stereo source data into a mono buffer.
//
// Parameters:	from -> buffer holding encoded data
//				to ->	buffer to hold decoded data
//				size =	number of bytes of input (= # of bytes of output)
//				signedOutput = 0 for unsigned output, non-zero for signed output
//				flag = flags from asset header
//
// Returns:		Number of samples placed in output buffer
//-----------------------------------------------------------------------------
/*
static long RllDecodeStereoToMono(unsigned char *from,short *to,unsigned int size,char signedOutput, unsigned short flag)
{
	unsigned int z;
	int prevL,prevR;
	
	if (currentHandle < 0) return 0;

	if (signedOutput) {
		prevL = (flag & 0xff00) - 0x8000;
		prevR = ((flag & 0x00ff) << 8) -0x8000;
	} else {
		prevL = flag & 0xff00;
		prevR = (flag & 0x00ff) << 8;
	}

	for (z=0;z<size;z+=1) {
		prevL= prevL + cin.sqrTable[from[z*2]];
		prevR = prevR + cin.sqrTable[from[z*2+1]];
		to[z] = (short)((prevL + prevR)/2);
	}

	return size;
}
*/
/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void move8_32( byte *src, byte *dst, int spl )
{
	double *dsrc, *ddst;
	int dspl;

	dsrc = (double *)src;
	ddst = (double *)dst;
	dspl = spl>>3;

	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void move4_32( byte *src, byte *dst, int spl  )
{
	double *dsrc, *ddst;
	int dspl;

	dsrc = (double *)src;
	ddst = (double *)dst;
	dspl = spl>>3;

	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
	dsrc += dspl; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void blit8_32( byte *src, byte *dst, int spl  )
{
	double *dsrc, *ddst;
	int dspl;

	dsrc = (double *)src;
	ddst = (double *)dst;
	dspl = spl>>3;

	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += 4; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += 4; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += 4; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += 4; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += 4; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += 4; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
	dsrc += 4; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1]; ddst[2] = dsrc[2]; ddst[3] = dsrc[3];
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void blit4_32( byte *src, byte *dst, int spl  )
{
	double *dsrc, *ddst;
	int dspl;

	dsrc = (double *)src;
	ddst = (double *)dst;
	dspl = spl>>3;

	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
	dsrc += 2; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
	dsrc += 2; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
	dsrc += 2; ddst += dspl;
	ddst[0] = dsrc[0]; ddst[1] = dsrc[1];
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void blit2_32( byte *src, byte *dst, int spl  )
{
	double *dsrc, *ddst;
	int dspl;

	dsrc = (double *)src;
	ddst = (double *)dst;
	dspl = spl>>3;

	ddst[0] = dsrc[0];
	ddst[dspl] = dsrc[1];
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void blitVQQuad32fs( byte **status, unsigned char *data )
{
unsigned short	newd, celdata, code;
unsigned int	index, i;

	newd	= 0;
	celdata = 0;
	index	= 0;
	
	if (currentHandle < 0) return;

	do {
		if (!newd) { 
			newd = 7;
			celdata = data[0] + data[1]*256;
			data += 2;
		} else {
			newd--;
		}

		code = (unsigned short)(celdata&0xc000); 
		celdata <<= 2;
		
		switch (code) {
			case	0x8000:													// vq code
				blit8_32( (byte *)&cin.vq8[(*data)*128], status[index], cinTable[currentHandle].samplesPerLine );
				data++;
				index += 5;
				break;
			case	0xc000:													// drop
				index++;													// skip 8x8
				for(i=0;i<4;i++) {
					if (!newd) { 
						newd = 7;
						celdata = data[0] + data[1]*256;
						data += 2;
					} else {
						newd--;
					}
						
					code = (unsigned short)(celdata&0xc000); celdata <<= 2; 

					switch (code) {											// code in top two bits of code
						case	0x8000:										// 4x4 vq code
							blit4_32( (byte *)&cin.vq4[(*data)*32], status[index], cinTable[currentHandle].samplesPerLine );
							data++;
							break;
						case	0xc000:										// 2x2 vq code
							blit2_32( (byte *)&cin.vq2[(*data)*8], status[index], cinTable[currentHandle].samplesPerLine );
							data++;
							blit2_32( (byte *)&cin.vq2[(*data)*8], status[index]+8, cinTable[currentHandle].samplesPerLine );
							data++;
							blit2_32( (byte *)&cin.vq2[(*data)*8], status[index]+cinTable[currentHandle].samplesPerLine*2, cinTable[currentHandle].samplesPerLine );
							data++;
							blit2_32( (byte *)&cin.vq2[(*data)*8], status[index]+cinTable[currentHandle].samplesPerLine*2+8, cinTable[currentHandle].samplesPerLine );
							data++;
							break;
						case	0x4000:										// motion compensation
							move4_32( status[index] + cin.mcomp[(*data)], status[index], cinTable[currentHandle].samplesPerLine );
							data++;
							break;
					}
					index++;
				}
				break;
			case	0x4000:													// motion compensation
				move8_32( status[index] + cin.mcomp[(*data)], status[index], cinTable[currentHandle].samplesPerLine );
				data++;
				index += 5;
				break;
			case	0x0000:
				index += 5;
				break;
		}
	} while ( status[index] != NULL );
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void ROQ_GenYUVTables( void )
{
	float t_ub,t_vr,t_ug,t_vg;
	long i;

	if (currentHandle < 0) return;

	t_ub = (1.77200f/2.0f) * (float)(1<<6) + 0.5f;
	t_vr = (1.40200f/2.0f) * (float)(1<<6) + 0.5f;
	t_ug = (0.34414f/2.0f) * (float)(1<<6) + 0.5f;
	t_vg = (0.71414f/2.0f) * (float)(1<<6) + 0.5f;
	for(i=0;i<256;i++) {
		float x = (float)(2 * i - 255);
	
		cin.ROQ_UB_tab[i] = (long)( ( t_ub * x) + (1<<5));
		cin.ROQ_VR_tab[i] = (long)( ( t_vr * x) + (1<<5));
		cin.ROQ_UG_tab[i] = (long)( (-t_ug * x)		 );
		cin.ROQ_VG_tab[i] = (long)( (-t_vg * x) + (1<<5));
		cin.ROQ_YY_tab[i] = (long)( (i << 6) | (i >> 2) );
	}
}

#define VQ2TO4(a,b,c,d) { \
    	*c++ = a[0];	\
	*d++ = a[0];	\
	*d++ = a[0];	\
	*c++ = a[1];	\
	*d++ = a[1];	\
	*d++ = a[1];	\
	*c++ = b[0];	\
	*d++ = b[0];	\
	*d++ = b[0];	\
	*c++ = b[1];	\
	*d++ = b[1];	\
	*d++ = b[1];	\
	*d++ = a[0];	\
	*d++ = a[0];	\
	*d++ = a[1];	\
	*d++ = a[1];	\
	*d++ = b[0];	\
	*d++ = b[0];	\
	*d++ = b[1];	\
	*d++ = b[1];	\
	a += 2; b += 2; }
 
#define VQ2TO2(a,b,c,d) { \
	*c++ = *a;	\
	*d++ = *a;	\
	*d++ = *a;	\
	*c++ = *b;	\
	*d++ = *b;	\
	*d++ = *b;	\
	*d++ = *a;	\
	*d++ = *a;	\
	*d++ = *b;	\
	*d++ = *b;	\
	a++; b++; }

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/
/*
static unsigned short yuv_to_rgb( long y, long u, long v )
{ 
	long r,g,b,YY = (long)(cin.ROQ_YY_tab[(y)]);

	r = (YY + cin.ROQ_VR_tab[v]) >> 9;
	g = (YY + cin.ROQ_UG_tab[u] + cin.ROQ_VG_tab[v]) >> 8;
	b = (YY + cin.ROQ_UB_tab[u]) >> 9;
	
	if (r<0) r = 0; if (g<0) g = 0; if (b<0) b = 0;
	if (r > 31) r = 31; if (g > 63) g = 63; if (b > 31) b = 31;

	return (unsigned short)((r<<11)+(g<<5)+(b));
}
*/
/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static unsigned int yuv_to_rgb24( long y, long u, long v )
{ 
	long r,g,b,YY = (long)(cin.ROQ_YY_tab[(y)]);

	r = (YY + cin.ROQ_VR_tab[v]) >> 6;
	g = (YY + cin.ROQ_UG_tab[u] + cin.ROQ_VG_tab[v]) >> 6;
	b = (YY + cin.ROQ_UB_tab[u]) >> 6;
	
	if (r<0) r = 0; if (g<0) g = 0; if (b<0) b = 0;
	if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
	
	return LittleLong((r)+(g<<8)+(b<<16));
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void decodeCodeBook( byte *input, unsigned short roq_flags )
{
	long	i, j, two, four;
	unsigned short	*bptr;
	long	y0,y1,y2,y3,cr,cb;
	unsigned int *iaptr, *ibptr, *icptr, *idptr;

	if (currentHandle < 0) return;

	if (!roq_flags) {
		two = four = 256;
	} else {
		two  = roq_flags>>8;
		if (!two) two = 256;
		four = roq_flags&0xff;
	}

	four *= 2;

	bptr = (unsigned short *)cin.vq2;

	//
	// normal height
	//
	ibptr = (unsigned int *)bptr;
	for(i=0;i<two;i++) {
		y0 = (long)*input++;
		y1 = (long)*input++;
		y2 = (long)*input++;
		y3 = (long)*input++;
		cr = (long)*input++;
		cb = (long)*input++;
		*ibptr++ = yuv_to_rgb24( y0, cr, cb );
		*ibptr++ = yuv_to_rgb24( y1, cr, cb );
		*ibptr++ = yuv_to_rgb24( y2, cr, cb );
		*ibptr++ = yuv_to_rgb24( y3, cr, cb );
	}
	
	icptr = (unsigned int *)cin.vq4;
	idptr = (unsigned int *)cin.vq8;
	
	for(i=0;i<four;i++) {
		iaptr = (unsigned int *)cin.vq2 + (*input++)*4;
		ibptr = (unsigned int *)cin.vq2 + (*input++)*4;
		for(j=0;j<2;j++) 
			VQ2TO4(iaptr, ibptr, icptr, idptr);
	}
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void recurseQuad( long startX, long startY, long quadSize, long xOff, long yOff )
{
	byte *scroff;
	long bigx, bigy, lowx, lowy, useY;
	long offset;

	offset = cinTable[currentHandle].screenDelta;
	
	lowx = lowy = 0;
	bigx = cinTable[currentHandle].xsize;
	bigy = cinTable[currentHandle].ysize;

	if (bigx > cinTable[currentHandle].CIN_WIDTH) bigx = cinTable[currentHandle].CIN_WIDTH;
	if (bigy > cinTable[currentHandle].CIN_HEIGHT) bigy = cinTable[currentHandle].CIN_HEIGHT;

	if ( (startX >= lowx) && (startX+quadSize) <= (bigx) && (startY+quadSize) <= (bigy) && (startY >= lowy) && quadSize <= MAXSIZE) {
		useY = startY;
		scroff = cin.linbuf + (useY+((cinTable[currentHandle].CIN_HEIGHT-bigy)>>1)+yOff)*(cinTable[currentHandle].samplesPerLine) + (((startX+xOff))*4);

		cin.qStatus[0][cinTable[currentHandle].onQuad  ] = scroff;
		cin.qStatus[1][cinTable[currentHandle].onQuad++] = scroff+offset;
	}

	if ( quadSize != MINSIZE ) {
		quadSize >>= 1;
		recurseQuad( startX,		  startY		  , quadSize, xOff, yOff );
		recurseQuad( startX+quadSize, startY		  , quadSize, xOff, yOff );
		recurseQuad( startX,		  startY+quadSize , quadSize, xOff, yOff );
		recurseQuad( startX+quadSize, startY+quadSize , quadSize, xOff, yOff );
	}
}


/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void setupQuad( long xOff, long yOff )
{
	long numQuadCels, i,x,y;
	byte *temp;

	if (currentHandle < 0) return;

	if (xOff == cin.oldXOff && yOff == cin.oldYOff && cinTable[currentHandle].ysize == cin.oldysize && cinTable[currentHandle].xsize == cin.oldxsize) {
		return;
	}

	cin.oldXOff = xOff;
	cin.oldYOff = yOff;
	cin.oldysize = cinTable[currentHandle].ysize;
	cin.oldxsize = cinTable[currentHandle].xsize;

	numQuadCels  = (cinTable[currentHandle].CIN_WIDTH*cinTable[currentHandle].CIN_HEIGHT) / (16);
	numQuadCels += numQuadCels/4 + numQuadCels/16;
	numQuadCels += 64;							  // for overflow

	numQuadCels  = (cinTable[currentHandle].xsize*cinTable[currentHandle].ysize) / (16);
	numQuadCels += numQuadCels/4;
	numQuadCels += 64;							  // for overflow

	cinTable[currentHandle].onQuad = 0;

	for(y=0;y<(long)cinTable[currentHandle].ysize;y+=16) 
		for(x=0;x<(long)cinTable[currentHandle].xsize;x+=16) 
			recurseQuad( x, y, 16, xOff, yOff );

	temp = NULL;

	for(i=(numQuadCels-64);i<numQuadCels;i++) {
		cin.qStatus[0][i] = temp;			  // eoq
		cin.qStatus[1][i] = temp;			  // eoq
	}
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void readQuadInfo( byte *qData )
{
	if (currentHandle < 0) return;

	cinTable[currentHandle].xsize    = qData[0]+qData[1]*256;
	cinTable[currentHandle].ysize    = qData[2]+qData[3]*256;
	cinTable[currentHandle].maxsize  = qData[4]+qData[5]*256;
	cinTable[currentHandle].minsize  = qData[6]+qData[7]*256;
	
	cinTable[currentHandle].CIN_HEIGHT = cinTable[currentHandle].ysize;
	cinTable[currentHandle].CIN_WIDTH  = cinTable[currentHandle].xsize;

	cinTable[currentHandle].samplesPerLine = cinTable[currentHandle].CIN_WIDTH*4;
	cinTable[currentHandle].screenDelta = cinTable[currentHandle].CIN_HEIGHT*cinTable[currentHandle].samplesPerLine;
	
	cinTable[currentHandle].VQ0 = cinTable[currentHandle].VQNormal;
	cinTable[currentHandle].VQ1 = cinTable[currentHandle].VQBuffer;

	cinTable[currentHandle].t[0] = (0 - (unsigned int)cin.linbuf)+(unsigned int)cin.linbuf+cinTable[currentHandle].screenDelta;
	cinTable[currentHandle].t[1] = (0 - ((unsigned int)cin.linbuf + cinTable[currentHandle].screenDelta))+(unsigned int)cin.linbuf;

	cinTable[currentHandle].drawX = cinTable[currentHandle].CIN_WIDTH;
	cinTable[currentHandle].drawY = cinTable[currentHandle].CIN_HEIGHT;
	// jic the card sucks
	if ( glConfig.maxTextureSize <= 256) {
        if (cinTable[currentHandle].drawX>256) {
            cinTable[currentHandle].drawX = 256;
        }
        if (cinTable[currentHandle].drawY>256) {
            cinTable[currentHandle].drawY = 256;
        }
		if (cinTable[currentHandle].CIN_WIDTH != 256 || cinTable[currentHandle].CIN_HEIGHT != 256) {
			Com_DPrintf("HACK: approxmimating cinematic for Rage Pro or Voodoo\n");
		}
	}
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void RoQPrepMcomp( long xoff, long yoff ) 
{
	long i, j, x, y, temp, temp2;

	if (currentHandle < 0) return;

	i=cinTable[currentHandle].samplesPerLine; 
	j=4;
	if ( cinTable[currentHandle].xsize == (cinTable[currentHandle].ysize*4) ) 
	{ 
		j = j+j; 
		i = i+i; 
	}
	
	for(y=0;y<16;y++) {
		temp2 = (y+yoff-8)*i;
		for(x=0;x<16;x++) {
			temp = (x+xoff-8)*j;
			cin.mcomp[(x*16)+y] = cinTable[currentHandle].normalBuffer0-(temp2+temp);
		}
	}
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void initRoQ() 
{
	if (currentHandle < 0) return;

	cinTable[currentHandle].VQNormal = (void (*)(byte *, void *))blitVQQuad32fs;
	cinTable[currentHandle].VQBuffer = (void (*)(byte *, void *))blitVQQuad32fs;
	ROQ_GenYUVTables();
	RllSetupTable();
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/
/*
static byte* RoQFetchInterlaced( byte *source ) {
	int x, *src, *dst;

	if (currentHandle < 0) return NULL;

	src = (int *)source;
	dst = (int *)cinTable[currentHandle].buf2;

	for(x=0;x<256*256;x++) {
		*dst = *src;
		dst++; src += 2;
	}
	return cinTable[currentHandle].buf2;
}
*/
static void RoQReset() {
	
	if (currentHandle < 0) return;

	if (cinTable[currentHandle].iFile) {
		Sys_EndStreamedFile(cinTable[currentHandle].iFile);
		FS_Seek(cinTable[currentHandle].iFile, 0, FS_SEEK_SET);
		FS_Read (cin.file, 16, cinTable[currentHandle].iFile);
		RoQ_init();
		// let the background thread start reading ahead
		Sys_BeginStreamedFile( cinTable[currentHandle].iFile, 0x10000 );
		cinTable[currentHandle].status = FMV_LOOPED;
	}
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void RoQInterrupt(void)
{
	byte				*framedata;

	if (currentHandle < 0) return;

	Sys_StreamedRead( cin.file, cinTable[currentHandle].RoQFrameSize+8, 1, cinTable[currentHandle].iFile );
	if ( cinTable[currentHandle].RoQPlayed >= cinTable[currentHandle].ROQSize ) { 
		if (cinTable[currentHandle].holdAtEnd==qfalse) {
			if (cinTable[currentHandle].looping) {
				RoQReset();
			} else {
				cinTable[currentHandle].status = FMV_EOF;
				if (cinTable[currentHandle].hSFX && !cinTable[currentHandle].looping)
				{
					S_CIN_StopSound( cinTable[currentHandle].hSFX );
				}
			}
		} else {
			cinTable[currentHandle].status = FMV_IDLE;
		}
		return; 
	}

	framedata = cin.file;
//
// new frame is ready
//
redump:
	switch(cinTable[currentHandle].roq_id) 
	{
		case	ROQ_QUAD_VQ:
			if ((cinTable[currentHandle].numQuads&1)) {
				cinTable[currentHandle].normalBuffer0 = cinTable[currentHandle].t[1];
				RoQPrepMcomp( cinTable[currentHandle].roqF0, cinTable[currentHandle].roqF1 );
				cinTable[currentHandle].VQ1( (byte *)cin.qStatus[1], framedata);
				cinTable[currentHandle].buf = 	cin.linbuf + cinTable[currentHandle].screenDelta;
			} else {
				cinTable[currentHandle].normalBuffer0 = cinTable[currentHandle].t[0];
				RoQPrepMcomp( cinTable[currentHandle].roqF0, cinTable[currentHandle].roqF1 );
				cinTable[currentHandle].VQ0( (byte *)cin.qStatus[0], framedata );
				cinTable[currentHandle].buf = 	cin.linbuf;
			}
			if (cinTable[currentHandle].numQuads == 0) {		// first frame
				memcpy(cin.linbuf+cinTable[currentHandle].screenDelta, cin.linbuf, cinTable[currentHandle].samplesPerLine*cinTable[currentHandle].ysize);
			}
			cinTable[currentHandle].numQuads++;
			cinTable[currentHandle].dirty = qtrue;
			break;
		case	ROQ_CODEBOOK:
			decodeCodeBook( framedata, (unsigned short)cinTable[currentHandle].roq_flags );
			break;
		case	ZA_SOUND_MONO:
			if (!cinTable[currentHandle].silent) {
				if (cinTable[currentHandle].numQuads == -1) {
					S_Update();
					s_rawend = s_soundtime;
					RllDecodeMonoToStereo( framedata, cinTable[currentHandle].RoQFrameSize, 0, (unsigned short)cinTable[currentHandle].roq_flags, !!(cinTable[currentHandle].hSFX) );
				}
				else
				{
					if (cinTable[currentHandle].hSFX) {
						S_Update();
					}
					RllDecodeMonoToStereo( framedata, cinTable[currentHandle].RoQFrameSize, 0, (unsigned short)cinTable[currentHandle].roq_flags, !!(cinTable[currentHandle].hSFX) );
				}
			}
			break;
		case	ZA_SOUND_STEREO:
			if (!cinTable[currentHandle].silent) {
				if (cinTable[currentHandle].numQuads == -1) {
					S_Update();
					s_rawend = s_soundtime;
					RllDecodeStereoToStereo( framedata, cinTable[currentHandle].RoQFrameSize, 0, (unsigned short)cinTable[currentHandle].roq_flags, !!(cinTable[currentHandle].hSFX) );
				}
				else
				{
					if (cinTable[currentHandle].hSFX) {
						S_Update();
					}
					RllDecodeStereoToStereo( framedata, cinTable[currentHandle].RoQFrameSize, 0, (unsigned short)cinTable[currentHandle].roq_flags, !!(cinTable[currentHandle].hSFX) );
				}
			}
			break;
		case	ROQ_QUAD_INFO:
			if (cinTable[currentHandle].numQuads == -1) {
				readQuadInfo( framedata );
				setupQuad( 0, 0 );
				cinTable[currentHandle].startTime = cinTable[currentHandle].lastTime = Sys_Milliseconds()*com_timescale->value;
			}
			if (cinTable[currentHandle].numQuads != 1) cinTable[currentHandle].numQuads = 0;
			break;
		case	ROQ_PACKET:
			cinTable[currentHandle].inMemory = cinTable[currentHandle].roq_flags;
			cinTable[currentHandle].RoQFrameSize = 0;           // for header
			break;
		case	ROQ_QUAD_HANG:
			cinTable[currentHandle].RoQFrameSize = 0;
			break;
		case	ROQ_QUAD_JPEG:
			break;
		default:
			cinTable[currentHandle].status = FMV_EOF;
			break;
	}	
//
// read in next frame data
//
	if ( cinTable[currentHandle].RoQPlayed >= cinTable[currentHandle].ROQSize ) { 
		if (cinTable[currentHandle].holdAtEnd==qfalse) {
			if (cinTable[currentHandle].looping) {
				RoQReset();
			} else {
				cinTable[currentHandle].status = FMV_EOF;
			}
		} else {
			cinTable[currentHandle].status = FMV_IDLE;
		}
		return; 
	}
	
	framedata		 += cinTable[currentHandle].RoQFrameSize;
	cinTable[currentHandle].roq_id		 = framedata[0] + framedata[1]*256;
	cinTable[currentHandle].RoQFrameSize = framedata[2] + framedata[3]*256 + framedata[4]*65536;
	cinTable[currentHandle].roq_flags	 = framedata[6] + framedata[7]*256;
	cinTable[currentHandle].roqF0		 = (char)framedata[7];
	cinTable[currentHandle].roqF1		 = (char)framedata[6];

	if (cinTable[currentHandle].RoQFrameSize>65536||cinTable[currentHandle].roq_id==0x1084) {
		Com_DPrintf("roq_size>65536||roq_id==0x1084\n");
		cinTable[currentHandle].status = FMV_EOF;
		if (cinTable[currentHandle].looping) {
			RoQReset();
		}
		return;
	}
	if (cinTable[currentHandle].inMemory && (cinTable[currentHandle].status != FMV_EOF)) 
	{ cinTable[currentHandle].inMemory--; framedata += 8; goto redump; }
//
// one more frame hits the dust
//
//	assert(cinTable[currentHandle].RoQFrameSize <= 65536);
//	r = Sys_StreamedRead( cin.file, cinTable[currentHandle].RoQFrameSize+8, 1, cinTable[currentHandle].iFile );
	cinTable[currentHandle].RoQPlayed	+= cinTable[currentHandle].RoQFrameSize+8;
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void RoQ_init( void )
{

	cinTable[currentHandle].startTime = cinTable[currentHandle].lastTime = Sys_Milliseconds()*com_timescale->value;

	cinTable[currentHandle].RoQPlayed = 24;

/*	get frame rate */	
	cinTable[currentHandle].roqFPS	 = cin.file[ 6] + cin.file[ 7]*256;
	
	if (!cinTable[currentHandle].roqFPS) cinTable[currentHandle].roqFPS = 30;
	

	cinTable[currentHandle].numQuads = -1;

	cinTable[currentHandle].roq_id		= cin.file[ 8] + cin.file[ 9]*256;
	cinTable[currentHandle].RoQFrameSize	= cin.file[10] + cin.file[11]*256 + cin.file[12]*65536;
	cinTable[currentHandle].roq_flags	= cin.file[14] + cin.file[15]*256;

	if (cinTable[currentHandle].RoQFrameSize > 65536 || !cinTable[currentHandle].RoQFrameSize) { 
		return;
	}

	if (cinTable[currentHandle].hSFX)
	{
		S_StartLocalSound(cinTable[currentHandle].hSFX, CHAN_AUTO);
	}
}

/******************************************************************************
*
* Function:		
*
* Description:	
*
******************************************************************************/

static void RoQShutdown( void ) {
	const char *s;
	
	if (!cinTable[currentHandle].buf) {
		if (cinTable[currentHandle].iFile) {
//			assert( 0 && "ROQ handle leak-prevention WAS needed!");
			Sys_EndStreamedFile( cinTable[currentHandle].iFile );
			FS_FCloseFile( cinTable[currentHandle].iFile );
			cinTable[currentHandle].iFile = 0;
			if (cinTable[currentHandle].hSFX) {
				S_CIN_StopSound( cinTable[currentHandle].hSFX );
			}
		}
		return;
	}

	if (cinTable[currentHandle].status == FMV_IDLE) {
		return;
	}

	Com_DPrintf("finished cinematic\n");
	cinTable[currentHandle].status = FMV_IDLE;

	if (cinTable[currentHandle].iFile) {
		Sys_EndStreamedFile( cinTable[currentHandle].iFile );
		FS_FCloseFile( cinTable[currentHandle].iFile );
		cinTable[currentHandle].iFile = 0;
		if (cinTable[currentHandle].hSFX) {
			S_CIN_StopSound( cinTable[currentHandle].hSFX );
		}
	}

	if (cinTable[currentHandle].alterGameState) {
		cls.state = CA_DISCONNECTED;
		// we can't just do a vstr nextmap, because
		// if we are aborting the intro cinematic with
		// a devmap command, nextmap would be valid by
		// the time it was referenced
		s = Cvar_VariableString( "nextmap" );
		if ( s[0] ) {
			Cbuf_ExecuteText( EXEC_APPEND, va("%s\n", s) );
			Cvar_Set( "nextmap", "" );
		}
		CL_handle = -1;
	}
	cinTable[currentHandle].fileName[0] = 0;
	currentHandle = -1;
}

e_status CIN_StopCinematic(int handle) {
	
	if (handle < 0 || handle>= MAX_VIDEO_HANDLES || cinTable[handle].status == FMV_EOF) return FMV_EOF;
	currentHandle = handle;

	Com_DPrintf("trFMV::stop(), closing %s\n", cinTable[currentHandle].fileName);

	if (!cinTable[currentHandle].buf) {
		if (cinTable[currentHandle].iFile) {
//			assert( 0 && "ROQ handle leak-prevention WAS needed!");
			Sys_EndStreamedFile( cinTable[currentHandle].iFile );
			FS_FCloseFile( cinTable[currentHandle].iFile );
			cinTable[currentHandle].iFile = 0;
			cinTable[currentHandle].fileName[0] = 0;
			if (cinTable[currentHandle].hSFX) {
				S_CIN_StopSound( cinTable[currentHandle].hSFX );
			}
		}
		return FMV_EOF;
	}

	if (cinTable[currentHandle].alterGameState) {
		if ( cls.state != CA_CINEMATIC ) {
			return cinTable[currentHandle].status;
		}
	}
	cinTable[currentHandle].status = FMV_EOF;
	if (strcmp(cinTable[currentHandle].fileName, "video/jk0101.roq") == 0) {
		// cinematic cutscene on first map (kejim_post) is over, so unpause the timer
		SpeedrunUnpauseTimer();
	}
	RoQShutdown();

	return FMV_EOF;
}

/*
==================
SCR_RunCinematic

Fetch and decompress the pending frame
==================
*/


e_status CIN_RunCinematic (int handle)
{
        // bk001204 - init
	int	start = 0;
	int     thisTime = 0;

	if (handle < 0 || handle>= MAX_VIDEO_HANDLES || cinTable[handle].status == FMV_EOF) 
	{
		return FMV_EOF;
	}

	if (currentHandle != handle) {
		currentHandle = handle;
		cinTable[currentHandle].status = FMV_EOF;
		RoQReset();
	}

	if (cinTable[handle].playonwalls < -1)
	{
		return cinTable[handle].status;
	}

	currentHandle = handle;

	if (cinTable[currentHandle].alterGameState) {
		if ( cls.state != CA_CINEMATIC ) {
			return cinTable[currentHandle].status;
		}
	}

	if (cinTable[currentHandle].status == FMV_IDLE) {
		return cinTable[currentHandle].status;
	}

	thisTime = Sys_Milliseconds()*com_timescale->value;
	if (cinTable[currentHandle].shader && (abs(thisTime - static_cast<long long>(cinTable[currentHandle].lastTime)))>100) {
		cinTable[currentHandle].startTime += thisTime - cinTable[currentHandle].lastTime;
	}
	cinTable[currentHandle].tfps = ((((Sys_Milliseconds()*com_timescale->value) - cinTable[currentHandle].startTime)*cinTable[currentHandle].roqFPS)/1000);

	start = cinTable[currentHandle].startTime;
	while(  (cinTable[currentHandle].tfps != cinTable[currentHandle].numQuads)
		&& (cinTable[currentHandle].status == FMV_PLAY) ) 
	{
		RoQInterrupt();
		if (start != cinTable[currentHandle].startTime) {
		  cinTable[currentHandle].tfps = ((((Sys_Milliseconds()*com_timescale->value)
							  - cinTable[currentHandle].startTime)*cinTable[currentHandle].roqFPS)/1000);
			start = cinTable[currentHandle].startTime;
		}
	}

	cinTable[currentHandle].lastTime = thisTime;

	if (cinTable[currentHandle].status == FMV_LOOPED) {
		cinTable[currentHandle].status = FMV_PLAY;
	}

	if (cinTable[currentHandle].status == FMV_EOF) {
	  if (cinTable[currentHandle].looping) {
		RoQReset();
	  } else {
		RoQShutdown();
		return FMV_IDLE;	//currentHandle is -1 now, so it can't fall through to the default return!
	  }
	}

	return cinTable[currentHandle].status;
}

void		Menus_CloseAll(void);
void		UI_Cursor_Show(qboolean flag);

/*
==================
CL_PlayCinematic

==================
*/
int CIN_PlayCinematic( const char *arg, int x, int y, int w, int h, int systemBits, const char *psAudioFile /* = NULL */ )
{
	unsigned short RoQID;
	char	name[MAX_OSPATH];
	int		i;

	if (strcmp(arg, "video/jk0101.roq") == 0) {
		// cinematic cutscene on first map (kejim_post) now starting, so start
		// a new run by resetting and pausing the timer
		SpeedrunResetTimer();
		SpeedrunPauseTimer();
	}

	if (strstr(arg, "/") == NULL && strstr(arg, "\\") == NULL) {
		Com_sprintf (name, sizeof(name), "video/%s", arg);
	} else {
		Com_sprintf (name, sizeof(name), "%s", arg);
	}
	COM_DefaultExtension(name,sizeof(name),".roq");

	if (!(systemBits & CIN_system)) {
		for ( i = 0 ; i < MAX_VIDEO_HANDLES ; i++ ) {
			if (!strcmp(cinTable[i].fileName, name) ) {
				return i;
			}
		}
	}

	Com_DPrintf("SCR_PlayCinematic( %s )\n", arg);

	memset(&cin, 0, sizeof(cinematics_t) );
	currentHandle = CIN_HandleForVideo();
	
	strcpy(cinTable[currentHandle].fileName, name);

	cinTable[currentHandle].ROQSize = 0;
	cinTable[currentHandle].ROQSize = FS_FOpenFileRead (cinTable[currentHandle].fileName, &cinTable[currentHandle].iFile, qtrue);

	if (cinTable[currentHandle].ROQSize<=0) {
		Com_Printf(S_COLOR_RED"ERROR: playCinematic: %s not found!\n", arg);
		cinTable[currentHandle].fileName[0] = 0;
		return -1;
	}

	CIN_SetExtents(currentHandle, x, y, w, h);
	CIN_SetLooping(currentHandle, (systemBits & CIN_loop)!=0);

	cinTable[currentHandle].CIN_HEIGHT = DEFAULT_CIN_HEIGHT;
	cinTable[currentHandle].CIN_WIDTH  =  DEFAULT_CIN_WIDTH;
	cinTable[currentHandle].holdAtEnd = (systemBits & CIN_hold) != 0;
	cinTable[currentHandle].alterGameState = (systemBits & CIN_system) != 0;
	cinTable[currentHandle].playonwalls = 1;
	cinTable[currentHandle].silent = (systemBits & CIN_silent) != 0;
	cinTable[currentHandle].shader = (systemBits & CIN_shader) != 0;
	if (psAudioFile)
	{
		cinTable[currentHandle].hSFX = S_RegisterSound(psAudioFile);
	}
	else
	{
		cinTable[currentHandle].hSFX = 0;
	}
	cinTable[currentHandle].hCRAWLTEXT = 0;

	if (cinTable[currentHandle].alterGameState) 
	{
		// close the menu
// TA...
//		if ( uivm ) 
//		{
//			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NONE );
//		}
// CHC:
		Con_Close();
		if (cls.uiStarted)
		{
			UI_Cursor_Show(qfalse);
			Menus_CloseAll();
		}
	}
	else 
	{
		cinTable[currentHandle].playonwalls = cl_ingameVideo->integer;
	}

	initRoQ();
					
	FS_Read (cin.file, 16, cinTable[currentHandle].iFile);

	RoQID = (unsigned short)(cin.file[0]) + (unsigned short)(cin.file[1])*256;
	if (RoQID == 0x1084)
	{
		RoQ_init();
//		FS_Read (cin.file, cinTable[currentHandle].RoQFrameSize+8, cinTable[currentHandle].iFile);
		// let the background thread start reading ahead
		Sys_BeginStreamedFile( cinTable[currentHandle].iFile, 0x10000 );

		cinTable[currentHandle].status = FMV_PLAY;
		Com_DPrintf("trFMV::play(), playing %s\n", arg);

		if (cinTable[currentHandle].alterGameState) {
			cls.state = CA_CINEMATIC;
		}
		
		Con_Close();

		s_rawend = s_soundtime;

		return currentHandle;
	}
	Com_DPrintf("trFMV::play(), invalid RoQ ID\n");

	RoQShutdown();
	return -1;
}

void CIN_SetExtents (int handle, int x, int y, int w, int h) {
	if (handle < 0 || handle>= MAX_VIDEO_HANDLES || cinTable[handle].status == FMV_EOF) return;
	cinTable[handle].xpos = x;
	cinTable[handle].ypos = y;
	cinTable[handle].width = w;
	cinTable[handle].height = h;
	cinTable[handle].dirty = qtrue;
}

void CIN_SetLooping(int handle, qboolean loop) {
	if (handle < 0 || handle>= MAX_VIDEO_HANDLES || cinTable[handle].status == FMV_EOF) return;
	cinTable[handle].looping = loop;
}

// Text crawl defines
#define TC_PLANE_WIDTH	250
#define TC_PLANE_NEAR	90
#define TC_PLANE_FAR	715
#define TC_PLANE_TOP	0
#define TC_PLANE_BOTTOM	1100

#define TC_DELAY 9000
#define TC_STOPTIME 81000
static void CIN_AddTextCrawl()
{
	refdef_t	refdef;
	polyVert_t	verts[4];

	// Set up refdef
	memset( &refdef, 0, sizeof( refdef ));

	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );

	refdef.fov_x = 130;
	refdef.fov_y = 130;

	refdef.x = 0;
	refdef.y = -50;
	refdef.width = cls.glconfig.vidWidth;
	refdef.height = cls.glconfig.vidHeight * 2; // deliberately extend off the bottom of the screen

	// use to set shaderTime for scrolling shaders
	refdef.time = 0; 

	// Set up the poly verts
	float fadeDown = 1.0;
	if (cls.realtime-CL_iPlaybackStartTime >= (TC_STOPTIME-2500))
	{
		fadeDown = (TC_STOPTIME - (cls.realtime-CL_iPlaybackStartTime))/ 2480.0f;
		if (fadeDown < 0)
		{
			fadeDown = 0;
		}
		if (fadeDown > 1)
		{
			fadeDown = 1;
		}
	}
	for ( int i = 0; i < 4; i++ )
	{
		verts[i].modulate[0] = 255*fadeDown; // gold color?
		verts[i].modulate[1] = 235*fadeDown;
		verts[i].modulate[2] = 127*fadeDown;
		verts[i].modulate[3] = 255*fadeDown;
	}

	_VectorScale( verts[2].modulate, 0.1f, verts[2].modulate ); // darken at the top??
	_VectorScale( verts[3].modulate, 0.1f, verts[3].modulate );

#define TIMEOFFSET  +(cls.realtime-CL_iPlaybackStartTime-TC_DELAY)*0.000015f -1
	VectorSet( verts[0].xyz, TC_PLANE_NEAR, -TC_PLANE_WIDTH, TC_PLANE_TOP );
	verts[0].st[0] = 1;
	verts[0].st[1] = 1 TIMEOFFSET;

	VectorSet( verts[1].xyz, TC_PLANE_NEAR, TC_PLANE_WIDTH, TC_PLANE_TOP );
	verts[1].st[0] = 0;
	verts[1].st[1] = 1 TIMEOFFSET;

	VectorSet( verts[2].xyz, TC_PLANE_FAR, TC_PLANE_WIDTH, TC_PLANE_BOTTOM );
	verts[2].st[0] = 0;
	verts[2].st[1] = 0 TIMEOFFSET;

	VectorSet( verts[3].xyz, TC_PLANE_FAR, -TC_PLANE_WIDTH, TC_PLANE_BOTTOM );
	verts[3].st[0] = 1;
	verts[3].st[1] = 0 TIMEOFFSET;

	// render it out
	re.ClearScene();
	re.AddPolyToScene( cinTable[CL_handle].hCRAWLTEXT, 4, verts );
	re.RenderScene( &refdef );

	//time's up
	if (cls.realtime-CL_iPlaybackStartTime >= TC_STOPTIME)
	{
//		cinTable[currentHandle].holdAtEnd = qfalse;
		cinTable[CL_handle].status = FMV_EOF;
		RoQShutdown();
		SCR_StopCinematic();	// change ROQ from FMV_IDLE to FMV_EOF, and clear some other vars
	}
}

/*
==================
SCR_DrawCinematic

==================
*/
void CIN_DrawCinematic (int handle) {
	float	x, y, w, h;

	if (handle < 0 || handle>= MAX_VIDEO_HANDLES || cinTable[handle].status == FMV_EOF) {
		return;
	}

	if (!cinTable[handle].buf) {
		return;
	}

	x = cinTable[handle].xpos;
	y = cinTable[handle].ypos;
	w = cinTable[handle].width;
	h = cinTable[handle].height;

	if (cinTable[handle].dirty && (cinTable[handle].CIN_WIDTH != cinTable[handle].drawX || cinTable[handle].CIN_HEIGHT != cinTable[handle].drawY)) {
		int ix, iy, *buf2, *buf3, xm, ym;

		xm = cinTable[handle].CIN_WIDTH/256;
		ym = cinTable[handle].CIN_HEIGHT/256;

		buf3 = (int*)cinTable[handle].buf;
		buf2 = (int*)Z_Malloc( 256*256*4, TAG_TEMP_WORKSPACE, qfalse );
		for (iy = 0; iy<256; iy++) {
			for (ix = 0; ix<256; ix++) {
				buf2[(iy<<8)+ix] = buf3[((iy*ym)*cinTable[handle].CIN_WIDTH) + (ix*xm)];
			}
		}
		re.DrawStretchRaw( x, y, w, h, 256, 256, (byte *)buf2, handle, qtrue);
		cinTable[handle].dirty = qfalse;
		Z_Free(buf2);

		return;
	}

	re.DrawStretchRaw( x, y, w, h, cinTable[handle].drawX, cinTable[handle].drawY, cinTable[handle].buf, handle, cinTable[handle].dirty);
	cinTable[handle].dirty = qfalse;
}

// external vars so I can check if the game is setup enough that I can play the intro video...
//
extern qboolean	com_fullyInitialized;
extern qboolean s_soundStarted, s_soundMuted;
//
// ... and if the app isn't ready yet (which should only apply for the intro video), then I use these...
//
static char	 sPendingCinematic_Arg	[256]={0};
static char	 sPendingCinematic_s	[256]={0};
static qboolean gbPendingCinematic = qfalse;
//
// This stuff is for EF1-type ingame cinematics...
//
static qboolean qbPlayingInGameCinematic = qfalse;
static qboolean qbInGameCinematicOnStandBy = qfalse;
static char	 sInGameCinematicStandingBy[MAX_QPATH];



static qboolean CIN_HardwareReadyToPlayVideos(void)
{
	if (com_fullyInitialized && cls.rendererStarted && 
								cls.soundStarted	&& 
								cls.soundRegistered
		)
	{
		return qtrue;
	}

	return qfalse;
}


static void PlayCinematic(const char *arg, const char *s, qboolean qbInGame)
{
	qboolean bFailed = qfalse;

	extern cvar_t *cl_skippingcin;
	if ( cl_skippingcin->integer )
	{
		SpeedrunUnpauseTimer();
	}
	Cvar_Set( "timescale", "1" );			// jic we were skipping a scripted cinematic, return to normal after playing video
	Cvar_Set( "skippingCinematic", "0" );	// "" 

	qbInGameCinematicOnStandBy = qfalse;

	int bits = qbInGame?0:CIN_system;

	Com_DPrintf("CL_PlayCinematic_f\n");

	char sTemp[1024];
	if (strstr(arg, "/") == NULL && strstr(arg, "\\") == NULL) {
		Com_sprintf (sTemp, sizeof(sTemp), "video/%s", arg);
	} else {
		Com_sprintf (sTemp, sizeof(sTemp), "%s", arg);
	}	
	COM_DefaultExtension(sTemp,sizeof(sTemp),".roq");
	arg = &sTemp[0];

	extern qboolean S_FileExists( const char *psFilename );
	if (S_FileExists( arg ))
	{
		SCR_StopCinematic();
		// command-line hack to avoid problems when playing intro video before app is fully setup...
		//
		if (!CIN_HardwareReadyToPlayVideos())	
		{
			strcpy(sPendingCinematic_Arg,arg);
			strcpy(sPendingCinematic_s , (s&&s[0])?s:"");
			gbPendingCinematic = qtrue;
			return;
		}

		qbPlayingInGameCinematic = qbInGame;

		if ((s && s[0] == '1') || Q_stricmp(arg,"video/end.roq")==0) {
			bits |= CIN_hold;
		}
		if (s && s[0] == '2') {
			bits |= CIN_loop;
		}

		S_StopAllSounds ();


		////////////////////////////////////////////////////////////////////
		//
		// work out associated audio-overlay file, if any...
		//
		extern cvar_t *s_language;
		qboolean	bIsForeign	= s_language && stricmp(s_language->string,"english") && stricmp(s_language->string,"");
		LPCSTR		psAudioFile	= NULL;
		qhandle_t	hCrawl = 0;
		if (!stricmp(arg,"video/jk0101_sw.roq"))
		{
			psAudioFile = "music/cinematic_1";
			if ( Cvar_VariableIntegerValue("com_demo") )
			{
				hCrawl = re.RegisterShader( "menu/video/tc_demo" );//demo version of text crawl
			}
			else
			{
				hCrawl = re.RegisterShader( va("menu/video/tc_%d",sp_language->integer) );
				if (!hCrawl)
				{
					hCrawl = re.RegisterShader( "menu/video/tc_0" );//failed, so go back to english
				}
			}
			bits |= CIN_hold;
		}
		else
		if (bIsForeign)
		{
			if (!stricmp(arg,"video/jk05.roq"))
			{
				psAudioFile = "sound/chars/video/cinematic_5";
				bits |= CIN_silent;	// knock out existing english track
			}
			else
			if (!stricmp(arg,"video/jk06.roq"))
			{
				psAudioFile = "sound/chars/video/cinematic_6";
				bits |= CIN_silent;	// knock out existing english track
			}
		}
		//
		////////////////////////////////////////////////////////////////////

		CL_handle = CIN_PlayCinematic( arg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, bits, psAudioFile );
		if (CL_handle >= 0) 
		{
			cinTable[CL_handle].hCRAWLTEXT = hCrawl;
			do 
			{
				SCR_RunCinematic();
			} 
			while (cinTable[currentHandle].buf == NULL && cinTable[currentHandle].status == FMV_PLAY);		// wait for first frame (load codebook and sound)
		
			if (qbInGame)
			{
				Cvar_SetValue( "cl_paused", 1);	// remove-menu call will have unpaused us, so we sometimes need to re-pause
			}

			CL_iPlaybackStartTime = cls.realtime;	// special use to avoid accidentally skipping ingame videos via fast-firing
		}
		else
		{
			// failed to open video...
			//
			bFailed = qtrue;
		}
	}
	else
	{
		// failed to open video...
		//
		bFailed = qtrue;
	}

	if (bFailed)
	{
		Com_Printf(S_COLOR_RED "PlayCinematic(): Failed to open \"%s\"\n",arg);
		//S_RestartMusic();	//restart the level music
		SCR_StopCinematic();	// I know this seems pointless, but it clears a bunch of vars as well
	}
	else
	{
		// this doesn't work for now...
		//
//		if (cls.state == CA_ACTIVE){
//			re.InitDissolve(qfalse);	// so we get a dissolve between previous screen image and cinematic
//		}
	}
}


qboolean CL_CheckPendingCinematic(void)
{
	if ( gbPendingCinematic && CIN_HardwareReadyToPlayVideos() )
	{
		gbPendingCinematic = qfalse;	// BEFORE next line, or we get recursion
		PlayCinematic(sPendingCinematic_Arg,sPendingCinematic_s[0]?sPendingCinematic_s:NULL,false);		
		return qtrue;
	}
	return qfalse;
}


void CL_PlayCinematic_f(void) 
{
	char	*arg, *s;
	
	arg = Cmd_Argv( 1 );
	s = Cmd_Argv(2);
	PlayCinematic(arg,s,qfalse);
}

void CL_PlayInGameCinematic_f(void)
{
	if (cls.state == CA_ACTIVE)
	{
		char *arg = Cmd_Argv( 1 );
		PlayCinematic(arg,NULL,qtrue);
	}
	else
	{
		qbInGameCinematicOnStandBy = qtrue;
		strcpy(sInGameCinematicStandingBy,Cmd_Argv(1));
	}
}


// Externally-called only, and only if cls.state == CA_CINEMATIC (or CL_IsRunningInGameCinematic() == true now)
//
void SCR_DrawCinematic (void) 
{
	if (CL_InGameCinematicOnStandBy())
	{
		PlayCinematic(sInGameCinematicStandingBy,NULL,qtrue);		
	}

	if (CL_handle >= 0 && CL_handle < MAX_VIDEO_HANDLES) {
		CIN_DrawCinematic(CL_handle);
		if (cinTable[CL_handle].hCRAWLTEXT && (cls.realtime - CL_iPlaybackStartTime >= TC_DELAY))
		{
			CIN_AddTextCrawl();
		}
	}
}

void SCR_RunCinematic (void)
{
	CL_CheckPendingCinematic();

	if (CL_handle >= 0 && CL_handle < MAX_VIDEO_HANDLES) {
		e_status Status = CIN_RunCinematic(CL_handle);
		
		if (CL_IsRunningInGameCinematic() && Status == FMV_IDLE  && !cinTable[CL_handle].holdAtEnd)
		{
			SCR_StopCinematic();	// change ROQ from FMV_IDLE to FMV_EOF, and clear some other vars
		}
	}
}

void SCR_StopCinematic( qboolean bAllowRefusal /* = qfalse */ )
{
	if (bAllowRefusal)
	{
		if ( (CL_handle >= 0 && CL_handle < MAX_VIDEO_HANDLES)
			&&
			cls.realtime < CL_iPlaybackStartTime + 1200	// 1.2 seconds have to have elapsed
			)
		{
			return;
		}
	}

	if ( CL_IsRunningInGameCinematic())
	{
		Com_DPrintf("In-game Cinematic Stopped\n");
	}

	if (CL_handle >= 0 && CL_handle < MAX_VIDEO_HANDLES) {
		CIN_StopCinematic(CL_handle);
		S_StopAllSounds ();
		CL_handle = -1;
		if (CL_IsRunningInGameCinematic()){
			re.InitDissolve(qfalse);	// dissolve from cinematic to underlying ingame
		}
	}

	if (cls.state == CA_CINEMATIC)
	{
		Com_DPrintf("Cinematic Stopped\n");
		cls.state =  CA_DISCONNECTED;
	}

	qbPlayingInGameCinematic = qfalse;
	qbInGameCinematicOnStandBy = qfalse;
	sInGameCinematicStandingBy[0]=0;
	Cvar_SetValue( "cl_paused", 0 );
	if (cls.state != CA_DISCONNECTED)	// cut down on needless calls to music code
	{
		S_RestartMusic();	//restart the level music
	}
}


void CIN_UploadCinematic(int handle) {
	if (handle >= 0 && handle < MAX_VIDEO_HANDLES) {
		if (!cinTable[handle].buf) {
			return;
		}
		if (cinTable[handle].playonwalls <= 0 && cinTable[handle].dirty) {
			if (cinTable[handle].playonwalls == 0) {
				cinTable[handle].playonwalls = -1;
			} else {
				if (cinTable[handle].playonwalls == -1) {
					cinTable[handle].playonwalls = -2;
				} else {
					cinTable[handle].dirty = qfalse;
				}
			}
		}
		re.UploadCinematic( cinTable[handle].drawX, cinTable[handle].drawY, cinTable[handle].buf, handle, cinTable[handle].dirty);
		if (cl_ingameVideo->integer == 0 && cinTable[handle].playonwalls == 1) {
			cinTable[handle].playonwalls--;
		}
	}
	else
	{
		// useful breakpoint
//		int z=1;
	}
}


qboolean CL_IsRunningInGameCinematic(void)
{
	return qbPlayingInGameCinematic;
}

qboolean CL_InGameCinematicOnStandBy(void)
{
	return qbInGameCinematicOnStandBy;
}


