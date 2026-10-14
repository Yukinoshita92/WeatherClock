#ifndef __IMAGE_H
#define __IMAGE_H

#include <stdint.h>

typedef struct image
{
	uint16_t height;
	uint16_t width;
	const uint8_t *model;
}image_t;

extern const image_t Image_main;
extern const image_t Image_error;
extern const image_t Image_wifi;
extern const image_t Image_wifi2;
extern const image_t Image_5g;
extern const image_t Image_charge;
extern const image_t Image_crowd;
extern const image_t Image_crowdwind;
extern const image_t Image_moon;
extern const image_t Image_rain;
extern const image_t Image_reloading;
extern const image_t Image_suncrowdwind;
extern const image_t Image_sun;
extern const image_t Image_sunset;
extern const image_t Image_sunrain;
extern const image_t Image_sunwind;
extern const image_t Image_temperature;
extern const image_t Image_yin;
extern const image_t Image_yuki;
extern const image_t Image_thunder;
extern const image_t Image_na;

#endif
