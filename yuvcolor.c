/*------------------------------------------------------------------------
 * yuvwhitebalance, adjust white balance of YUV4MPEG streams
 * Copyright 2005 Johannes Lehtinen
 *----------------------------------------------------------------------*/

#define PROGNAME "yuvcolor"
#define VERSION "0.1"
#define COPYRIGHT "Copyright 2005 Johannes Lehtinen"

#define DEFAULT_BUFFER_SIZE 30
#define MAX_PLANE_COUNT 5

#define VERBOSE_DEBUG 2

#define OPER_WHITEBALANCE 1
#define OPER_LCONTRAST 2
#define OPER_CCONTRAST 4

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <yuv4mpeg.h>

int oper = 0;
int only_half = 0;
y4m_stream_info_t stream_info;
int plane_count;
int plane_width[MAX_PLANE_COUNT];
int plane_height[MAX_PLANE_COUNT];
int plane_length[MAX_PLANE_COUNT];
int x_shift[2];
int y_shift[2];
uint8_t *(*input_planes)[MAX_PLANE_COUNT];
y4m_frame_info_t *input_frame_infos;
int (*favg)[MAX_PLANE_COUNT];
int avg_sum[MAX_PLANE_COUNT];
int (*fmin)[MAX_PLANE_COUNT];
int (*fmax)[MAX_PLANE_COUNT];
int min[MAX_PLANE_COUNT];
int max[MAX_PLANE_COUNT];
uint8_t *output_planes[MAX_PLANE_COUNT];
int buffer_count = 0;
int buffer_head = 0;
int buffer_tail = 0;
int buffer_size = DEFAULT_BUFFER_SIZE;
int buffer_pos = 0;
int verbose = 0;
int input_frame_count = 0;
int output_frame_count = 0;

static void parse_options(int argc, char *argv[]);
static int read_frame(void);
static void step_buffer(void);
static void analyze_buffered_frame(int i);
static void adjust_frame(int i);
static inline int uv_limit(int v);

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
	input_planes = malloc(sizeof(uint8_t *[MAX_PLANE_COUNT]) * buffer_size);
	input_frame_infos = malloc(sizeof(y4m_frame_info_t) * buffer_size);
	favg = malloc(sizeof(int [MAX_PLANE_COUNT]) * buffer_size);
	fmin = malloc(sizeof(int [MAX_PLANE_COUNT]) * buffer_size);
	fmax = malloc(sizeof(int [MAX_PLANE_COUNT]) * buffer_size);
	if (input_planes == NULL || input_frame_infos == NULL
		|| favg == NULL || fmin == NULL || fmax == NULL) {
		fputs(PROGNAME ": error: memory allocation failed\n", stderr);
		exit(1);
	}
	for (i = 0; i < buffer_size; i++) {
		y4m_init_frame_info(input_frame_infos + i);
	}
	plane_count = y4m_si_get_plane_count(&stream_info);
	if (plane_count < 3 || plane_count > MAX_PLANE_COUNT) {
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
		if (i >= 1 && i <= 2) {
			switch (plane_width[0] / plane_width[i]) {
				case 1:
					x_shift[i-1] = 0;
					break;
				case 2:
					x_shift[i-1] = 1;
					break;
				default:
					fputs(PROGNAME ": error: unsupported chroma mode\n", stderr);
					exit(1);
			}
			switch (plane_height[0] / plane_height[i]) {
				case 1:
					y_shift[i-1] = 0;
					break;
				case 2:
					y_shift[i-1] = 1;
					break;
				default:
					fputs(PROGNAME ": error: unsupported chroma mode\n", stderr);
					exit(1);
			}
		}
		for (j = 0; j < buffer_size; j++) {
			(input_planes[j])[i] = malloc(plane_length[i]);
			if ((input_planes[j])[i] == NULL) {
				fputs(PROGNAME ": error: memory allocation failed\n", stderr);
				exit(1);
			}
		}
		avg_sum[i] = 0;
		min[i] = 255;
		max[i] = 0;
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
			input_planes[buffer_pos]) != Y4M_OK) {
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
	while ((c = getopt(argc, argv, "b:CcdhHvw")) != -1) {
		switch (c) {
			case 'h':
				fputs(
PROGNAME " " VERSION " - adjust white balance of YUV4MPEG streams\n"
COPYRIGHT "\n"
"\n"
"Adjust the while balance and/or contrast of a YUV4MPEG stream. The source\n"
"stream is read from the standard input and the result is written to the\n"
"standard output.\n"
"\n"
"usage: " PROGNAME " <command>... [<option>...]\n"
"commands:\n"
"  -w       adjust white balance\n"
"  -c       enhance contrast by maximizing luminance scale\n"
"  -C       enhance color contrast by maximizing chrominance scale\n"
"options:\n"
"  -h       print this help text and exit\n"
"  -b NUM   use information from up to NUM surrounding frames to adjust\n"
"             the white balance of a frame (default is 30 frames)\n"
"  -H       adjust only the first half of each frame (for comparison)\n"
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
			case 'C':
				oper |= OPER_CCONTRAST;
				break;
			case 'c':
				oper |= OPER_LCONTRAST;
				break;
			case 'd':
				verbose |= VERBOSE_DEBUG;
				break;
			case 'H':
				only_half = 1;
				break;
			case 'v':
				verbose |= 1;
				break;
			case 'w':
				oper |= OPER_WHITEBALANCE;
				break;
			case ':':
				fputs(PROGNAME ": error: missing option parameter\n", stderr);
				exit(1);
			default:
				fputs(PROGNAME ": error: unknown option\n", stderr);
				exit(1);
		}
	}
	
	/* Print configuration if verbose */
	if (verbose) {
		if (oper & OPER_WHITEBALANCE) {
			fputs(PROGNAME ": conf: adjust white balance\n", stderr);
		}
		if (oper & OPER_LCONTRAST) {
			fputs(PROGNAME ": conf: enhance luminance contrast\n", stderr);
		}
		if (oper & OPER_CCONTRAST) {
			fputs(PROGNAME ": conf: enhance color contrast\n", stderr);
		}
		fprintf(stderr, PROGNAME ": conf: buffer size %u frames\n",
			buffer_size);
		if (only_half) {
			fputs(PROGNAME ": conf: adjust only the first half of each frame\n",
				stderr);
		}
	}
	
	/* Error if no-op */
	if (!oper) {
		fputs(PROGNAME ": error: no operation specified (try -h)\n", stderr);
		exit(1);
	}
}

static int read_frame(void) {
	int i;
	
	if (buffer_count >= buffer_size) {
		step_buffer();
	}
	if ((i = y4m_read_frame(STDIN_FILENO, &stream_info,
		input_frame_infos + buffer_head, input_planes[buffer_head]))
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
		for (i = 0; i < plane_count; i++) {
			if ((fmin[buffer_head])[i] < min[i]) {
				min[i] = (fmin[buffer_head])[i];
				if ((verbose & VERBOSE_DEBUG) && i <= 2) {
					fprintf(stderr, PROGNAME
						": debug: new min(%c) = %u\n",
						(i == 0 ? 'y' : (i == 1 ? 'u' : 'v')),
						min[i]);
				}
			}
			if ((fmax[buffer_head])[i] > max[i]) {
				max[i] = (fmax[buffer_head])[i];
				if ((verbose & VERBOSE_DEBUG) && i <= 2) {
					fprintf(stderr, PROGNAME
						": debug: new max(%c) = %u\n",
						(i == 0 ? 'y' : (i == 1 ? 'u' : 'v')),
						max[i]);
				}
			}
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
	int i, j, k;
	
	for (i = 0; i < plane_count; i++) {
		avg_sum[i] -= (favg[buffer_tail])[i];
		if (min[i] >= (fmin[buffer_tail])[i]) {
			int oldmin = min[i];
			
			min[i] = 255;
			j = buffer_tail;
			for (k = buffer_count - 1; k; k--) {
				if (++j >= buffer_size) {
					j = 0;
				}
				if ((fmin[j])[i] < min[i]) {
					min[i] = (fmin[j])[i];
				}
			}
			if ((verbose & VERBOSE_DEBUG) && i <= 2 && oldmin < min[i]) {
				fprintf(stderr, PROGNAME
					": debug: new min(%c) = %u\n",
					(i == 0 ? 'y' : (i == 1 ? 'u' : 'v')),
					min[i]);
			}
		}
		if (max[i] <= (fmin[buffer_tail])[i]) {
			int oldmax = max[i];
			
			max[i] = 0;
			j = buffer_tail;
			for (k = buffer_count - 1; k; k--) {
				if (++j >= buffer_size) {
					j = 0;
				}
				if ((fmax[j])[i] > max[i]) {
					max[i] = (fmax[j])[i];
				}
			}			
			if ((verbose & VERBOSE_DEBUG) && i <= 2 && oldmax > max[i]) {
				fprintf(stderr, PROGNAME
					": debug: new max(%c) = %u\n",
					(i == 0 ? 'y' : (i == 1 ? 'u' : 'v')),
					max[i]);
			}
		}
	}
	if (++buffer_tail >= buffer_size) {
		buffer_tail = 0;
	}
	buffer_count--;	
}

static void analyze_buffered_frame(int i) {
	unsigned long sum[MAX_PLANE_COUNT];
	int j, k;
	uint8_t *p;

	for (j = 0; j < plane_count; j++) {
		sum[j] = 0;
		(fmin[i])[j] = 255;
		(fmax[i])[j] = 0;
	}
	if (oper & OPER_LCONTRAST) {
		p = (input_planes[i])[0];
		for (k = plane_length[0]; k; k--) {
			if (*p < (fmin[i])[0]) {
				(fmin[i])[0] = *p;
			}
			if (*p > (fmax[i])[0]) {
				(fmax[i])[0] = *p;
			}
			p++;
		}
		if (verbose & VERBOSE_DEBUG) {
			fprintf(stderr, PROGNAME ": debug: input frame %u y range [%u ... %u]\n",
				input_frame_count, (int) (fmin[i])[0], (int) (fmax[i])[0]);
		}
	}	
	if (oper & (OPER_WHITEBALANCE | OPER_CCONTRAST)) {
		for (j = 1; j <= 2; j++) {
			p = (input_planes[i])[j];
			for (k = plane_length[j]; k; k--) {
				if (oper & OPER_WHITEBALANCE) {
					sum[j] += *p;
				}
				if (oper & OPER_CCONTRAST) {
					if (*p < (fmin[i])[j]) {
						(fmin[i])[j] = *p;
					}
					if (*p > (fmax[i])[j]) {
						(fmax[i])[j] = *p;
					}
				}
				p++;
			}
			(favg[i])[j] = (sum[j] + plane_length[j] / 2) / plane_length[j];
			if (verbose & VERBOSE_DEBUG) {
				if (oper & OPER_CCONTRAST) {
					fprintf(stderr, PROGNAME ": debug: input frame %u %c range [%u ... %u]\n",
						input_frame_count, j == 1 ? 'u' : 'v', (int) (fmin[i])[j], (fmax[i])[j]);
				}
				if (oper & OPER_WHITEBALANCE) {
					fprintf(stderr, PROGNAME ": debug: input frame %u avg(%c) = %u\n",
						input_frame_count, j == 1 ? 'u' : 'v', (int) (favg[i])[j]);
				}
			}
		}
	}
}

static void adjust_frame(int i) {
	int j, k;
	uint8_t *p;
	int wboff[3];
	
	if (oper & OPER_LCONTRAST) {
		int off = -min[0];
		int div = max[0] - min[0];
		
		if (verbose & VERBOSE_DEBUG) {
			fprintf(stderr, PROGNAME ": debug: output frame %u luminance adjustment y' = (y %c %u) * 255 / %u\n",
				output_frame_count, off < 0 ? '-' : '+', abs(off), div);
		}
		p = (input_planes[i])[0];
		for (k = only_half ? plane_length[0] / 2 : plane_length[0]; k; k--) {
			*p = (uint8_t) (((int) *p + off) * 255 / div);
			p++;
		}
	}
	for (j = 1; j <= 2; j++) {
		if (oper & OPER_WHITEBALANCE) {
			wboff[j] = -((avg_sum[j] + buffer_count / 2) / buffer_count - 128);
		} else {
			wboff[j] = 0;
		}
	}
	if (oper & (OPER_WHITEBALANCE | OPER_CCONTRAST)) {
		for (j = 1; j <= 2; j++) {
			int off = -uv_limit(min[j] + wboff[j]);
			int div = uv_limit(max[j] + wboff[j])
				- uv_limit(min[j] + wboff[j]);
				
			if (verbose & VERBOSE_DEBUG) {
				if (oper & OPER_WHITEBALANCE) {
					fprintf(stderr, PROGNAME ": debug: output frame %u white balance adjustment %c' = %c %c %u\n",
						output_frame_count, j == 1 ? 'u' : 'v', j == 1 ? 'u' : 'v', wboff[j] < 0 ? '-' : '+', abs(wboff[j]));
				}
				if (oper & OPER_CCONTRAST) {
					fprintf(stderr, PROGNAME ": debug: output frame %u chrominance adjustment %c' = (%c %c %u) * 224 / %u + 16\n",
						output_frame_count, j == 1 ? 'u' : 'v', j == 1 ? 'u' : 'v', off < 0 ? '-' : '+', abs(off), div);
				}
			}
			p = (input_planes[i])[j];
			for (k = only_half ? plane_length[j] / 2 : plane_length[j]; k; k--) {
				
				if (oper & OPER_CCONTRAST) {
					*p = (uv_limit(*p + wboff[j]) + off) * 224 / div + 16;
				} else {
					*p = uv_limit(*p + wboff[j]);
				}
				p++;
			}
		}
	}
}

static inline int uv_limit(int v) {
	if (v <= 16) {
		return 16;
	} else if (v >= 240) {
		return 240;
	} else {
		return v;
	}
}
