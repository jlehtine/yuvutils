/*------------------------------------------------------------------------
 * yuvadjust, adjust white balance and contrast of YUV4MPEG streams
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

#define PROGNAME "yuvadjust"
#define VERSION "1.0"
#define COPYRIGHT "Copyright 2005 Johannes Lehtinen"

#define DEFAULT_BUFFER_SIZE 30

#define VERBOSE_DEBUG 2

#define OPER_LCONTRAST 1
#define OPER_WHITEBALANCE 2
#define OPER_CCONTRAST 4

#define MIN_Y 16
#define MAX_Y 235
#define MIN_UV 16
#define MAX_UV 240

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <assert.h>
#include <yuv4mpeg.h>

static int oper = 0;
static int only_half = 0;
static y4m_stream_info_t stream_info;
static int plane_count;
static int plane_width[Y4M_MAX_NUM_PLANES];
static int plane_height[Y4M_MAX_NUM_PLANES];
static int plane_length[Y4M_MAX_NUM_PLANES];
static uint8_t *(*planes)[Y4M_MAX_NUM_PLANES];
static y4m_frame_info_t *input_frame_infos;
static int (*favg)[Y4M_MAX_NUM_PLANES];
static int avg_sum[Y4M_MAX_NUM_PLANES];
static int buffer_count = 0;
static int buffer_head = 0;
static int buffer_tail = 0;
static int buffer_size = DEFAULT_BUFFER_SIZE;
static int buffer_pos = 0;
static int verbose = 0;
static int clip = 0;
static int input_frame_count = 0;
static int output_frame_count = 0;

static void parse_options(int argc, char *argv[]);
static int read_frame(void);
static void step_buffer(void);
static void analyze_buffered_frame(int i);
static void adjust_frame(int i);

int main(int argc, char *argv[]) {
	int i;

	/* Read options */
	parse_options(argc, argv);
	
	/* Initialize stream info and frame info */
	y4m_allow_unknown_tags(1);
	y4m_accept_extensions(1);
	y4m_init_stream_info(&stream_info);

	/* Read stream header */
	if (y4m_read_stream_header(STDIN_FILENO, &stream_info) != Y4M_OK) {
		fputs(PROGNAME ": error: could not read input stream header\n", stderr);
		exit(1);
	}

	/* Print input information if verbose */
	if (verbose) {
		const char *chrstr;
		
		chrstr = y4m_chroma_description(y4m_si_get_chroma(&stream_info));
		fprintf(stderr, PROGNAME ": input: chroma mode %s\n",
			(chrstr != NULL ? chrstr : "unsupported"));
	}

	/* Allocate space for buffers */
	planes = malloc(sizeof(uint8_t *[Y4M_MAX_NUM_PLANES]) * buffer_size);
	input_frame_infos = malloc(sizeof(y4m_frame_info_t) * buffer_size);
	favg = malloc(sizeof(int [Y4M_MAX_NUM_PLANES]) * buffer_size);
	if (planes == NULL || input_frame_infos == NULL
		|| favg == NULL /*|| yvcount == NULL*/) {
		fputs(PROGNAME ": error: memory allocation failed\n", stderr);
		exit(1);
	}
	for (i = 0; i < buffer_size; i++) {
		y4m_init_frame_info(input_frame_infos + i);
	}
	plane_count = y4m_si_get_plane_count(&stream_info);
	if (plane_count < 3 || plane_count > Y4M_MAX_NUM_PLANES) {
		fputs(PROGNAME ": error: unsupported chroma mode\n", stderr);
		exit(1);
	}
	for (i = 0; i < plane_count; i++) {
		int j;
		
		plane_width[i] = y4m_si_get_plane_width(&stream_info, i);
		plane_height[i] = y4m_si_get_plane_height(&stream_info, i);
		plane_length[i] = y4m_si_get_plane_length(&stream_info, i);
		if (plane_length[i] != plane_width[i] * plane_height[i]) {
			fputs(PROGNAME ": error: unsupported chroma mode\n", stderr);
			exit(1);
		}
		for (j = 0; j < buffer_size; j++) {
			(planes[j])[i] = malloc(plane_length[i]);
			if ((planes[j])[i] == NULL) {
				fputs(PROGNAME ": error: memory allocation failed\n", stderr);
				exit(1);
			}
		}
		avg_sum[i] = 0;
	}
	
	/* Write output stream header */
	if (y4m_write_stream_header(STDOUT_FILENO, &stream_info) != Y4M_OK) {
		fputs(PROGNAME ": error: could not write output stream header\n",
			stderr);
		exit(1);
	}
	
	/* Buffer some frames */
	for (i = 0; i < (buffer_size + 1) / 2; i++) {
		if (!read_frame()) {
			break;
		}
	}
	
	/* Process frame by frame and send out */
	while (buffer_pos != buffer_head) {
		adjust_frame(buffer_pos);
		if (y4m_write_frame(STDOUT_FILENO, &stream_info,
			input_frame_infos + buffer_pos,
			planes[buffer_pos]) != Y4M_OK) {
			fputs(PROGNAME ": error: could not write output stream\n",
				stderr);
			exit(1);
		}
		if (verbose & VERBOSE_DEBUG) {
			fprintf(stderr, PROGNAME ": debug: produced output frame %u\n",
				output_frame_count);
		}
		output_frame_count++;
		if (++buffer_pos >= buffer_size) {
			buffer_pos = 0;
		}
		if (!read_frame()) {
			step_buffer();
		}
	}
	
	return 0;
}

static void parse_options(int argc, char *argv[]) {
	int c;

	/* Read options */	
	while ((c = getopt(argc, argv, "b:cdhHlvwW")) != -1) {
		switch (c) {
			case 'h':
				fputs(
PROGNAME " " VERSION " - adjust luminance and white balance of a YUV4MPEG stream\n"
COPYRIGHT "\n"
"\n"
"Automatically adjust luminance level, contrast and white balance of\n"
"a YUV4MPEG stream. The input stream is read from the standard input and the\n"
"adjusted stream is written to the standard output. The stream is adjusted\n"
"according to the gray world assumption.\n"
"\n"
"This tool can be used to quickly enhance video which is too dark or which is\n"
"blueish or reddish. A typical example would be a video recorded in a dimly\n"
"lighted room. However, because the gray world assumption is not valid\n"
"for all content, the result might also be worse, especially when applied to\n"
"high quality input. It is a good idea to experiment and to compare the result\n"
"to the original (see also the -H option).\n"
"\n"
"usage: " PROGNAME " command... [option...]\n"
"commands:\n"
"  -l       adjust luminance level and contrast\n"
"  -w       adjust white balance (conflicts with -W)\n"
"  -W       adjust white balance and color contrast (conflicts with -w)\n"
"options:\n"
"  -h       print this help text and exit\n"
"  -b NUM   use information from up to NUM surrounding frames to adjust\n"
"             a frame (default is 30 frames)\n"
"  -H       adjust only the first half of each frame (for comparison)\n"
"  -c       clip output YUV values to their nominal ranges (exclude headroom)\n"
"  -v       verbose operation\n"
"  -d       enable debug output\n",
					stdout);
				exit(0);
			case 'b':
				buffer_size = atoi(optarg);
				if (buffer_size <= 0) {
					fputs(PROGNAME ": error: illegal buffer size\n", stderr);
					exit(1);
				}
				break;
			case 'c':
				clip = 1;
				break;
			case 'd':
				verbose |= VERBOSE_DEBUG;
				break;
			case 'H':
				only_half = 1;
				break;
			case 'l':
				oper |= OPER_LCONTRAST;
				break;
			case 'v':
				verbose |= 1;
				break;
			case 'w':
				oper |= OPER_WHITEBALANCE;
				break;
			case 'W':
				oper |= OPER_CCONTRAST;
				break;
			case ':':
				fputs(PROGNAME ": error: missing option parameter\n", stderr);
				exit(1);
			default:
				fputs(PROGNAME ": error: unknown option\n", stderr);
				exit(1);
		}
	}	
	
	/* Error if no-op */
	if (!oper) {
		fputs(PROGNAME ": error: no operation specified (try -h)\n", stderr);
		exit(1);
	}
	
	/* Color contrast enhancement will adjust white balance */
	if ((oper & OPER_WHITEBALANCE) && (oper & OPER_CCONTRAST)) {
		fputs(PROGNAME ": warning: color contrast enhancement will also adjust white balance\n", stderr);
		oper &= ~(OPER_WHITEBALANCE);
	}

	/* Print configuration if verbose */
	if (verbose) {
		if (oper & OPER_WHITEBALANCE) {
			fputs(PROGNAME ": conf: adjust white balance\n", stderr);
		}
		if (oper & OPER_LCONTRAST) {
			fputs(PROGNAME ": conf: adjust luminance level and contrast\n", stderr);
		}
		if (oper & OPER_CCONTRAST) {
			fputs(PROGNAME ": conf: adjust white balance and color contrast\n", stderr);
		}
		fprintf(stderr, PROGNAME ": conf: buffer size %u frames\n",
			buffer_size);
		if (only_half) {
			fputs(PROGNAME ": conf: adjust only the first half of each frame\n",
				stderr);
		}
		if (clip) {
			fputs(PROGNAME ": conf: clip output YUV values to their nominal range\n",
				stderr);
		}
	}
}

static int read_frame(void) {
	int i;
	
	if (buffer_count >= buffer_size) {
		step_buffer();
	}
	if ((i = y4m_read_frame(STDIN_FILENO, &stream_info,
		input_frame_infos + buffer_head, planes[buffer_head]))
		== Y4M_OK) {
		
		buffer_count++;
		if (verbose & VERBOSE_DEBUG) {
			fprintf(stderr, PROGNAME
				": debug: consumed input frame %u (%u frames buffered)\n",
				input_frame_count, buffer_count);
		}
		analyze_buffered_frame(buffer_head);
		for (i = 0; i < plane_count; i++) {
			avg_sum[i] += (favg[buffer_head])[i];
		}
		if (++buffer_head >= buffer_size) {
			buffer_head = 0;
		}
		input_frame_count++;
		return 1;
	} else if (i != Y4M_ERR_EOF) {
		fputs(PROGNAME ": error: could not read input stream\n", stderr);
		exit(1);
	} else {
		return 0;
	}
}

static void step_buffer(void) {
	int i;

	for (i = 0; i < plane_count; i++) {
		avg_sum[i] -= (favg[buffer_tail])[i];
	}
	if (++buffer_tail >= buffer_size) {
		buffer_tail = 0;
	}
	buffer_count--;	
}

static void analyze_buffered_frame(int i) {
	int j, k;
	uint8_t *p;

	for (j = 0; j <= 2; j++) {
		unsigned long sum = 0;
			
		if ((j == 0 && (oper & OPER_LCONTRAST))
			|| (j > 0 && (oper & (OPER_WHITEBALANCE | OPER_CCONTRAST)))) {
			p = (planes[i])[j];
			for (k = plane_length[j]; k; k--) {
				sum += *p;
				p++;
			}
			(favg[i])[j] = (sum + plane_length[j] / 2) / plane_length[j];
			if (verbose & VERBOSE_DEBUG) {
				if (oper & OPER_WHITEBALANCE) {
					fprintf(stderr, PROGNAME ": debug: input frame %u avg(%c) = %u\n",
						input_frame_count, (j == 0 ? 'y' : (j == 1 ? 'u' : 'v')), (int) (favg[i])[j]);
				}
			}
		}
	}
}

static void adjust_frame(int i) {
	int j, k;
	uint8_t *p;

	/* Contrast enhancements */
	for (j = 0; j <= 2; j++) {	
		if ((j == 0 && (oper & OPER_LCONTRAST))
			|| (j > 0 && (oper & OPER_CCONTRAST))) {
			double avg;
			double a, b;
			uint8_t table[256];
			double min;
			double max;

			if (j == 0) {
				min = MIN_Y;
				max = MAX_Y;
			} else {
				min = MIN_UV;
				max = MAX_UV;
			}
			avg = ((double) avg_sum[j] / buffer_count - min) / (max - min);
			if (avg < 0.001) {
				avg = 0.001;
			} else if (avg > 0.999) {
				avg = 0.999;
			}
			a = (0.5 - avg) / (avg * (avg - 1));
			b = 1 - a;		
			for (k = 0; k < 256; k++) {
				double kn;
				double v;

				kn = ((double) k - min) / (max - min);
				v = (a * (kn * kn) + b * kn) * (max - min) + min;
				if (clip) {
					if (v < min) {
						v = min;
					} else if (v > max) {
						v = max;
					}
				} else {
					if (v < 0) {
						v = 0;
					} else if (v > 255) {
						v = 255;
					}
				}
				table[k] = rint(v);
			}
			if (verbose & VERBOSE_DEBUG) {
				char v = (j == 0 ? 'y' : (j == 1 ? 'u' : 'v'));
				fprintf(stderr, PROGNAME ": debug: output frame %u average %c %u adjustment %c' = %.3f * %c^2 + %.3f * %c\n",
					output_frame_count, v, avg_sum[j] / buffer_count, v, a, v, b, v);
			}
			p = (planes[i])[j];
			for (k = only_half ? plane_length[j] / 2 : plane_length[j]; k; k--) {
				*p = table[*p];
				p++;
			}
		}
	}
	
	/* Plain white balance adjustment */
	if (oper & OPER_WHITEBALANCE) {
		for (j = 1; j <= 2; j++) {
			int min, max;
			int wboff;
			
			if (j == 0) {
				min = MIN_Y;
				max = MAX_Y;
			} else {
				min = MIN_UV;
				max = MAX_UV;
			}
			wboff = -((avg_sum[j] + buffer_count / 2) / buffer_count - 128);
			if (verbose & VERBOSE_DEBUG) {
				if (oper & OPER_WHITEBALANCE) {
					fprintf(stderr, PROGNAME ": debug: output frame %u white balance adjustment %c' = %c %c %u\n",
						output_frame_count, j == 1 ? 'u' : 'v', j == 1 ? 'u' : 'v', wboff < 0 ? '-' : '+', abs(wboff));
				}
			}
			p = (planes[i])[j];
			for (k = only_half ? plane_length[j] / 2 : plane_length[j]; k; k--) {
				int v = *p + wboff;
				if (clip) {
					if (v < min) {
						v = min;
					} else if (v > max) {
						v = max;
					}
				} else {
					if (v < 0) {
						v = 0;
					} else if (v > 255) {
						v = 255;
					}
				}
				*p = v;
				p++;
			}
		}
	}
}
