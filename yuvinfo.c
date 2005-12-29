/*------------------------------------------------------------------------
 * yuvinfo, describes a YUV4MPEG stream
 * Copyright 2005 Johannes Lehtinen <johannes.lehtinen@iki.fi>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 *----------------------------------------------------------------------*/

#define PROGNAME "yuvinfo"
#define VERSION "1.0"
#define COPYRIGHT "Copyright 2005 Johannes Lehtinen"

#define DISPLAY_ALL 0
#define DISPLAY_LENGTH 1

#define MIN_Y 16
#define MAX_Y 235
#define MIN_UV 16
#define MAX_UV 240

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <yuv4mpeg.h>

static y4m_stream_info_t stream_info;
static int plane_count;
static int plane_length[Y4M_MAX_NUM_PLANES];
static int plane_width[Y4M_MAX_NUM_PLANES];
static int plane_height[Y4M_MAX_NUM_PLANES];
static uint8_t *planes[Y4M_MAX_NUM_PLANES];
static double sqrt2pi;

static void overlay_histograms(void);
static double ndf(double x, double avg, double stddev);

int main(int argc, char *argv[]) {
	int display = DISPLAY_ALL;
	int piping = 0;
	int show_histograms = 0;
	y4m_frame_info_t frame_info;
	int frame_length;
	int use_lseek;
	int length;
	int i;
	
	/* Initialize calculated constants */
	sqrt2pi = sqrt(2 * M_PI);
	
	/* Read options */
	while ((i = getopt(argc, argv, "hlcH")) != -1) {
		switch (i) {
			case 'h':
				fputs(
PROGNAME " " VERSION " - describes a YUV4MPEG stream\n"
COPYRIGHT "\n"
"\n"
"Describes a YUV4MPEG stream read from the standard input using an output\n"
"format similar to lavinfo. Optionally copies the input to the standard output\n"
"and can also overlay YUV histograms in the output video stream.\n"
"\n"
"usage: " PROGNAME " [-h] [-l] [-c] [-H]\n"
"options:\n"
"  -h     print this help text and exit\n"
"  -l     display only the length of the stream in frames\n"
"  -c     copy the input to stdout and write information to stderr\n"
"  -H     overlay YUV histograms in the output video stream (implies -c)\n",
					stdout);
				exit(0);
			case 'l':
				display = DISPLAY_LENGTH;
				break;
			case 'c':
				piping = 1;
				break;
			case 'H':
				show_histograms = 1;
				piping = 1;
				break;
			default:
				fputs(PROGNAME ": error: unknown option\n", stderr);
				exit(1);
		}
	}
	
	/* Read the stream header */
	y4m_allow_unknown_tags(1);
	y4m_accept_extensions(1);
	y4m_init_stream_info(&stream_info);
	if (y4m_read_stream_header(STDIN_FILENO, &stream_info) != Y4M_OK) {
		fputs(PROGNAME ": error: error reading stream header\n", stderr);
		exit(1);
	}
	
	/* Initialize internal data structures */
	frame_length = y4m_si_get_framelength(&stream_info);
	plane_count = y4m_si_get_plane_count(&stream_info);
	if(plane_count > Y4M_MAX_NUM_PLANES) {
		fputs(PROGNAME ": error: unsupported chroma mode\n", stderr);
		exit(1);
	}
	for (i = 0; i < plane_count; i++) {
		plane_length[i] = y4m_si_get_plane_length(&stream_info, i);
		plane_width[i] = y4m_si_get_plane_width(&stream_info, i);
		plane_height[i] = y4m_si_get_plane_height(&stream_info, i);
		if (show_histograms
			&& (plane_width[i] * plane_height[i] != plane_length[i]
				|| (i == 0
					&& (plane_width[i] != y4m_si_get_width(&stream_info)
						|| plane_height[i] != y4m_si_get_height(&stream_info))))) {
			fputs(PROGNAME
				": error: profiling not supported for this chroma mode\n",
				stderr);
			exit(1);
		}
	}
	if (show_histograms
		&& (plane_width[0] < 256 || plane_height[0] < plane_count * 64)) {
		fputs(PROGNAME ": error: frame too small for profiling overlay",
			stderr);
		exit(1);
	}

	/* Process the stream */
	y4m_init_frame_info(&frame_info);
	length = 0;
	if (piping || lseek(STDIN_FILENO, 0, SEEK_CUR) == -1) {
		use_lseek = 0;
		for (i = 0; i < plane_count; i++) {
			planes[i] = malloc(plane_length[i]);
			if (planes[i] == NULL) {
				fputs(PROGNAME ": error: memory allocation failed\n", stderr);
				exit(1);
			}
		}
	} else {
		use_lseek = 1;
	}
	if (piping) {
		if (y4m_write_stream_header(STDOUT_FILENO, &stream_info) != Y4M_OK) {
			fputs(PROGNAME ": error: error writing stream header\n", stderr);
			exit(1);
		}
	}	
	while ((i = y4m_read_frame_header(STDIN_FILENO, &stream_info, &frame_info)) == Y4M_OK) {
		if (use_lseek) {
			if (lseek(STDIN_FILENO, frame_length, SEEK_CUR) == -1) {
				fputs(PROGNAME ": error: error seeking frame data\n", stderr);
				exit(1);
			}
		} else {
			if (y4m_read_frame_data(STDIN_FILENO, &stream_info, &frame_info, planes) != Y4M_OK) {
				fputs(PROGNAME ": error: error reading frame data\n", stderr);
				exit(1);
			}
			if (show_histograms) {
				overlay_histograms();
			}
			if (piping && y4m_write_frame(STDOUT_FILENO, &stream_info, &frame_info, planes) != Y4M_OK) {
				fputs(PROGNAME ": error error writing frame\n", stderr);
				exit(1);
			}
		}
		length++;
	}
	if (i != Y4M_ERR_EOF) {
		fputs(PROGNAME ": error: error reading frame header\n", stderr);
		exit(1);
	}
	
	/* Finalize */
	y4m_fini_frame_info(&frame_info);
	y4m_fini_stream_info(&stream_info);

	/* Print information */
	switch (display) {
		case DISPLAY_LENGTH:
			fprintf(piping ? stderr : stdout, "%u\n", length);
			break;
		default:
		{
			char inter;
			char *norm;
			const char *chromakw;
			y4m_ratio_t fps;
			y4m_ratio_t sar;
			FILE *o = (piping ? stderr : stdout);
			
			fprintf(o, "video_frames=%u\n", length);
			fprintf(o, "video_width=%u\n", y4m_si_get_width(&stream_info));
			fprintf(o, "video_height=%u\n", y4m_si_get_height(&stream_info));
			switch (y4m_si_get_interlace(&stream_info)) {
				case Y4M_ILACE_NONE:
					inter = 'p';
					break;
				case Y4M_ILACE_TOP_FIRST:
					inter = 't';
					break;
				case Y4M_ILACE_BOTTOM_FIRST:
					inter = 'b';
					break;
				case Y4M_ILACE_MIXED:
					inter = 'm';
					break;
				default:
					inter = '?';
					break;
			}
			fprintf(o, "video_inter=%c\n", inter);
			fps = y4m_si_get_framerate(&stream_info);
			if (Y4M_RATIO_EQL(fps, y4m_fps_NTSC)
				|| Y4M_RATIO_EQL(fps, y4m_fps_NTSC_FILM)
				|| Y4M_RATIO_EQL(fps, y4m_fps_NTSC_FIELD)) {
				norm = "NTSC";
			} else if (Y4M_RATIO_EQL(fps, y4m_fps_PAL)
				|| Y4M_RATIO_EQL(fps, y4m_fps_PAL_FIELD)) {
				norm = "PAL";
			} else {
				norm = "unknown";
			}
			fprintf(o, "video_norm=%s\n", norm);
			fprintf(o, "video_fps=%.6f\n", Y4M_RATIO_DBL(fps));
			fprintf(o, "video_fps_ratio=%u:%u\n", fps.n, fps.d);
			sar = y4m_si_get_sampleaspect(&stream_info);
			fprintf(o, "video_sar_width=%u\n", sar.n);
			fprintf(o, "video_sar_height=%u\n", sar.d);
			fprintf(o, "video_sar_ratio=%u:%u\n", sar.n, sar.d);
			chromakw = y4m_chroma_keyword(y4m_si_get_chroma(&stream_info));
			fprintf(o, "chroma=%s\n", chromakw != NULL ? chromakw : "unknown");
			fputs("has_audio=0\n", o);
			break;
		}
	}
	
	return 0;
}

static void overlay_histograms(void) {
	int i;
	
	/* Calculate and overlay histograms for each plane */
	for (i = 0; i < plane_count; i++) {
		unsigned long vf[256];
		unsigned long max;
		double avg, var, stddev;
		int x, y;
		int j;
		uint8_t *p;

		/* Calculate value frequency */		
		for (j = 0; j < 256; j++) {
			vf[j] = 0;
		}
		p = planes[i];
		for (j = plane_length[i]; j != 0; j--) {
			vf[*p]++;
			p++;
		}
		
		/* Draw histogram background */
		y = plane_height[0] - 64 * (plane_count - i) + 4;
		x = (plane_width[0] - 256) / 2;
		p = planes[0] + y * plane_width[0] + x;
		for (y = 60; y != 0; y--) {
			for (j = 256; j != 0; j--) {
				*p = (*p + 3 * MIN_Y) / 4;
				p++;
			}
			p += plane_width[0] - 256;
		}
		
		/* Calculate average, variance and standard deviation */
		avg = 0;
		for (j = 0; j < 256; j++) {
			avg += ((double) vf[j] / plane_length[i]) * j;
		}
		var = 0;
		for (j = 0; j < 256; j++) {
			var += ((double) vf[j] / plane_length[i]) * pow(j - avg, 2);
		}
		stddev = sqrt(var);
		
		/* Calculate maximum frequency for scaling */
		max = 2 * ndf(avg, avg, stddev) * plane_length[i];
		
		/* Draw histogram */
		for (j = 0; j < 256; j++) {
			int h;
			
			x = (plane_width[0] - 256) / 2 + j;
			h = 60 * vf[j] / max;
			if (h > 60) {
				h = 60;
			}
			y = plane_height[0] - 64 * (plane_count - i - 1) - h;
			for (; h > 0; h--, y++) {
				p = planes[0] + y * plane_width[0] + x;
				*p = MAX_Y;
			}
		}
		
		/* Draw normal distribution */
		x = (plane_width[0] - 256) / 2;
		y = plane_height[0] - 64 * (plane_count - i - 1);
		for (j = 0; j < 256; j++, x++) {
			int h;
			
			h = rint(60 * plane_length[i] * ndf(j, avg, stddev) / max);
			if (h > 0 && h <= 60) {
				p = planes[0] + (y - h) * plane_width[0] + x;
				*p = (MIN_Y + MAX_Y) / 2;
			}
		}
	}
}

static double ndf(double x, double avg, double stddev) {
	return (1/(stddev * sqrt2pi)) * exp(-0.5 * pow((x - avg) / stddev, 2));
}
