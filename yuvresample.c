/*------------------------------------------------------------------------
 * yuvresample, a resampler for YUV4MPEG streams
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

#define PROGNAME "yuvresample"
#define VERSION "1.0"
#define COPYRIGHT "Copyright 2005 Johannes Lehtinen"

#define SAMPLING_CLOSEST 0
#define SAMPLING_AVERAGE 1

#define VERBOSE_DEBUG 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <yuv4mpeg.h>

static y4m_ratio_t input_fps = { 0, 0 };
static y4m_ratio_t output_fps = { 0, 0 };
static int input_interlacing = Y4M_UNKNOWN;
static int output_interlacing = Y4M_UNKNOWN;
static int sampling_mode = SAMPLING_AVERAGE;
static int verbose = 0;

static y4m_stream_info_t input_si;
static y4m_stream_info_t output_si;
static y4m_frame_info_t fi;
static int plane_count;
static int plane_width[Y4M_MAX_NUM_PLANES];
static int plane_height[Y4M_MAX_NUM_PLANES];
static int plane_length[Y4M_MAX_NUM_PLANES];
static uint8_t *input_planes[2][Y4M_MAX_NUM_PLANES];
static uint8_t *output_planes[Y4M_MAX_NUM_PLANES];
static uint8_t *work_lines[2];
static int input_frame_time;
static int output_frame_time;
static double frame_time_d;
static int input_pos = 0;
static int output_pos = 0;
static int buffer_frame_count = 0;
static int buffer_frame_index[2] = { -1, -1 };
static int input_frame_count = 0;
static int output_frame_count = 0;

static void parse_options(int argc, char *argv[]);
static void parse_ratio(y4m_ratio_t *ratio, const char *str);
static int parse_interlacing(const char *str);
static const char *get_interlace_mode(int interlacing);
static int gcd(int a, int b);
static int position_input(int output_pos);
static void step_buffers(void);
static int consume_input(void);
static void produce_field(int voffset, int step, int pos);
static void produce_source_line(uint8_t *dst, int srcframe, int srcfield, int p, int y);

int main(int argc, char *argv[]) {
	y4m_ratio_t ratio;
	int i;
	
	/* Parse options */
	parse_options(argc, argv);
	
	/* Check if a no-op */
	if (output_fps.d == 0 && output_interlacing == Y4M_UNKNOWN) {
		fputs(PROGNAME ": warning: producing identical output stream (try -h)\n",
			stderr);
	}
	
	/* Open the input stream */
	y4m_allow_unknown_tags(1);
	y4m_accept_extensions(1);
	y4m_init_stream_info(&input_si);
	if (y4m_read_stream_header(STDIN_FILENO, &input_si) != Y4M_OK) {
		fputs(PROGNAME ": error: error reading YUV4MPEG stream header\n",
			stderr);
		exit(1);
	}
	
	/* Print input information if verbose */
	if (verbose) {
		const char *chrstr;
		
		fprintf(stderr, PROGNAME ": input: frame size %ux%u\n",
			y4m_si_get_width(&input_si),
			y4m_si_get_height(&input_si));
		fprintf(stderr, PROGNAME ": input: interlacing mode %s",
			get_interlace_mode(y4m_si_get_interlace(&input_si)));
		if (input_interlacing != Y4M_UNKNOWN) {
			fprintf(stderr, " overridden as %s",
				get_interlace_mode(input_interlacing));
		}
		ratio = y4m_si_get_framerate(&input_si);
		fprintf(stderr, "\n" PROGNAME ": input: frame rate %u:%u (%.3f fps)",
			ratio.n, ratio.d, (double) ratio.n / ratio.d);
		if (input_fps.d != 0) {
			fprintf(stderr, " overridden as %u:%u (%.3f fps)",
				input_fps.n, input_fps.d, (double) input_fps.n / input_fps.d);
		}
		ratio = y4m_si_get_sampleaspect(&input_si);
		fprintf(stderr, "\n" PROGNAME ": input: sample aspect ratio %u:%u\n",
			ratio.n, ratio.d);
		chrstr = y4m_chroma_description(y4m_si_get_chroma(&input_si));
		fprintf(stderr, PROGNAME ": input: chroma mode %s\n",
			(chrstr != NULL ? chrstr : "unsupported"));
	}
	
	/* Possible overrides */
	if (input_fps.d != 0) {
		y4m_si_set_framerate(&input_si, input_fps);
	} else {
		input_fps = y4m_si_get_framerate(&input_si);
	}
	if (input_interlacing != Y4M_UNKNOWN) {
		y4m_si_set_interlace(&input_si, input_interlacing);
	} else {
		input_interlacing = y4m_si_get_interlace(&input_si);
	}
	
	/* Check the stream header */
	ratio = y4m_si_get_framerate(&input_si);
	if (ratio.n <= 0 || ratio.d <= 0) {
		fputs(PROGNAME ": error: invalid input frame rate\n", stderr);
		exit(1);
	}
	i = y4m_si_get_interlace(&input_si);
	if (i != Y4M_ILACE_NONE
		&& i != Y4M_ILACE_TOP_FIRST
		&& i != Y4M_ILACE_BOTTOM_FIRST) {
		fputs(PROGNAME ": error: unsupported input interlacing mode\n", stderr);
		exit(1);
	}
	
	/* Construct the output stream header */
	y4m_init_stream_info(&output_si);
	y4m_copy_stream_info(&output_si, &input_si);
	if (output_fps.d != 0) {
		y4m_si_set_framerate(&output_si, output_fps);
	} else {
		output_fps = y4m_si_get_framerate(&output_si);
	}
	if (output_interlacing != Y4M_UNKNOWN) {
		y4m_si_set_interlace(&output_si, output_interlacing);
	} else {
		output_interlacing = y4m_si_get_interlace(&output_si);
	}
	if (y4m_write_stream_header(STDOUT_FILENO, &output_si) != Y4M_OK) {
		fputs(PROGNAME ": error: could not write output stream\n", stderr);
		exit(1);
	}
	y4m_init_frame_info(&fi);
	
	/* Initialize internal data structures */
	plane_count = y4m_si_get_plane_count(&input_si);
	if (plane_count > Y4M_MAX_NUM_PLANES) {
		fputs(PROGNAME ": error: unsupported chroma mode\n", stderr);
		exit(1);
	}
	for (i = 0; i < plane_count; i++) {
		plane_width[i] = y4m_si_get_plane_width(&input_si, i);
		plane_height[i] = y4m_si_get_plane_height(&input_si, i);
		if (output_interlacing != Y4M_ILACE_NONE
			&& plane_height[i] & 1) {
			fputs(PROGNAME
				": error: invalid height for interlaced output\n",
				stderr);
			exit(1);
		}
		plane_length[i] = y4m_si_get_plane_length(&input_si, i);
		if (plane_width[i] > y4m_si_get_width(&input_si)
			|| plane_length[i] != plane_width[i] * plane_height[i]) {
			fputs(PROGNAME ": error: unsupported chroma mode\n", stderr);
			exit(1);
		}
		input_planes[0][i] = malloc(plane_length[i]);
		input_planes[1][i] = malloc(plane_length[i]);
		output_planes[i] = malloc(plane_length[i]);
		if (input_planes[0][i] == NULL
			|| input_planes[1][i] == NULL
			|| output_planes[i] == NULL) {
			fputs(PROGNAME ": error: memory allocation failed\n", stderr);
			exit(1);
		}
	}
	work_lines[0] = malloc(y4m_si_get_width(&input_si));
	work_lines[1] = malloc(y4m_si_get_width(&input_si));
	if (work_lines[0] == NULL
		|| work_lines[1] == NULL) {
		fputs(PROGNAME ": error: memory allocation failed\n", stderr);
		exit(1);
	}
	
	/* Determine an exact (relative) frame time */
	y4m_ratio_reduce(&input_fps);
	y4m_ratio_reduce(&output_fps);
	input_frame_time = input_fps.d * output_fps.n;
	output_frame_time = output_fps.d * input_fps.n;
	i = gcd(input_frame_time, output_frame_time);
	input_frame_time /= i;
	output_frame_time /= i;
	frame_time_d = (double) (input_fps.n * output_fps.n) / i;
	if ((input_interlacing != Y4M_ILACE_NONE
			&& (input_frame_time & 1))
		|| (output_interlacing != Y4M_ILACE_NONE
			&& (output_frame_time & 1))) {
		input_frame_time *= 2;
		output_frame_time *= 2;
		frame_time_d *= 2;
	}
	if (verbose & VERBOSE_DEBUG) {
		fprintf(stderr,
			PROGNAME
			": debug: relative input frame time %u, output frame time %u\n",
			input_frame_time, output_frame_time);
		if (input_interlacing != Y4M_ILACE_NONE
			|| output_interlacing != Y4M_ILACE_NONE) {
			fprintf(stderr,
				PROGNAME
				": debug: relative input field time %u, output field time %u\n",
				(input_interlacing != Y4M_ILACE_NONE ?
				input_frame_time / 2 : input_frame_time),
				(output_interlacing != Y4M_ILACE_NONE ?
				output_frame_time / 2 : output_frame_time));
		}
	}
	
	/* Resample data */
	while (1) {
		
		/* Produce the output frame */
		if (!position_input(output_pos)) {
			break;
		}
		if (output_interlacing == Y4M_ILACE_NONE) {
			produce_field(0, 1, output_pos);
		} else {
			produce_field(output_interlacing == Y4M_ILACE_TOP_FIRST ? 0 : 1, 2,
				output_pos);
			if (!position_input(output_pos + output_frame_time / 2)) {
				break;
			}
			produce_field(output_interlacing == Y4M_ILACE_TOP_FIRST ? 1 : 0, 2,
				output_pos + output_frame_time / 2);
		}
		
		/* Write the finished output frame out */
		y4m_clear_frame_info(&fi);
		if (y4m_write_frame(STDOUT_FILENO, &output_si, &fi, output_planes) != Y4M_OK) {
			fputs(PROGNAME ": error: could not write output stream\n", stderr);
			exit(1);
		}
		if (verbose & VERBOSE_DEBUG) {
			fprintf(stderr, PROGNAME ": debug: produced output frame %u\n",
				output_frame_count);
		}
		output_frame_count++;
		output_pos += output_frame_time;
	}

	/* Print some information if verbose enabled */
	if (verbose) {
		fprintf(stderr,
			PROGNAME
			": info: produced %u output frames from %u input frames\n",
			output_frame_count, input_frame_count);
		fprintf(stderr,
			PROGNAME
			": info: input and output length difference %.4f s\n",
			(input_frame_count * input_frame_time
				- output_frame_count * output_frame_time)
				/ frame_time_d);
	}
	
	/* Finalize */
	y4m_fini_frame_info(&fi);
	y4m_fini_stream_info(&output_si);
	y4m_fini_stream_info(&input_si);
	
	/* Return */
	return 0;
}

static void parse_options(int argc, char *argv[]) {
	int c;

	/* Read options */	
	while ((c = getopt(argc, argv, "dF:f:hI:i:m:v")) != -1) {
		switch (c) {
			case 'h':
				fputs(
PROGNAME " " VERSION " - a resampler for YUV4MPEG streams\n"
COPYRIGHT "\n"
"\n"
"Resamples a YUV4MPEG stream to change the frame rate, optionally changing the\n"
"interlacing mode as well. The source stream is read from the standard input\n"
"and the result is written to the standard output.\n"
"\n"
"Each output frame/field is produced as the weighted average of the two\n"
"temporally closest input frames/fields. Optionally, the closest frame/field\n"
"can be used as the single source.\n"
"\n"
"usage: " PROGNAME " [<option>...]\n"
"options:\n"
"  -h       print this help text and exit\n"
"  -f N:D   output frame rate as a ratio (defaults to the input frame rate)\n"
"  -F N:D   input frame rate as a ratio (overrides source stream info)\n"
"  -i I     output interlacing mode (defaults to the input mode)\n"
"             p - progressive\n"
"             t - top field first\n"
"             b - bottom field first\n"
"  -I I     input interlacing mode (overrides source stream info)\n"
"  -m M     source frame selection mode (defaults to 'a')\n"
"             c - the closest input frame/field\n"
"             a - weighted average of the two closest input frames/fields\n"
"  -v       verbose operation\n"
"  -d       enable debug output\n",
					stdout);
				exit(0);
			case 'd':
				verbose |= VERBOSE_DEBUG;
				break;
			case 'f':
				parse_ratio(&output_fps, optarg);
				break;
			case 'F':
				parse_ratio(&input_fps, optarg);
				break;
			case 'i':
				output_interlacing = parse_interlacing(optarg);
				break;
			case 'I':
				input_interlacing = parse_interlacing(optarg);
				break;
			case 'm':
				sampling_mode = -1;
				if (optarg[0] != '\0' && optarg[1] == '\0') {
					switch (optarg[0]) {
						case 'c':
							sampling_mode = SAMPLING_CLOSEST;
							break;
						case 'a':
							sampling_mode = SAMPLING_AVERAGE;
							break;
					}
				}
				if (sampling_mode == -1) {
					fprintf(stderr,
						PROGNAME ": error: unknown sampling mode %s\n",
						optarg);
					exit(1);
				}
				break;
			case 'v':
				verbose |= 1;
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
		if (output_fps.d != 0) {
			fprintf(stderr, PROGNAME ": conf: output frame rate %u:%u (%.3f fps)\n",
				output_fps.n, output_fps.d,
				(double) output_fps.n / output_fps.d);
		} else {
			fputs(PROGNAME ": conf: output frame rate same as input\n",
				stderr);
		}
		if (input_fps.d != 0) {
			fprintf(stderr, PROGNAME ": conf: override input frame rate as %u:%u (%.3f fps)\n",
				input_fps.n, input_fps.d,
				(double) input_fps.n / input_fps.d);
		}
		if (output_interlacing != Y4M_UNKNOWN) {
			fprintf(stderr, PROGNAME ": conf: output interlacing mode %s\n",
				get_interlace_mode(output_interlacing));
		} else {
			fputs(PROGNAME ": conf: output interlacing mode same as input\n",
				stderr);
		}
		if (input_interlacing != Y4M_UNKNOWN) {
			fprintf(stderr, PROGNAME ": conf: override input interlacing mode as %s\n",
				get_interlace_mode(input_interlacing));
		}
		fprintf(stderr, PROGNAME ": conf: sampling mode %s\n",
			(sampling_mode == SAMPLING_CLOSEST ? "closest" : "weighted average"));
	}
}

static void parse_ratio(y4m_ratio_t *ratio, const char *str) {
	if (y4m_parse_ratio(ratio, str) != Y4M_OK
		|| ratio->d <= 0 || ratio->n <= 0) {
		fprintf(stderr, PROGNAME ": error: invalid ratio %s\n", str);
		exit(1);
	}
}

static int parse_interlacing(const char *str) {
	if (str[0] != '\0' && str[1] == '\0') {
		switch (str[0]) {
			case 'p':
				return Y4M_ILACE_NONE;
			case 't':
				return Y4M_ILACE_TOP_FIRST;
			case 'b':
				return Y4M_ILACE_BOTTOM_FIRST;
		}
	}
	fprintf(stderr, PROGNAME ": error: unknown interlacing mode %s\n", str);
	exit(1);
}

static const char *get_interlace_mode(int interlacing) {
	switch (interlacing) {
		case Y4M_ILACE_NONE:
			return "non-interlaced (progressive)";
		case Y4M_ILACE_TOP_FIRST:
			return "top-field first";
		case Y4M_ILACE_BOTTOM_FIRST:
			return "bottom-field first";
		case Y4M_ILACE_MIXED:
			return "mixed";
		default:
			return "unknown";
	}
}

static int gcd(int a, int b) {
	if (b == 0) {
		return a;
	} else {
		return gcd(b, a % b);
	}
}

static int position_input(int output_pos) {
	switch (sampling_mode) {
		case SAMPLING_CLOSEST:
			while (buffer_frame_count == 0
				|| abs(input_pos + (input_interlacing != Y4M_ILACE_NONE ? input_frame_time / 2 : 0) - output_pos)
					> abs(input_pos + input_frame_time - output_pos)) {
				assert(buffer_frame_count <= 1);
				if (buffer_frame_count == 1) {
					step_buffers();
				}
				if (!consume_input()) {
					return 0;
				}
			}
			return 1;
		case SAMPLING_AVERAGE:
			while (buffer_frame_count == 0
				|| input_pos + input_frame_time <= output_pos
				|| (buffer_frame_count < 2
					&& (input_interlacing != Y4M_ILACE_NONE ? input_pos + input_frame_time / 2 < output_pos : input_pos != output_pos))) {
				assert(buffer_frame_count <= 2);
				if (buffer_frame_count == 2) {
					step_buffers();
				} else if (!consume_input()) {
					return 0;
				}
			}
			return 1;
		default:
			assert(0);
			exit(1);
	}
}

static void step_buffers(void) {
	assert(buffer_frame_count > 0 && buffer_frame_count <= 2);
	buffer_frame_count--;
	if (buffer_frame_count == 1) {
		buffer_frame_index[0] = buffer_frame_index[1];
	}
	buffer_frame_index[buffer_frame_count] = -1;
	input_pos += input_frame_time;
}

static int consume_input(void) {
	int i;
	assert(buffer_frame_count <= 2);
	if (buffer_frame_count == 2) {
		step_buffers();
	}
	if (buffer_frame_count > 0) {
		buffer_frame_index[1] = 1 - buffer_frame_index[0];
	} else {
		buffer_frame_index[0] = 0;
	}
	if ((i = y4m_read_frame(STDIN_FILENO, &input_si, &fi, input_planes[buffer_frame_index[buffer_frame_count]])) == Y4M_OK) {
		buffer_frame_count++;
		if (verbose & VERBOSE_DEBUG) {
			fprintf(stderr, PROGNAME ": debug: consumed input frame %u (%u frames buffered)\n",
				input_frame_count, buffer_frame_count);
		}
		input_frame_count++;
		return 1;
	} else if (i == Y4M_ERR_EOF) {
		return 0;
	} else {
		fputs(PROGNAME ": error: could not read input stream\n", stderr);
		exit(1);
	}
}

static void produce_field(int voffset, int step, int pos) {
	int p;
	int srcframe[2] = { -1, -1 };
	int srcfield[2] = { -1, -1 };
	int srctime[2] = { 0, 0 };
	int timediff = 0;
	int w[2] = { 0, 0 };
	
	switch (sampling_mode) {
		case SAMPLING_CLOSEST:
			srcframe[0] = 0;
			if (input_interlacing != Y4M_ILACE_NONE) {
				srcfield[0] = abs(input_pos - pos) <= abs(input_pos + input_frame_time / 2 - pos)
					? (input_interlacing == Y4M_ILACE_TOP_FIRST ? 0 : 1)
					: (input_interlacing == Y4M_ILACE_TOP_FIRST ? 1 : 0);
			}
			break;
		case SAMPLING_AVERAGE:
			srcframe[0] = 0;
			if (input_interlacing == Y4M_ILACE_NONE) {
				srctime[0] = input_pos;
				if (pos != srctime[0]) {
					srcframe[1] = 1;
					srctime[1] = input_pos + input_frame_time;
				}
			} else {
				if (input_pos + input_frame_time / 2 > pos) {
					srcfield[0] = 0;
					srctime[0] = input_pos;
					if (pos != srctime[0]) {
						srcframe[1] = 0;
						srcfield[1] = 1;
						srctime[1] = input_pos + input_frame_time / 2;
					}
				} else {
					srcfield[0] = 1;
					srctime[0] = input_pos + input_frame_time / 2;
					if (pos != srctime[0]) {
						srcframe[1] = 1;
						srcfield[1] = 0;
						srctime[1] = input_pos + input_frame_time;
					}
				}
			}
			if (srcframe[1] != -1) {
				timediff = srctime[1] - srctime[0];
				w[0] = timediff - (pos - srctime[0]);
				w[1] = timediff - (srctime[1] - pos);
			}
			break;
	}	
	for (p = 0; p < plane_count; p++) {
		int y;
		for (y = voffset; y < plane_height[p]; y += step) {
			switch (sampling_mode) {
				case SAMPLING_CLOSEST:
					produce_source_line(output_planes[p] + y * plane_width[p], srcframe[0], srcfield[0], p, y);
					break;
				case SAMPLING_AVERAGE:
					if (srcframe[1] == -1) {
						produce_source_line(output_planes[p] + y * plane_width[p], srcframe[0], srcfield[0], p, y);
					} else {
						int x;
						
						produce_source_line(work_lines[0], srcframe[0], srcfield[0], p, y);
						produce_source_line(work_lines[1], srcframe[1], srcfield[1], p, y);						
						for (x = 0; x < plane_width[p]; x++) {
							*(output_planes[p] + y * plane_width[p] + x)
								= (uint8_t) ((w[0] * work_lines[0][x] + w[1] * work_lines[1][x]) / timediff);
						}
					}
					break;
			}
		}
	}
}

static void produce_source_line(uint8_t *dst, int srcframe, int srcfield, int p, int y) {
	if (input_interlacing == Y4M_ILACE_NONE
		|| (srcfield ? (y & 1) : !(y & 1))) {
		memcpy(dst, input_planes[buffer_frame_index[srcframe]][p] + y * plane_width[p], plane_width[p]);
	} else {
		if (y == 0) {
			memcpy(dst, input_planes[buffer_frame_index[srcframe]][p] + plane_width[p], plane_width[p]);
		} else if (y == plane_height[p] - 1) {
			memcpy(dst, input_planes[buffer_frame_index[srcframe]][p] + (y - 1) * plane_width[p], plane_width[p]);
		} else {
			uint8_t *l = input_planes[buffer_frame_index[srcframe]][p] + (y - 1) * plane_width[p];
			int x;
			for (x = 0; x < plane_width[p]; x++) {
				dst[x] = (uint8_t) (((int) l[x] + (l + 2 * plane_width[p])[x]) / 2);
			}
		}
	}
}
