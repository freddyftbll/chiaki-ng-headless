// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki-cli.h>

#include <chiaki/session.h>
#include <chiaki/controller.h>
#include <chiaki/base64.h>
#include <chiaki/thread.h>
#include <chiaki/time.h>

#include <chiaki/config.h>

#if (defined(CHIAKI_LIB_ENABLE_FFMPEG_DECODER) && CHIAKI_LIB_ENABLE_FFMPEG_DECODER) || (defined(CHIAKI_ENABLE_FFMPEG_DECODER) && CHIAKI_ENABLE_FFMPEG_DECODER)
#undef CHIAKI_ENABLE_FFMPEG_DECODER
#define CHIAKI_ENABLE_FFMPEG_DECODER 1
#include <chiaki/ffmpegdecoder.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#else
#undef CHIAKI_ENABLE_FFMPEG_DECODER
#define CHIAKI_ENABLE_FFMPEG_DECODER 0
#endif

#include <json-c/json.h>
#include <argp.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif

static char doc[] = "Stream PS4 or PS5 Remote Play headlessly with stdin/stdout.";

#define ARG_KEY_HOST            'h'
#define ARG_KEY_REGISTKEY       'r'
#define ARG_KEY_MORNING         'm'
#define ARG_KEY_RP_KEY          1001
#define ARG_KEY_PS4             '4'
#define ARG_KEY_PS5             '5'
#define ARG_KEY_VIDEO_FORMAT    'v'
#define ARG_KEY_INPUT_FORMAT    'i'
#define ARG_KEY_RESOLUTION      1002
#define ARG_KEY_FPS             1003
#define ARG_KEY_BITRATE         'b'
#define ARG_KEY_NO_VIDEO        1004
#define ARG_KEY_NO_AUDIO        1005
#define ARG_KEY_CODEC           1006

static struct argp_option options[] = {
	{ "host", ARG_KEY_HOST, "HOST", 0, "Console hostname or IP address", 0 },
	{ "regist-key", ARG_KEY_REGISTKEY, "KEY", 0, "Remote Play registration key", 0 },
	{ "morning", ARG_KEY_MORNING, "KEY", 0, "Remote Play morning / session auth key (Base64)", 0 },
	{ "rp-key", ARG_KEY_RP_KEY, "KEY", 0, "Alias for --morning", 0 },
	{ "ps4", ARG_KEY_PS4, NULL, 0, "PlayStation 4", 0 },
	{ "ps5", ARG_KEY_PS5, NULL, 0, "PlayStation 5 (default)", 0 },
	{ "codec", ARG_KEY_CODEC, "CODEC", 0, "Video codec: h264, h265 (default: h265 for PS5, h264 for PS4)", 0 },
	{ "video-format", ARG_KEY_VIDEO_FORMAT, "FORMAT", 0, "Output video format: png, yuv420p, raw-h264, raw-h265, raw-h264-stream, raw-h265-stream, none (default: png)", 0 },
	{ "input-format", ARG_KEY_INPUT_FORMAT, "FORMAT", 0, "Controller input format on stdin: json, lines (default: json)", 0 },
	{ "resolution", ARG_KEY_RESOLUTION, "RES", 0, "Video resolution: 360p, 540p, 720p, 1080p (default: 720p)", 0 },
	{ "fps", ARG_KEY_FPS, "FPS", 0, "Target framerate: 30, 60 (default: 60)", 0 },
	{ "bitrate", ARG_KEY_BITRATE, "KBPS", 0, "Video target bitrate in kbps", 0 },
	{ "no-video", ARG_KEY_NO_VIDEO, NULL, 0, "Disable video stream", 0 },
	{ "no-audio", ARG_KEY_NO_AUDIO, NULL, 0, "Disable audio stream", 0 },
	{ 0 }
};

typedef enum {
	VIDEO_OUT_PNG,
	VIDEO_OUT_YUV420P,
	VIDEO_OUT_RAW_H264,
	VIDEO_OUT_RAW_H265,
	VIDEO_OUT_RAW_H264_STREAM,
	VIDEO_OUT_RAW_H265_STREAM,
	VIDEO_OUT_NONE
} VideoOutFormat;

typedef enum {
	INPUT_FORMAT_JSON,
	INPUT_FORMAT_LINES
} InputFormat;

typedef struct arguments {
	const char *host;
	const char *regist_key;
	const char *morning;
	bool ps5;
	VideoOutFormat video_format;
	InputFormat input_format;
	ChiakiVideoResolutionPreset resolution;
	ChiakiVideoFPSPreset fps;
	unsigned int bitrate;
	bool no_video;
	bool no_audio;
	ChiakiCodec codec;
	bool codec_specified;
} Arguments;

typedef struct stream_context {
	ChiakiLog *log;
	Arguments args;
	ChiakiSession session;
	ChiakiControllerState controller_state;
	ChiakiMutex controller_mutex;
	ChiakiThread stdin_thread;
	volatile bool running;

	uint64_t frame_index;
	unsigned int stream_width;
	unsigned int stream_height;

#if CHIAKI_ENABLE_FFMPEG_DECODER
	ChiakiFfmpegDecoder decoder;
	bool decoder_initialized;
	AVCodecContext *png_enc_ctx;
	struct SwsContext *sws_ctx;
	int sws_src_w, sws_src_h;
	enum AVPixelFormat sws_src_fmt;
#endif
} StreamContext;

static volatile sig_atomic_t g_stop_requested = 0;
static StreamContext *g_stream_ctx = NULL;

static void sig_handler(int sig)
{
	(void)sig;
	g_stop_requested = 1;
	if(g_stream_ctx)
	{
		g_stream_ctx->running = false;
		chiaki_session_stop(&g_stream_ctx->session);
	}
}

static int parse_opt(int key, char *arg, struct argp_state *state)
{
	Arguments *arguments = state->input;

	switch(key)
	{
		case ARG_KEY_HOST:
			arguments->host = arg;
			break;
		case ARG_KEY_REGISTKEY:
			arguments->regist_key = arg;
			break;
		case ARG_KEY_MORNING:
		case ARG_KEY_RP_KEY:
			arguments->morning = arg;
			break;
		case ARG_KEY_PS4:
			arguments->ps5 = false;
			break;
		case ARG_KEY_PS5:
			arguments->ps5 = true;
			break;
		case ARG_KEY_VIDEO_FORMAT:
			if(strcmp(arg, "png") == 0)
				arguments->video_format = VIDEO_OUT_PNG;
			else if(strcmp(arg, "yuv420p") == 0)
				arguments->video_format = VIDEO_OUT_YUV420P;
			else if(strcmp(arg, "raw-h264") == 0)
				arguments->video_format = VIDEO_OUT_RAW_H264;
			else if(strcmp(arg, "raw-h265") == 0)
				arguments->video_format = VIDEO_OUT_RAW_H265;
			else if(strcmp(arg, "raw-h264-stream") == 0)
				arguments->video_format = VIDEO_OUT_RAW_H264_STREAM;
			else if(strcmp(arg, "raw-h265-stream") == 0)
				arguments->video_format = VIDEO_OUT_RAW_H265_STREAM;
			else if(strcmp(arg, "none") == 0)
				arguments->video_format = VIDEO_OUT_NONE;
			else
			{
				fprintf(stderr, "Unknown video format: %s\n", arg);
				argp_usage(state);
			}
			break;
		case ARG_KEY_INPUT_FORMAT:
			if(strcmp(arg, "json") == 0)
				arguments->input_format = INPUT_FORMAT_JSON;
			else if(strcmp(arg, "lines") == 0)
				arguments->input_format = INPUT_FORMAT_LINES;
			else
			{
				fprintf(stderr, "Unknown input format: %s\n", arg);
				argp_usage(state);
			}
			break;
		case ARG_KEY_RESOLUTION:
			if(strcmp(arg, "360p") == 0)
				arguments->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
			else if(strcmp(arg, "540p") == 0)
				arguments->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
			else if(strcmp(arg, "720p") == 0)
				arguments->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
			else if(strcmp(arg, "1080p") == 0)
				arguments->resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_1080p;
			else
			{
				fprintf(stderr, "Unknown resolution: %s (choose 360p, 540p, 720p, 1080p)\n", arg);
				argp_usage(state);
			}
			break;
		case ARG_KEY_FPS:
		{
			int fps = atoi(arg);
			if(fps == 30)
				arguments->fps = CHIAKI_VIDEO_FPS_PRESET_30;
			else if(fps == 60)
				arguments->fps = CHIAKI_VIDEO_FPS_PRESET_60;
			else
			{
				fprintf(stderr, "Unknown fps: %s (choose 30 or 60)\n", arg);
				argp_usage(state);
			}
			break;
		}
		case ARG_KEY_BITRATE:
			arguments->bitrate = (unsigned int)atoi(arg);
			break;
		case ARG_KEY_NO_VIDEO:
			arguments->no_video = true;
			arguments->video_format = VIDEO_OUT_NONE;
			break;
		case ARG_KEY_NO_AUDIO:
			arguments->no_audio = true;
			break;
		case ARG_KEY_CODEC:
			if(strcasecmp(arg, "h264") == 0)
			{
				arguments->codec = CHIAKI_CODEC_H264;
				arguments->codec_specified = true;
			}
			else if(strcasecmp(arg, "h265") == 0 || strcasecmp(arg, "hevc") == 0)
			{
				arguments->codec = CHIAKI_CODEC_H265;
				arguments->codec_specified = true;
			}
			else
			{
				fprintf(stderr, "Unknown codec: %s (choose h264 or h265)\n", arg);
				argp_usage(state);
			}
			break;
		case ARGP_KEY_ARG:
			argp_usage(state);
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { options, parse_opt, 0, doc, 0, 0, 0 };

#if CHIAKI_ENABLE_FFMPEG_DECODER

static void free_png_encoder(StreamContext *ctx)
{
	if(ctx->sws_ctx)
	{
		sws_freeContext(ctx->sws_ctx);
		ctx->sws_ctx = NULL;
	}
	if(ctx->png_enc_ctx)
	{
		avcodec_free_context(&ctx->png_enc_ctx);
		ctx->png_enc_ctx = NULL;
	}
	ctx->sws_src_w = 0;
	ctx->sws_src_h = 0;
}

static bool init_png_encoder(StreamContext *ctx, int width, int height, enum AVPixelFormat src_fmt)
{
	if(ctx->png_enc_ctx && ctx->sws_src_w == width && ctx->sws_src_h == height && ctx->sws_src_fmt == src_fmt)
		return true;

	free_png_encoder(ctx);

	const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
	if(!codec)
	{
		CHIAKI_LOGE(ctx->log, "FFmpeg PNG encoder not available");
		return false;
	}

	ctx->png_enc_ctx = avcodec_alloc_context3(codec);
	if(!ctx->png_enc_ctx)
	{
		CHIAKI_LOGE(ctx->log, "Failed to allocate PNG encoder context");
		return false;
	}

	ctx->png_enc_ctx->width = width;
	ctx->png_enc_ctx->height = height;
	ctx->png_enc_ctx->pix_fmt = AV_PIX_FMT_RGB24;
	ctx->png_enc_ctx->time_base = (AVRational){1, 60};

	if(avcodec_open2(ctx->png_enc_ctx, codec, NULL) < 0)
	{
		CHIAKI_LOGE(ctx->log, "Failed to open PNG encoder");
		avcodec_free_context(&ctx->png_enc_ctx);
		return false;
	}

	ctx->sws_ctx = sws_getContext(width, height, src_fmt,
	                              width, height, AV_PIX_FMT_RGB24,
	                              SWS_BILINEAR, NULL, NULL, NULL);
	if(!ctx->sws_ctx)
	{
		CHIAKI_LOGE(ctx->log, "Failed to create SwsContext");
		avcodec_free_context(&ctx->png_enc_ctx);
		return false;
	}

	ctx->sws_src_w = width;
	ctx->sws_src_h = height;
	ctx->sws_src_fmt = src_fmt;
	return true;
}

static void output_png_frame(StreamContext *ctx, AVFrame *frame, double pts)
{
	if(!init_png_encoder(ctx, frame->width, frame->height, (enum AVPixelFormat)frame->format))
		return;

	AVFrame *rgb_frame = av_frame_alloc();
	if(!rgb_frame)
		return;

	rgb_frame->format = AV_PIX_FMT_RGB24;
	rgb_frame->width = frame->width;
	rgb_frame->height = frame->height;
	if(av_frame_get_buffer(rgb_frame, 0) < 0)
	{
		av_frame_free(&rgb_frame);
		return;
	}

	sws_scale(ctx->sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,
	          0, frame->height, rgb_frame->data, rgb_frame->linesize);

	rgb_frame->pts = frame->pts;

	if(avcodec_send_frame(ctx->png_enc_ctx, rgb_frame) >= 0)
	{
		AVPacket *pkt = av_packet_alloc();
		if(pkt)
		{
			if(avcodec_receive_packet(ctx->png_enc_ctx, pkt) >= 0)
			{
				fprintf(stdout, "FRAME %d %d %.6f %" PRIu64 " %d\n",
				        frame->width, frame->height, pts, ctx->frame_index++, pkt->size);
				fwrite(pkt->data, 1, pkt->size, stdout);
				fputc('\n', stdout);
				fflush(stdout);
			}
			av_packet_free(&pkt);
		}
	}

	av_frame_free(&rgb_frame);
}

static void output_yuv420p_frame(StreamContext *ctx, AVFrame *frame, double pts)
{
	int w = frame->width;
	int h = frame->height;
	int uv_w = w / 2;
	int uv_h = h / 2;
	size_t y_size = (size_t)w * (size_t)h;
	size_t uv_size = (size_t)uv_w * (size_t)uv_h;
	size_t total_size = y_size + 2 * uv_size;

	fprintf(stdout, "FRAME %d %d %.6f %" PRIu64 " %zu\n",
	        w, h, pts, ctx->frame_index++, total_size);

	if(frame->linesize[0] == w)
	{
		fwrite(frame->data[0], 1, y_size, stdout);
	}
	else
	{
		for(int y = 0; y < h; y++)
			fwrite(frame->data[0] + y * frame->linesize[0], 1, w, stdout);
	}

	if(frame->linesize[1] == uv_w)
	{
		fwrite(frame->data[1], 1, uv_size, stdout);
	}
	else
	{
		for(int y = 0; y < uv_h; y++)
			fwrite(frame->data[1] + y * frame->linesize[1], 1, uv_w, stdout);
	}

	if(frame->linesize[2] == uv_w)
	{
		fwrite(frame->data[2], 1, uv_size, stdout);
	}
	else
	{
		for(int y = 0; y < uv_h; y++)
			fwrite(frame->data[2] + y * frame->linesize[2], 1, uv_w, stdout);
	}

	fputc('\n', stdout);
	fflush(stdout);
}

static void ffmpeg_frame_available_cb(ChiakiFfmpegDecoder *decoder, void *user)
{
	StreamContext *ctx = user;
	int32_t frames_lost = 0;
	ChiakiFfmpegFrame f = chiaki_ffmpeg_decoder_pull_frame(decoder, &frames_lost);
	if(!f.frame)
		return;

	if(ctx->args.video_format == VIDEO_OUT_PNG)
		output_png_frame(ctx, f.frame, f.pts);
	else if(ctx->args.video_format == VIDEO_OUT_YUV420P)
		output_yuv420p_frame(ctx, f.frame, f.pts);

	av_frame_free(&f.frame);
}

#endif // CHIAKI_ENABLE_FFMPEG_DECODER

static bool video_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost, bool frame_recovered, void *user)
{
	StreamContext *ctx = user;
	if(!ctx->running)
		return false;

	if(ctx->args.video_format == VIDEO_OUT_RAW_H264_STREAM || ctx->args.video_format == VIDEO_OUT_RAW_H265_STREAM)
	{
		fwrite(buf, 1, buf_size, stdout);
		fflush(stdout);
		return true;
	}

	if(ctx->args.video_format == VIDEO_OUT_RAW_H264 || ctx->args.video_format == VIDEO_OUT_RAW_H265)
	{
		double pts = (double)chiaki_time_now_monotonic_us() / 1000000.0;
		fprintf(stdout, "FRAME %u %u %.6f %" PRIu64 " %zu\n",
		        ctx->stream_width, ctx->stream_height, pts, ctx->frame_index++, buf_size);
		fwrite(buf, 1, buf_size, stdout);
		fputc('\n', stdout);
		fflush(stdout);
		return true;
	}

#if CHIAKI_ENABLE_FFMPEG_DECODER
	if(ctx->decoder_initialized)
		return chiaki_ffmpeg_decoder_video_sample_cb(buf, buf_size, frames_lost, frame_recovered, &ctx->decoder);
#endif

	return true;
}

static void event_cb(ChiakiEvent *event, void *user)
{
	StreamContext *ctx = user;
	switch(event->type)
	{
		case CHIAKI_EVENT_CONNECTED:
			fprintf(stderr, "{\"event\": \"connected\", \"width\": %u, \"height\": %u}\n",
			        ctx->stream_width, ctx->stream_height);
			fflush(stderr);
			break;
		case CHIAKI_EVENT_QUIT:
			fprintf(stderr, "{\"event\": \"quit\", \"reason\": \"%s\"}\n",
			        chiaki_quit_reason_string(event->quit.reason));
			fflush(stderr);
			ctx->running = false;
			break;
		case CHIAKI_EVENT_RUMBLE:
			fprintf(stderr, "{\"event\": \"rumble\", \"left\": %u, \"right\": %u}\n",
			        event->rumble.left, event->rumble.right);
			fflush(stderr);
			break;
		case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
			fprintf(stderr, "{\"event\": \"login_pin_request\", \"pin_incorrect\": %s}\n",
			        event->login_pin_request.pin_incorrect ? "true" : "false");
			fflush(stderr);
			break;
		case CHIAKI_EVENT_VIDEO_FEC_FAILURE:
			fprintf(stderr, "{\"event\": \"video_fec_failure\"}\n");
			fflush(stderr);
			break;
		case CHIAKI_EVENT_KEYBOARD_OPEN:
		case CHIAKI_EVENT_KEYBOARD_TEXT_CHANGE:
			fprintf(stderr, "{\"event\": \"keyboard\", \"text\": \"%s\"}\n",
			        event->keyboard.text_str ? event->keyboard.text_str : "");
			fflush(stderr);
			break;
		case CHIAKI_EVENT_KEYBOARD_REMOTE_CLOSE:
			fprintf(stderr, "{\"event\": \"keyboard_close\"}\n");
			fflush(stderr);
			break;
		default:
			break;
	}
}

CHIAKI_EXPORT uint32_t chiaki_cli_parse_button_name(const char *name)
{
	if(!name)
		return 0;
	if(strcasecmp(name, "CROSS") == 0 || strcasecmp(name, "X") == 0)
		return CHIAKI_CONTROLLER_BUTTON_CROSS;
	if(strcasecmp(name, "MOON") == 0 || strcasecmp(name, "CIRCLE") == 0 || strcasecmp(name, "O") == 0)
		return CHIAKI_CONTROLLER_BUTTON_MOON;
	if(strcasecmp(name, "BOX") == 0 || strcasecmp(name, "SQUARE") == 0)
		return CHIAKI_CONTROLLER_BUTTON_BOX;
	if(strcasecmp(name, "PYRAMID") == 0 || strcasecmp(name, "TRIANGLE") == 0)
		return CHIAKI_CONTROLLER_BUTTON_PYRAMID;
	if(strcasecmp(name, "DPAD_LEFT") == 0 || strcasecmp(name, "LEFT") == 0)
		return CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
	if(strcasecmp(name, "DPAD_RIGHT") == 0 || strcasecmp(name, "RIGHT") == 0)
		return CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
	if(strcasecmp(name, "DPAD_UP") == 0 || strcasecmp(name, "UP") == 0)
		return CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
	if(strcasecmp(name, "DPAD_DOWN") == 0 || strcasecmp(name, "DOWN") == 0)
		return CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
	if(strcasecmp(name, "L1") == 0)
		return CHIAKI_CONTROLLER_BUTTON_L1;
	if(strcasecmp(name, "R1") == 0)
		return CHIAKI_CONTROLLER_BUTTON_R1;
	if(strcasecmp(name, "L3") == 0)
		return CHIAKI_CONTROLLER_BUTTON_L3;
	if(strcasecmp(name, "R3") == 0)
		return CHIAKI_CONTROLLER_BUTTON_R3;
	if(strcasecmp(name, "OPTIONS") == 0)
		return CHIAKI_CONTROLLER_BUTTON_OPTIONS;
	if(strcasecmp(name, "SHARE") == 0 || strcasecmp(name, "CREATE") == 0)
		return CHIAKI_CONTROLLER_BUTTON_SHARE;
	if(strcasecmp(name, "TOUCHPAD") == 0)
		return CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;
	if(strcasecmp(name, "PS") == 0)
		return CHIAKI_CONTROLLER_BUTTON_PS;
	return 0;
}

CHIAKI_EXPORT void chiaki_cli_parse_json_input(ChiakiControllerState *state, const char *line)
{
	if(!state || !line)
		return;

	struct json_object *obj = json_tokener_parse(line);
	if(!obj)
		return;

	struct json_object *idle_val;
	if(json_object_object_get_ex(obj, "idle", &idle_val) && json_object_get_boolean(idle_val))
	{
		chiaki_controller_state_set_idle(state);
	}

	struct json_object *buttons_arr;
	if(json_object_object_get_ex(obj, "buttons", &buttons_arr))
	{
		state->buttons = 0;
		if(json_object_is_type(buttons_arr, json_type_array))
		{
			size_t len = json_object_array_length(buttons_arr);
			for(size_t i = 0; i < len; i++)
			{
				struct json_object *b = json_object_array_get_idx(buttons_arr, i);
				if(b && json_object_is_type(b, json_type_string))
					state->buttons |= chiaki_cli_parse_button_name(json_object_get_string(b));
			}
		}
	}

	struct json_object *val;
	if(json_object_object_get_ex(obj, "left_x", &val))
	{
		double d = json_object_get_double(val);
		if(d >= -1.0 && d <= 1.0 && (d < -0.00001 || d > 0.00001))
			state->left_x = (int16_t)(d * 32767.0);
		else
			state->left_x = (int16_t)json_object_get_int(val);
	}
	if(json_object_object_get_ex(obj, "left_y", &val))
	{
		double d = json_object_get_double(val);
		if(d >= -1.0 && d <= 1.0 && (d < -0.00001 || d > 0.00001))
			state->left_y = (int16_t)(d * 32767.0);
		else
			state->left_y = (int16_t)json_object_get_int(val);
	}
	if(json_object_object_get_ex(obj, "right_x", &val))
	{
		double d = json_object_get_double(val);
		if(d >= -1.0 && d <= 1.0 && (d < -0.00001 || d > 0.00001))
			state->right_x = (int16_t)(d * 32767.0);
		else
			state->right_x = (int16_t)json_object_get_int(val);
	}
	if(json_object_object_get_ex(obj, "right_y", &val))
	{
		double d = json_object_get_double(val);
		if(d >= -1.0 && d <= 1.0 && (d < -0.00001 || d > 0.00001))
			state->right_y = (int16_t)(d * 32767.0);
		else
			state->right_y = (int16_t)json_object_get_int(val);
	}
	if(json_object_object_get_ex(obj, "l2", &val))
	{
		double d = json_object_get_double(val);
		if(d > 0.0 && d <= 1.0)
			state->l2_state = (uint8_t)(d * 255.0);
		else
			state->l2_state = (uint8_t)json_object_get_int(val);
	}
	if(json_object_object_get_ex(obj, "r2", &val))
	{
		double d = json_object_get_double(val);
		if(d > 0.0 && d <= 1.0)
			state->r2_state = (uint8_t)(d * 255.0);
		else
			state->r2_state = (uint8_t)json_object_get_int(val);
	}

	struct json_object *touches_arr;
	if(json_object_object_get_ex(obj, "touches", &touches_arr) && json_object_is_type(touches_arr, json_type_array))
	{
		size_t len = json_object_array_length(touches_arr);
		for(size_t i = 0; i < len && i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
		{
			struct json_object *t = json_object_array_get_idx(touches_arr, i);
			struct json_object *tid, *tx, *ty;
			if(json_object_object_get_ex(t, "id", &tid) &&
			   json_object_object_get_ex(t, "x", &tx) &&
			   json_object_object_get_ex(t, "y", &ty))
			{
				state->touches[i].id = (int8_t)json_object_get_int(tid);
				state->touches[i].x = (uint16_t)json_object_get_int(tx);
				state->touches[i].y = (uint16_t)json_object_get_int(ty);
			}
		}
	}

	json_object_put(obj);
}

CHIAKI_EXPORT void chiaki_cli_parse_lines_input(ChiakiControllerState *state, const char *line)
{
	if(!state || !line)
		return;

	char cmd[32] = {0}, arg1[32] = {0}, arg2[32] = {0};
	int count = sscanf(line, "%31s %31s %31s", cmd, arg1, arg2);
	if(count <= 0)
		return;

	if(strcasecmp(cmd, "IDLE") == 0 || strcasecmp(cmd, "RESET") == 0)
	{
		chiaki_controller_state_set_idle(state);
	}
	else if(strcasecmp(cmd, "DOWN") == 0 && count >= 2)
	{
		state->buttons |= chiaki_cli_parse_button_name(arg1);
	}
	else if(strcasecmp(cmd, "UP") == 0 && count >= 2)
	{
		state->buttons &= ~chiaki_cli_parse_button_name(arg1);
	}
	else if(strcasecmp(cmd, "BUTTON") == 0 && count >= 3)
	{
		uint32_t btn = chiaki_cli_parse_button_name(arg1);
		if(strcasecmp(arg2, "DOWN") == 0)
			state->buttons |= btn;
		else if(strcasecmp(arg2, "UP") == 0)
			state->buttons &= ~btn;
	}
	else if(strcasecmp(cmd, "AXIS") == 0 && count >= 3)
	{
		int val = atoi(arg2);
		if(strcasecmp(arg1, "LX") == 0)
			state->left_x = (int16_t)val;
		else if(strcasecmp(arg1, "LY") == 0)
			state->left_y = (int16_t)val;
		else if(strcasecmp(arg1, "RX") == 0)
			state->right_x = (int16_t)val;
		else if(strcasecmp(arg1, "RY") == 0)
			state->right_y = (int16_t)val;
	}
	else if(strcasecmp(cmd, "TRIGGER") == 0 && count >= 3)
	{
		int val = atoi(arg2);
		if(strcasecmp(arg1, "L2") == 0)
			state->l2_state = (uint8_t)val;
		else if(strcasecmp(arg1, "R2") == 0)
			state->r2_state = (uint8_t)val;
	}
}

static void parse_json_input(StreamContext *ctx, const char *line)
{
	chiaki_mutex_lock(&ctx->controller_mutex);
	chiaki_cli_parse_json_input(&ctx->controller_state, line);
	ChiakiControllerState copy = ctx->controller_state;
	chiaki_mutex_unlock(&ctx->controller_mutex);

	ChiakiErrorCode err = chiaki_session_set_controller_state(&ctx->session, &copy);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGW(ctx->log, "Failed to set controller state: %s", chiaki_error_string(err));
	}
}

static void parse_lines_input(StreamContext *ctx, const char *line)
{
	chiaki_mutex_lock(&ctx->controller_mutex);
	chiaki_cli_parse_lines_input(&ctx->controller_state, line);
	ChiakiControllerState copy = ctx->controller_state;
	chiaki_mutex_unlock(&ctx->controller_mutex);

	ChiakiErrorCode err = chiaki_session_set_controller_state(&ctx->session, &copy);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGW(ctx->log, "Failed to set controller state: %s", chiaki_error_string(err));
	}
}

static void *stdin_worker(void *arg)
{
	StreamContext *ctx = arg;
	char line_buf[4096];
	CHIAKI_LOGI(ctx->log, "Stream stdin reader worker active");

	while(ctx->running && !g_stop_requested)
	{
		if(!fgets(line_buf, sizeof(line_buf), stdin))
		{
			CHIAKI_LOGI(ctx->log, "Stream stdin reached EOF or read error");
			break;
		}

		size_t len = strlen(line_buf);
		while(len > 0 && (line_buf[len - 1] == '\r' || line_buf[len - 1] == '\n'))
			line_buf[--len] = '\0';

		if(len == 0)
			continue;

		if(ctx->args.input_format == INPUT_FORMAT_JSON)
			parse_json_input(ctx, line_buf);
		else
			parse_lines_input(ctx, line_buf);
	}

	CHIAKI_LOGI(ctx->log, "Stream stdin reader worker exiting");
	return NULL;
}

CHIAKI_EXPORT int chiaki_cli_cmd_stream(ChiakiLog *log, int argc, char *argv[])
{
	Arguments arguments = { 0 };
	arguments.ps5 = true;
	arguments.video_format = VIDEO_OUT_PNG;
	arguments.input_format = INPUT_FORMAT_JSON;
	arguments.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
	arguments.fps = CHIAKI_VIDEO_FPS_PRESET_60;

	error_t argp_r = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
	if(argp_r != 0)
		return 1;

	if(!arguments.host)
	{
		fprintf(stderr, "Error: No --host specified. See --help.\n");
		return 1;
	}
	if(!arguments.regist_key)
	{
		fprintf(stderr, "Error: No --regist-key specified. See --help.\n");
		return 1;
	}
	if(!arguments.morning)
	{
		fprintf(stderr, "Error: No --morning (or --rp-key) specified. See --help.\n");
		return 1;
	}

#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	StreamContext ctx = { 0 };
	ctx.log = log;
	ctx.args = arguments;
	ctx.running = true;
	chiaki_controller_state_set_idle(&ctx.controller_state);
	chiaki_mutex_init(&ctx.controller_mutex, false);

	ChiakiConnectInfo connect_info = { 0 };
	connect_info.ps5 = arguments.ps5;
	connect_info.host = arguments.host;
	connect_info.video_profile_auto_downgrade = true;
	chiaki_connect_video_profile_preset(&connect_info.video_profile, arguments.resolution, arguments.fps);

	if(arguments.codec_specified)
		connect_info.video_profile.codec = arguments.codec;
	else if(arguments.ps5)
		connect_info.video_profile.codec = CHIAKI_CODEC_H265;
	else
		connect_info.video_profile.codec = CHIAKI_CODEC_H264;

	connect_info.enable_idr_on_fec_failure = true;
	connect_info.enable_dualsense = arguments.ps5;
	connect_info.enable_keyboard = false;

	if(arguments.bitrate > 0)
		connect_info.video_profile.bitrate = arguments.bitrate;

	ctx.stream_width = connect_info.video_profile.width;
	ctx.stream_height = connect_info.video_profile.height;

	if(arguments.no_video)
		connect_info.audio_video_disabled |= CHIAKI_VIDEO_DISABLED;
	if(arguments.no_audio)
		connect_info.audio_video_disabled |= CHIAKI_AUDIO_DISABLED;

	// Parse registration key
	size_t regist_key_len = strlen(arguments.regist_key);
	if(regist_key_len > sizeof(connect_info.regist_key))
	{
		size_t decoded_size = sizeof(connect_info.regist_key);
		if(chiaki_base64_decode(arguments.regist_key, regist_key_len, (uint8_t *)connect_info.regist_key, &decoded_size) != CHIAKI_ERR_SUCCESS)
		{
			fprintf(stderr, "Error: Failed to parse regist-key (neither plain string nor base64)\n");
			return 1;
		}
	}
	else
	{
		memcpy(connect_info.regist_key, arguments.regist_key, regist_key_len);
	}

	// Parse morning / rp_key (base64)
	size_t morning_size = sizeof(connect_info.morning);
	ChiakiErrorCode b64_err = chiaki_base64_decode(arguments.morning, strlen(arguments.morning), connect_info.morning, &morning_size);
	if(b64_err != CHIAKI_ERR_SUCCESS || morning_size != sizeof(connect_info.morning))
	{
		fprintf(stderr, "Error: Failed to decode --morning / --rp-key from Base64 (expected %zu bytes)\n", sizeof(connect_info.morning));
		return 1;
	}

#if CHIAKI_ENABLE_FFMPEG_DECODER
	if(arguments.video_format == VIDEO_OUT_PNG || arguments.video_format == VIDEO_OUT_YUV420P)
	{
		ChiakiErrorCode dec_err = chiaki_ffmpeg_decoder_init(&ctx.decoder, log, connect_info.video_profile.codec,
		                                                     connect_info.video_profile.max_fps, NULL, NULL,
		                                                     ffmpeg_frame_available_cb, &ctx);
		if(dec_err != CHIAKI_ERR_SUCCESS)
		{
			fprintf(stderr, "Error: Failed to initialize FFmpeg decoder: %s\n", chiaki_error_string(dec_err));
			return 1;
		}
		ctx.decoder_initialized = true;
	}
#else
	if(arguments.video_format == VIDEO_OUT_PNG || arguments.video_format == VIDEO_OUT_YUV420P)
	{
		fprintf(stderr, "Error: Video format '%s' requires FFmpeg decoder support, but it was disabled at build time.\n",
		        arguments.video_format == VIDEO_OUT_PNG ? "png" : "yuv420p");
		return 1;
	}
#endif

	ChiakiErrorCode err = chiaki_session_init(&ctx.session, &connect_info, log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "Error: Failed to initialize Chiaki session: %s\n", chiaki_error_string(err));
#if CHIAKI_ENABLE_FFMPEG_DECODER
		if(ctx.decoder_initialized)
			chiaki_ffmpeg_decoder_fini(&ctx.decoder);
#endif
		return 1;
	}

	chiaki_session_set_event_cb(&ctx.session, event_cb, &ctx);
	if(!arguments.no_video && arguments.video_format != VIDEO_OUT_NONE)
		chiaki_session_set_video_sample_cb(&ctx.session, video_sample_cb, &ctx);

	g_stream_ctx = &ctx;
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	err = chiaki_session_start(&ctx.session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "Error: Failed to start Chiaki session: %s\n", chiaki_error_string(err));
		chiaki_session_fini(&ctx.session);
#if CHIAKI_ENABLE_FFMPEG_DECODER
		if(ctx.decoder_initialized)
			chiaki_ffmpeg_decoder_fini(&ctx.decoder);
#endif
		return 1;
	}

	chiaki_thread_create(&ctx.stdin_thread, stdin_worker, &ctx);

	// Join session (blocks until session stops / quits)
	chiaki_session_join(&ctx.session);
	ctx.running = false;

	chiaki_thread_join(&ctx.stdin_thread, NULL);
	chiaki_session_fini(&ctx.session);

#if CHIAKI_ENABLE_FFMPEG_DECODER
	if(ctx.decoder_initialized)
	{
		free_png_encoder(&ctx);
		chiaki_ffmpeg_decoder_fini(&ctx.decoder);
	}
#endif

	chiaki_mutex_fini(&ctx.controller_mutex);
	g_stream_ctx = NULL;

	return 0;
}
