/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <binder/ProcessState.h>

#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>

#include <ui/PixelFormat.h>

#include <SkImageEncoder.h>
#include <SkBitmap.h>
#include <SkData.h>
#include <SkStream.h>
#include "spi_tft.h"	
using namespace android;

static uint32_t DEFAULT_DISPLAY_ID = ISurfaceComposer::eDisplayIdMain;

#define RGB888_RED      0x00ff0000
#define RGB888_GREEN    0x0000ff00
#define RGB888_BLUE     0x000000ff

#define RGB565_RED      0xf800
#define RGB565_GREEN    0x07e0
#define RGB565_BLUE     0x001f

#define DISPLAY_H     320
#define DISPLAY_W	  240	

unsigned short RGB888ToRGB565(unsigned char cRed,unsigned char cGreen,unsigned char cBlue)
{
	unsigned short n565Color = 0;
 	int pixsel = 0;
	// 获取RGB单色，并截取高位
	//unsigned char cRed   = (n888Color & RGB888_RED)   >> 19;
	//unsigned char cGreen = (n888Color & RGB888_GREEN) >> 10;
	//unsigned char cBlue  = (n888Color & RGB888_BLUE)  >> 3;
 	
	// 连接
	return (unsigned short)((((cRed) << 8) & 0xF800) |   
            (((cGreen) << 3) & 0x7E0)  |  
            (((cBlue) >> 3)));  
	//n565Color = (cRed << 11) | (cGreen << 5) |(cBlue << 0);
	//return n565Color;
}
static void usage(const char* pname)
{
    fprintf(stderr,
            "usage: %s [-hp] [-d display-id] [FILENAME]\n"
            "   -h: this message\n"
            "   -p: save the file as a png.\n"
            "   -d: specify the display id to capture, default %d.\n"
            "If FILENAME ends with .png it will be saved as a png.\n"
            "If FILENAME is not given, the results will be printed to stdout.\n",
            pname, DEFAULT_DISPLAY_ID
    );
}

static SkColorType flinger2skia(PixelFormat f)
{
    switch (f) {
        case PIXEL_FORMAT_RGB_565:
            return kRGB_565_SkColorType;
        default:
            return kN32_SkColorType;
    }
}

static status_t vinfoToPixelFormat(const fb_var_screeninfo& vinfo,
        uint32_t* bytespp, uint32_t* f)
{

    switch (vinfo.bits_per_pixel) {
        case 16:
            *f = PIXEL_FORMAT_RGB_565;
            *bytespp = 2;
            break;
        case 24:
            *f = PIXEL_FORMAT_RGB_888;
            *bytespp = 3;
            break;
        case 32:
            // TODO: do better decoding of vinfo here
            *f = PIXEL_FORMAT_RGBX_8888;
            *bytespp = 4;
            break;
        default:
            return BAD_VALUE;
    }
    return NO_ERROR;
}

int main(int argc, char** argv)
{
    ProcessState::self()->startThreadPool();

    const char* pname = argv[0];
    bool png = false;
	
    bool init = false;
    int32_t displayId = DEFAULT_DISPLAY_ID;
    int c;
	unsigned char rgb_data[320*240*2] = {0};
	unsigned char* p_image;
	p_image = rgb_data;
    while ((c = getopt(argc, argv, "iphd:")) != -1) {
        switch (c) {
			case 'i':
				init = true;
				spi_tft_init();
				return 0;
				break;
            case 'p':
                png = true;
                break;
            case 'd':
                displayId = atoi(optarg);
                break;
            case '?':
            case 'h':
                usage(pname);
                return 1;
        }
    }
    argc -= optind;
    argv += optind;

    int fd = -1;
    if (argc == 0) {
        fd = dup(STDOUT_FILENO);
    } else if (argc == 1) {
        const char* fn = argv[0];
        fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0664);
        if (fd == -1) {
            fprintf(stderr, "Error opening file: %s (%s)\n", fn, strerror(errno));
            return 1;
        }
        const int len = strlen(fn);
        if (len >= 4 && 0 == strcmp(fn+len-4, ".png")) {
            png = true;
        }
    }
    
    if (fd == -1) {
        usage(pname);
        return 1;
    }

    void const* mapbase = MAP_FAILED;
    ssize_t mapsize = -1;

    void const* base = 0;
	
    void const* offtset_h = 0;
    uint32_t w, s, h, f;
    size_t size = 0;
	int i = 0;
	unsigned short  pixel_565;
	unsigned short  pixel_565_ld;
    ScreenshotClient screenshot;
/*
	if(init == true){
		spi_tft_init();
		dislpay_tft_init();
		return 0;
	}
*/
	
	dislpay_tft_init();
	while(1){
    sp<IBinder> display = SurfaceComposerClient::getBuiltInDisplay(displayId);
    if (display != NULL && screenshot.update(display, Rect(), false) == NO_ERROR) {
        base = screenshot.getPixels();
        w = screenshot.getWidth();
        h = screenshot.getHeight();
        s = screenshot.getStride();
        f = screenshot.getFormat();
        size = screenshot.getSize();		
    } else {
        const char* fbpath = "/dev/graphics/fb0";
        int fb = open(fbpath, O_RDONLY);
        if (fb >= 0) {
            struct fb_var_screeninfo vinfo;
            if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) == 0) {
                uint32_t bytespp;
                if (vinfoToPixelFormat(vinfo, &bytespp, &f) == NO_ERROR) {
                    size_t offset = (vinfo.xoffset + vinfo.yoffset*vinfo.xres) * bytespp;
                    w = vinfo.xres;
                    h = vinfo.yres;
                    s = vinfo.xres;
                    size = w*h*bytespp;
                    mapsize = offset + size;
                    mapbase = mmap(0, mapsize, PROT_READ, MAP_PRIVATE, fb, 0);
                    if (mapbase != MAP_FAILED) {
                        base = (void const *)((char const *)mapbase + offset);
						offtset_h = base;
                    }
                }
            }
            close(fb);
        }
    }

    if (base) {
        if (png) {
            const SkImageInfo info = SkImageInfo::Make(w, h, flinger2skia(f),
                                                       kPremul_SkAlphaType);
            SkBitmap b;
            b.installPixels(info, const_cast<void*>(base), s*bytesPerPixel(f));
            SkDynamicMemoryWStream stream;
            SkImageEncoder::EncodeStream(&stream, b,
                    SkImageEncoder::kPNG_Type, SkImageEncoder::kDefaultQuality);
            SkData* streamData = stream.copyToData();
            write(fd, streamData->data(), streamData->size());
            streamData->unref();
        } else {

            size_t Bpp = bytesPerPixel(f);
			offtset_h = base;
			p_image = rgb_data;
			/*
            for (size_t y=0 ; y<h/8 ; y++) {
				for(i =0;i < 640*Bpp;i+=5*Bpp){
					pixel_565 = RGB888ToRGB565(*(char*)(base),*(char*)(base+1),*(char*)(base+2));
					pixel_565_ld = ((pixel_565&0xFF)<<8)|((pixel_565&0xFF00)>>8);
					memcpy(p_image,&pixel_565_ld , sizeof(unsigned short));
					p_image +=sizeof(unsigned short);
                	base = (void *)((char *)base + 5*Bpp);
				}
                offtset_h = (void *)((char *)offtset_h + 8*s*Bpp);
				base = offtset_h;
            }
			*/
	        for (size_t y=0 ; y<DISPLAY_H ; y++) {
				
				for(i =0;i < DISPLAY_W;i++){
					pixel_565 = RGB888ToRGB565(*(char*)(base),*(char*)(base+1),*(char*)(base+2));
					pixel_565_ld = ((pixel_565&0xFF)<<8)|((pixel_565&0xFF00)>>8);
					memcpy(p_image,&pixel_565_ld , sizeof(unsigned short));
					p_image +=sizeof(unsigned short);
		        	base = (void *)((char *)base + Bpp);
				}
				
	        offtset_h = (void *)((char *)offtset_h + s*Bpp);
			base = offtset_h;
	    }	
        }
		display_image(rgb_data,sizeof(rgb_data));
    }
}
    close(fd);
    if (mapbase != MAP_FAILED) {
        munmap((void *)mapbase, mapsize);
    }
    return 0;
}
