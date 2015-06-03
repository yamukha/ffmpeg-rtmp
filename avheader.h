/*
 * avheader.h
 *
 *  Created on: May 28, 2015
 *      Author: amukha
 */

#ifndef AVHEADER_H_
#define AVHEADER_H_

#define FILTER_SIMPLE_BLUR

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/fifo.h>
#include <libavutil/mathematics.h>
#ifdef __cplusplus
}
#endif

#endif /* AVHEADER_H_ */
