/*------------------------------------------------------------------------
 * yuvcut, cuts a slice of a YUV4MPEG stream
 * Copyright 2006 Johannes Lehtinen <johannes.lehtinen@iki.fi>
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

#define PROGNAME "yuvcut"
#define VERSION "1.0"
#define COPYRIGHT "Copyright 2006 Johannes Lehtinen"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <stdarg.h>
#include <yuv4mpeg.h>
#include <mjpeg_logging.h>

/* -----------------------------------------------------------------------
 * Internal data structures
 * ---------------------------------------------------------------------*/

/** Specifies a range of frames */
typedef struct range_spec_t range_spec_t;
struct range_spec_t {

	/** The start time code in seconds, or 0 if plain index */
	int start_sec;
	
	/** The end time code in seconds, or 0 if plain index */
	int end_sec;
	
	/** The start index (relative to start time) */
	int start_idx;
	
	/** The end index (relative to end time or -1 for the end of stream) */
	int end_idx;

	/** Whether the start is relative to the end of previous range */
	int start_relative;

	/** Whether the end is relative to the start location */
	int end_relative;

	/** The next range, or NULL if last */
	range_spec_t *next_range_spec;

};

/** Absolute frame index range */
typedef struct abs_range_t abs_range_t;
struct abs_range_t {
	
	/** The start frame index */
	int start_idx;
	
	/** The end frame index, or -1 for end of stream */
	int end_idx;
	
	/** The next range, or NULL if last */
	abs_range_t *next_abs_range;
	
};

/* -----------------------------------------------------------------------
 * Internal function declarations
 * ---------------------------------------------------------------------*/

static void *checked_malloc(size_t size);
static void parse_arguments(int argc, char *argv[], range_spec_t **ranges);
static range_spec_t *parse_range_spec(char *str);
static void parse_location(char *str, int *sec, int *idx);
static abs_range_t *range_specs_to_abs_ranges(range_spec_t *range_specs, y4m_ratio_t fps);

/* -----------------------------------------------------------------------
 * Function definitions
 * ---------------------------------------------------------------------*/

/**
 * The main routine.
 * 
 * @param argc the number of arguments
 * @param argv the arguments
 * @return the exit value
 */
int main(int argc, char *argv[]) {
	range_spec_t *range_specs = NULL;
	abs_range_t *abs_ranges;
	y4m_stream_info_t si;
	y4m_frame_info_t fi;
	uint8_t *planes[Y4M_MAX_NUM_PLANES];
	int use_lseek;
	int num_planes, frame_length;
	int in_pos, out_pos;
	int i;
	
	/* Parse arguments */
	parse_arguments(argc, argv, &range_specs);
	
	/* Read the stream header */
	y4m_allow_unknown_tags(1);
	y4m_accept_extensions(1);
	y4m_init_stream_info(&si);
	if (y4m_read_stream_header(STDIN_FILENO, &si) != Y4M_OK) {
		mjpeg_error_exit1("error reading stream header");
	}
	num_planes = y4m_si_get_plane_count(&si);
	frame_length = y4m_si_get_framelength(&si);
	
	/* Convert range specifications to absolute ranges */
	abs_ranges =
		range_specs_to_abs_ranges(range_specs, y4m_si_get_framerate(&si));
	range_specs = NULL;

	/* Allocate memory for the planes */
	for (i = 0; i < num_planes; i++) {
		planes[i] = checked_malloc(y4m_si_get_plane_length(&si, i));
	}

	/* Copy the header */
	if (y4m_write_stream_header(STDOUT_FILENO, &si) != Y4M_OK) {
		mjpeg_error_exit1("error writing stream header");
	}
	
	/* Check if lseek can be used on the input stream */
	if (lseek(STDIN_FILENO, 0, SEEK_CUR) == -1) {
		use_lseek = 0;
	} else {
		use_lseek = 1;
	}
	
	/* Copy the specified frames */
	y4m_init_frame_info(&fi);
	in_pos = 0;
	out_pos = 0;
	while (abs_ranges != NULL) {
		abs_range_t *range = abs_ranges;
		
		/* Skip and copy frames */
		while (in_pos <= range->end_idx || range->end_idx == -1) {
			
			/* Read frame header */
			i = y4m_read_frame_header(STDIN_FILENO, &si, &fi);
			if (i == Y4M_ERR_EOF) {
				if (in_pos <= range->start_idx || range->end_idx != -1) {
					mjpeg_error_exit1("unexpected end of stream at input frame %d", in_pos);
				} else {
					break;
				}
			} else if (i != Y4M_OK) {
				mjpeg_error_exit1("error reading frame header at input frame %d", in_pos);
			}
			
			/* Seek over the frame if possible, otherwise read the frame */
			if (in_pos < range->start_idx && use_lseek) {
				if (lseek(STDIN_FILENO, frame_length, SEEK_CUR) == -1) {
					mjpeg_error_exit1("failed to seek over input frame %d", in_pos);
				}
			} else {
				if (y4m_read_frame_data(STDIN_FILENO, &si, &fi, planes)
					!= Y4M_OK) {
					mjpeg_error_exit1("failed to read input frame %d", in_pos);
				}
			}
			
			/* Copy the frame if it is part of the range */
			if (in_pos >= range->start_idx) {
				if (y4m_write_frame(STDOUT_FILENO, &si, &fi, planes)
					!= Y4M_OK) {
					mjpeg_error_exit1("failed to write output frame %d", out_pos);
				}
				mjpeg_info("wrote input frame %d as output frame %d", in_pos, out_pos);
				out_pos++;
			}
			
			/* Move to next frame */
			y4m_clear_frame_info(&fi);
			in_pos++;
		}

		/* Move to next range */
		abs_ranges = range->next_abs_range;
		free(range);
	}
	
	/* Close input and output streams */	
	if (close(STDOUT_FILENO) == -1) {
		mjpeg_error_exit1("error closing output stream");
	}
	close(STDIN_FILENO);

	/* Finalize data structures */
	y4m_fini_frame_info(&fi);
	y4m_fini_stream_info(&si);

	return 0;
}

/**
 * Allocates memory and checks that the allocation was succesful. Never
 * returns NULL pointers.
 * 
 * @param size number of bytes to be allocated
 * @return pointer to the newly allocated memory
 */
static void *checked_malloc(size_t size) {
	void *p;
	
	if ((p = malloc(size)) == NULL) {
		mjpeg_error_exit1("memory allocation failed (%lu bytes)\n",
			(unsigned long) size);
		exit(1); /* to avoid compiler warning */
	} else {
		return p;
	}
}

/**
 * Parses command line arguments into configuration structure.
 * 
 * @param argc the number of arguments
 * @param argv the arguments
 * @param ranges pointer to the ranges list
 */
static void parse_arguments(int argc, char *argv[], range_spec_t **ranges) {
	int configured = 0;
	int i;
	char *cp;
	range_spec_t *range;
	int verbosity = LOG_WARN;
	
	assert(*ranges == NULL);
	while ((i = getopt(argc, argv, "c:hv")) != -1) {
		switch (i) {
			case 'c':
				while ((cp = strrchr(optarg, ',')) != NULL) {
					*cp = '\0';
					range = parse_range_spec(cp + 1);
					range->next_range_spec = *ranges;
					*ranges = range;
				}
				range = parse_range_spec(optarg);
				range->next_range_spec = *ranges;
				*ranges = range;
				configured = 1;
				break;
			case 'h':
				fputs(
PROGNAME " " VERSION " - cuts a slice of a YUV4MPEG stream\n"
COPYRIGHT "\n"
"\n"
"Reads a YUV4MPEG stream from the standard input and outputs the selected\n"
"ranges of frames to the standard output.  A range can be specified using\n"
"frame indexes (starting at 0) or using time code format [H:]MIN:SEC[.F]\n"
"(optional hours, minutes, seconds and optional frame index).  If start/end\n"
"location starts with '+' it is interpreted relative to the previously\n"
"specified location.  If the start/end location is omitted then start/end\n"
"of stream is assumed.  The ranges must not be overlapping and they must\n"
"be specified in order.\n"
"\n"
"usage: " PROGNAME " [-h] [-c [START]-[[+]END][,[+]START-[[+]END]]...] [-v]\n"
"options:\n"
"  -h      print this help text and exit\n"
"  -c [START]-[[+]END][,[+]START-[[+]END]]...\n"
"          the ranges of frames to be copied\n"
"  -v      verbose operation (twice for debug)\n",
					stdout);
				exit(0);
			case 'v':
				if (verbosity == LOG_WARN) {
					verbosity = LOG_INFO;
				} else {
					verbosity = LOG_DEBUG;
				}
				break;
			default:
				mjpeg_error_exit1("invalid argument (try -h for help)");
		}
	}
	mjpeg_default_handler_verbosity(verbosity);
	if (!configured) {
		mjpeg_error_exit1("range not configured (try -h for help)");
	}
}

/**
 * Parses a range specification.
 * 
 * @param str the specification string
 * @return the range specification
 */
static range_spec_t *parse_range_spec(char *str) {
	char *str_end;
	range_spec_t *range;

	/* Allocate memory for the range specification */
	range = checked_malloc(sizeof(range_spec_t));
	range->next_range_spec = NULL;
	
	/* Split start/end location */
	if ((str_end = strchr(str, '-')) == NULL) {
		mjpeg_error_exit1("invalid range specification \"%s\"", str);
	}
	*(str_end++) = '\0';
	
	/* Check if relative start/end location */
	if (*str == '+') {
		range->start_relative = 1;
		str++;
	} else {
		range->start_relative = 0;
	}
	if (*str_end == '+') {
		range->end_relative = 1;
		str_end++;
	} else {
		range->end_relative = 0;
	}
	
	/* Parse start/end location specifications */
	if (*str != '\0') {
		parse_location(str, &(range->start_sec), &(range->start_idx));
	} else {
		range->start_sec = 0;
		range->start_idx = 0;
	}
	if (*str_end != '\0') {
		parse_location(str_end, &(range->end_sec), &(range->end_idx));
	} else {
		range->end_sec = 0;
		range->end_idx = -1;
	}
	
	/* Return the range specification */
	return range;
}

/**
 * Parses a location specification (either frame index or time code).
 * 
 * @param str the specification string
 * @param sec where to store the seconds
 * @param idx where to store the frame index
 */
static void parse_location(char *str, int *sec, int *idx) {
	char *str_sec;	
	
	/* Check if this is a time code or plain index */
	if ((str_sec = strrchr(str, ':')) != NULL) {
		char *str_hour;
		char *str_min;
		char *str_idx;
		int i;
		
		/* Split the time code string */
		*str_sec++ = '\0';
		str_idx = strrchr(str_sec, '.');
		if (str_idx != NULL) {
			*(str_idx++) = '\0';
		}
		str_min = strrchr(str, ':');
		if (str_min != NULL) {
			*(str_min++) = '\0';
			str_hour = str;
		} else {
			str_min = str;
			str_hour = NULL;
		}
		
		/* Determine the time code as seconds and frame index */
		*sec = 0;
		if (str_hour != NULL) {
			if (sscanf(str_hour, "%d", &i) != 1) {
				mjpeg_error_exit1("invalid hour specification \"%s\"", str_hour);
			} else {
				*sec += i * 3600;
			}
		}
		if (sscanf(str_min, "%d", &i) != 1) {
			mjpeg_error_exit1("invalid minute specification \"%s\"", str_min);
		} else {
			*sec += i* 60;
		}
		if (sscanf(str_sec, "%d", &i) != 1) {
			mjpeg_error_exit1("invalid second specification \"%s\"", str_sec);
		} else {
			*sec += i;
		}
		if (str_idx != NULL && sscanf(str_idx, "%d", idx) != 1) {
			mjpeg_error_exit1("invalid frame index \"%s\"", str_idx);
		}
		
		
	} else {
		*sec = 0;
		if (sscanf(str, "%d", idx) != 1) {
			mjpeg_error_exit1("invalid frame index \"%s\"", str);
		}
	}
}

/**
 * Converts range specifications to absolute frame index ranges. Releases the
 * memory allocated for the specifications.
 * 
 * @param range_specs the range specifications
 * @param fps the frame rate ratio
 * @return the absolute frame index ranges
 */
static abs_range_t *range_specs_to_abs_ranges(range_spec_t *range_specs, y4m_ratio_t fps) {
	int pos = 0;
	abs_range_t *ranges = NULL;
	
	/* Convert all range specifications */
	while (range_specs != NULL) {
		range_spec_t *rs = range_specs;
		abs_range_t *range;
		
		/* Allocate memory for the absolute frame index range */
		range = checked_malloc(sizeof(abs_range_t));
		
		/* Check if end of stream encountered anyway */
		if (pos == -1) {
			mjpeg_error_exit1("a range specification after a range specification which is already supposed to consume rest of the stream");
		}
		
		/* Determine absolute range */
		range->start_idx = (rs->start_sec * fps.n + fps.d - 1) / fps.d
			+ rs->start_idx;
		if (rs->start_relative) {
			range->start_idx += pos;
		}
		range->end_idx = (rs->end_sec * fps.n + fps.d - 1) / fps.d
			+ rs->end_idx;
		if (rs->end_relative && rs->end_idx != -1) {
			range->end_idx += range->start_idx;
		}
		mjpeg_info("range: frames %6d - %6d",
			range->start_idx, range->end_idx);
		
		/* Check the range */
		if (range->end_idx < range->start_idx && range->end_idx != -1) {
			mjpeg_error_exit1("a range has a negative size");
		}
		if (range->start_idx < pos) {
			mjpeg_error_exit1("a range starts before the previous range ends");
		}

		/* Free the processed specification and move to next specification */		
		range_specs = rs -> next_range_spec;
		free(rs);
		range->next_abs_range = ranges;
		ranges = range;
	}
	return ranges;
}
