#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <setjmp.h>
#include <3ds.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <stdbool.h>
#include "quirc.h"
#define CONFIG_3D_SLIDERSTATE (*(volatile float*)0x1FF81080)
#define WAIT_TIMEOUT 1000000000ULL

#define WIDTH 400
#define HEIGHT 240
#define SCREEN_SIZE WIDTH * HEIGHT * 2
#define BPP_RGB565 2
#define BPP_GREYSCALE 1
#define BUF_SIZE_RGB565 SCREEN_SIZE * BPP_RGB565
#define BUF_SIZE_BPP_GREYSCALE SCREEN_SIZE * BPP_GREYSCALE

static jmp_buf exitJmp;
struct quirc *qr;


inline void clearScreen(void) {
	u8 *frame = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	memset(frame, 0, 320 * 240 * 3);
}

void hang(const char *message) {
	clearScreen();
	printf("%s", message);
	printf("Press start to exit");

	while (aptMainLoop()) {
		hidScanInput();

		u32 kHeld = hidKeysHeld();
		if (kHeld & KEY_START) longjmp(exitJmp, 1);
	}
}

void cleanup() {
	camExit();
	gfxExit();
	acExit();
}

void writePictureToFramebufferRGB565(void *fb, void *img, u16 x, u16 y, u16 width, u16 height) {
	u8 *fb_8 = (u8*) fb;
	u16 *img_16 = (u16*) img;
	int i, j, draw_x, draw_y;
	for(j = 0; j < height; j++) {
		for(i = 0; i < width; i++) {
			draw_y = y + height - j;
			draw_x = x + i;
			u32 v = (draw_y + draw_x * height) * 3;
			u16 data = img_16[j * width + i];
			uint8_t b = ((data >> 11) & 0x1F) << 3;
			uint8_t g = ((data >> 5) & 0x3F) << 2;
			uint8_t r = (data & 0x1F) << 3;
			fb_8[v] = r;
			fb_8[v+1] = g;
			fb_8[v+2] = b;
		}
	}
}

void writePictureToFramebufferGreyscale(void *fb, void *img, u16 x, u16 y, u16 width, u16 height) {
    u8 *fb_8 = (u8*) fb;
    u8 *img_8 = (u8*) img;
    int i, j, draw_x, draw_y;
    for(j = 0; j < height; j++) {
        for(i = 0; i < width; i++) {
            draw_y = y + height - j;
            draw_x = x + i;
            u32 v = (draw_y + draw_x * height) * 3;
            u8 data = img_8[j * width + i];

            fb_8[v] = data;
            fb_8[v+1] = data;
            fb_8[v+2] = data;
        }
    }
}

void rgb565_to_greyscale (void* img_rgb565, void* img_greyscale, u16 width, u16 height){
    u32 nPixels = width*height;

    u16 *rgb565 = (u16*) img_rgb565;
    u8 *greyscale = (u8*) img_greyscale;
    for (u32 i = 0; i < nPixels; ++i) {
        u16 px = rgb565[i];
        u8 r = (px & 0x1F) << 3;
        u8 g = ((px >> 5) & 0x3F) << 2;
        u8 b = ((px >> 11) & 0x1F) << 3;
        greyscale [i] = r * 0.21 + g * 0.72 + b * 0.07;
    }
}

void printf_if_result_nonzero(const char* fmt, Result r){
    if(r){
        printf(fmt, (unsigned int)r);
    }
}

void run_qr_test(u8* img_greyscale, int* w, int* h){
    u8* buf = quirc_begin(qr, w, h);
    printf("buffer acquired\n");
    memcpy(buf, img_greyscale, (*w)*(*h));
    printf("memcpy ok\n");

    quirc_end(qr);
    printf("qr_end\n");
    int count = quirc_count(qr);
    if(count){
        printf("found %d QR codes", count);

    }
};



int main() {
	// Initializations
	acInit();
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);

	// Enable double buffering to remove screen tearing
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);

	// Save current stack frame for easy exit
	if(setjmp(exitJmp)) {
		cleanup();
		return 0;
	}

	u32 kDown;
    u8* buf = (u8*)malloc(BUF_SIZE_RGB565);
    u8* buf_greyscale = (u8*)malloc(BUF_SIZE_BPP_GREYSCALE);
    qr = quirc_new();
    u32 bufSize;
    Handle camReceiveEvent = 0;
    if(!buf || !buf_greyscale || !qr) {
        hang("Failed to allocate memory!");
    }
    if (quirc_resize(qr, WIDTH, HEIGHT) < 0) {
        hang("Failed to allocate video memory");
    }

    printf("Initializing camera\n");

    printf_if_result_nonzero("camInit: 0x%08X\n", camInit());
    printf_if_result_nonzero("CAMU_SetSize: 0x%08X\n", CAMU_SetSize(SELECT_OUT1_OUT2, SIZE_CTR_TOP_LCD, CONTEXT_A));
    printf_if_result_nonzero("CAMU_SetOutputFormat: 0x%08X\n",
                             CAMU_SetOutputFormat(SELECT_OUT1_OUT2, OUTPUT_RGB_565, CONTEXT_A));
    // TODO: For some reason frame grabbing times out above 10fps. Figure out why this is.
    printf_if_result_nonzero("CAMU_SetFrameRate: 0x%08X\n", CAMU_SetFrameRate(SELECT_OUT1_OUT2, FRAME_RATE_10));
    printf_if_result_nonzero("CAMU_SetNoiseFilter: 0x%08X\n", CAMU_SetNoiseFilter(SELECT_OUT1_OUT2, true));
    printf_if_result_nonzero("CAMU_SetAutoExposure: 0x%08X\n", CAMU_SetAutoExposure(SELECT_OUT1_OUT2, true));
    printf_if_result_nonzero("CAMU_SetAutoWhiteBalance: 0x%08X\n", CAMU_SetAutoWhiteBalance(SELECT_OUT1_OUT2, true));
    // TODO: Figure out how to use the effects properly.
    //printf_if_result_nonzero("CAMU_SetEffect: 0x%08X\n", CAMU_SetEffect(SELECT_OUT1_OUT2, EFFECT_SEPIA, CONTEXT_A));
    printf_if_result_nonzero("CAMU_SetTrimming: 0x%08X\n", CAMU_SetTrimming(PORT_CAM1, false));
    printf_if_result_nonzero("CAMU_SetTrimming: 0x%08X\n", CAMU_SetTrimming(PORT_CAM2, false));
    //printf_if_result_nonzero("CAMU_SetTrimmingParamsCenter: 0x%08X\n",
    // CAMU_SetTrimmingParamsCenter(PORT_CAM1, 512, 240, 512, 384));
    printf_if_result_nonzero("CAMU_GetMaxBytes: 0x%08X\n", CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT));
    printf_if_result_nonzero("CAMU_SetTransferBytes: 0x%08X\n",
                             CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT));
    printf_if_result_nonzero("CAMU_Activate: 0x%08X\n", CAMU_Activate(SELECT_OUT1));
    printf_if_result_nonzero("CAMU_ClearBuffer: 0x%08X\n", CAMU_ClearBuffer(PORT_BOTH));
    printf_if_result_nonzero("CAMU_SynchronizeVsyncTiming: 0x%08X\n",
                             CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2));
    printf_if_result_nonzero("CAMU_StartCapture: 0x%08X\n", CAMU_StartCapture(PORT_BOTH));
//	printf_if_result_nonzero("CAMU_PlayShutterSound: 0x%08X\n", CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_MOVIE));

	gfxFlushBuffers();
	gspWaitForVBlank();
	gfxSwapBuffers();
	printf("Press Start to exit to Homebrew Launcher\n");

	// Main loop
	while (aptMainLoop()) {
		// Read which buttons are currently pressed or not
		hidScanInput();
		kDown = hidKeysDown();

		// If START button is pressed, break loop and quit
		if (kDown & KEY_START) {
			break;
		}
        printf_if_result_nonzero("CAMU_SetReceiving: 0x%08X\n",
                                 CAMU_SetReceiving(&camReceiveEvent, buf, PORT_CAM1, SCREEN_SIZE, (s16) bufSize));

        printf_if_result_nonzero("svcWaitSynchronization: 0x%08X\n",
                                 svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT));

        rgb565_to_greyscale(buf, buf_greyscale, WIDTH, HEIGHT);
        int width = WIDTH;
        int height = HEIGHT;

        printf_if_result_nonzero("svcCloseHandle: 0x%08X\n",
                                 svcCloseHandle(camReceiveEvent));
        writePictureToFramebufferGreyscale(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), buf_greyscale, 0, 0, WIDTH, HEIGHT);
        run_qr_test(buf_greyscale, &width, &height);

		// Flush and swap framebuffers
		gfxFlushBuffers();
		gspWaitForVBlank();
		gfxSwapBuffers();
	}

	printf_if_result_nonzero("CAMU_StopCapture: 0x%08X\n", CAMU_StopCapture(PORT_BOTH));

	printf_if_result_nonzero("CAMU_Activate: 0x%08X\n", CAMU_Activate(SELECT_NONE));

	// Exit
	free(buf);
	free(buf_greyscale);
    quirc_destroy(qr);
    cleanup();

	// Return to hbmenu
	return 0;
}
