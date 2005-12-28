/*------------------------------------------------------------------------
 * yuvinfo, describe YUV4MPEG streams
 * Copyright 2005 Johannes Lehtinen
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
#include <sys/types.h>
#include <unistd.h>
#include <yuv4mpeg.h>

y4m_stream_info_t stream_info;
int plane_count;
int plane_length[Y4M_MAX_NUM_PLANES];
int plane_width[Y4M_MAX_NUM_PLANES];
int plane_height[Y4M_MAX_NUM_PLANES];
uint8_t *planes[Y4M_MAX_NUM_PLANES];

static void overlay_profile(void);

int main(int argc, char *argv[]) {
	int display = DISPLAY_ALL;
	int piping = 0;
	int show_profile = 0;
	y4m_frame_info_t frame_info;
	int frame_length;
	int use_lseek;
	int length;
	int i;
	
	/* Read options */
	while ((i = getopt(argc, argv, "hlpP")) != -1) {
		switch (i) {
			case 'h':
				fputs(
PROGNAME " " VERSION " - a resampler for YUV4MPEG streams\n"
COPYRIGHT "\n"
"\n"
"Describes a YUV4MPEG stream read from the standard input.\n"
"\n"
"usage: " PROGNAME " [<option>...]\n"
"options:\n"
"  -h     print this help text and exit\n"
"  -l     display only the length of the stream in frames\n"
"  -p     pipe the input to stdout and write information to stderr\n"
"  -P     overlay luminance and color profile (implies -p)\n",
					stdout);
				exit(0);
			case 'l':
				display = DISPLAY_LENGTH;
				break;
			case 'p':
				piping = 1;
				break;
			case 'P':
				show_profile = 1;
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
		if (show_profile
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
	if (show_profile
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
			if (show_profile) {
				overlay_profile();
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

static void overlay_profile(void) {
	int i;
	
	/* Calculate and overlay value profiles for each plane */
	for (i = 0; i < plane_count; i++) {
		unsigned long vf[256];
		unsigned long max;
		double avg, var;
		double a;
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
		
		/* Calculate maximum frequency for scaling */
		max = 0;
		for (j = 0; j < 256; j++) {
			if (vf[j] > max) {
				max = vf[j];
			}
		}
		
		/* Draw profile background */
		y = plane_height[0] - 64 * (plane_count - i) + 4;
		x = (plane_width[0] - 256) / 2;
		p = planes[0] + y * plane_width[0] + x;
		for (y = 60; y != 0; y--) {
			for (j = 256; j != 0; j--) {
				*p = (*p + 3 * MIN_Y) >> 2;
				p++;
			}
			p += plane_width[0] - 256;
		}
		
		/* Draw profile */
		for (j = 0; j < 256; j++) {
			int h;
			
			x = (plane_width[0] - 256) / 2 + j;
			h = 60 * vf[j] / max;
			y = plane_height[0] - 64 * (plane_count - i - 1) - h;
			for (; h > 0; h--, y++) {
				p = planes[0] + y * plane_width[0] + x;
				*p = MAX_Y;
			}
		}
	}
}
