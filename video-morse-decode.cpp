/*

Decode morse code from video.

Usage :

video-morse-decode <video_filename> <start_frame> <end_frame> <x0> <y0> <x1> <y1>

Example :

youtube-dl https://www.youtube.com/watch?v=... --output video.mp4
./video-morse-decode video.mp4 - 0 -1 0.40 0.5 0.6 0.75

Options :

<video_filename> : MPEG4, AVI, FLV etc - anything FFmpeg supports
<start_frame>    : 0 = start from first frame, 30 = skip 1 second (if 30fps)
<end_frame>      : -1 = end at last frame, 60 = end at 2 second (if 30fps)
<x0> <y0>        : coordinates of top-left area to examine (0.0-1.0)
<x1> <y1>        : coordinates of bottom-right area to examine (0.0-1.0)

Compile :

g++ -O2 -std=c++14 $(pkg-config --cflags-only-I libavcodec) \
-o video-morse-decode video-morse-decode.cpp \
$(pkg-config --libs-only-l libavcodec libavutil \
libavfilter libavformat libswscale) -lm

*/

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <cstdint>
#include <cmath>

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace Util {

// replace first occurance of 'from' with 'to', in 'input'
bool replace(
	std::string& input,
	const std::string& from,
	const std::string& to
) {
	size_t start_pos = input.find(from);
	if (start_pos == std::string::npos) {
		return false;
	}
	input.replace(start_pos, from.length(), to);
	return true;
}

// return a copy of 'input' with all occurances of 'from' replaced with 'to'
std::string replace_all(
	const std::string & input,
	const std::string& from,
	const std::string& to
) {
	std::string out = input;
	while (replace(out, from, to));
	return out;
}

template <typename T>
T sign(const T & x)
{
	if (x > 0) { return 1; }
	else if (x < 0) { return -1; }
	return 0;
}

double gaussian(double x, double a)
{
	return sqrt(a / M_PI) * pow(M_E, -a * x * x);
}

/*
return indexes of turning points, largest to smallest frequency.

example:
get_local_maximums(vf, 3, 1) ->
where vf =
	0: 1
	5: 17
	8: 116   <- turning point: largest frequency, this is output first
	9: 18
	10: 2
	19: 1
	20: 9
	21: 12   <- turning point: 3rd largest frequency, output 3rd
	24: 5
	46: 1
	49: 6
	51: 56   <- turning point: 2nd largest frequency, output 2nd
	52: 6
	53: 1
return : [ 8, 51, 21 ]
*/
std::vector<int> get_local_maximums(
	const std::map<int, int> & vf, // value -> frequency pairs
	int count, // number of turning points required
	int window_size // gaussian smoothing window size
)
{
	std::vector<int> s(vf.rbegin()->first + 1);
	std::vector<std::pair<int, int>> tp;
	std::vector<int> r;
	int i = 0, d = 0;

	// convert map to vector
	for (const auto & e : vf) {
		s[e.first] = e.second;
	}

	// gaussian filter
	std::vector<int> o(s.size());
	for (int i = 0; i < s.size(); i++) {
		double t = 0;
		for (int j = -window_size; j < window_size+1; j++) {
			int k = i + j;
			if (k >= 0 && k < s.size()) {
				t += (double)s[k] * gaussian(j, 1);
			}
		}
		o[i] = t;
	}

	// find local maximums
	int ld = 0;
	for (i = 1; i < o.size(); i++) {
		d = sign(o[i] - o[i - 1]);
		if (i > 0 && (ld == 1 || ld == 0) && d == -1) {
			tp.push_back(std::pair<int, int>(i - 1, o[i-1]));
		}
		ld = d;
	}

	// last point rising?
	if (d > 0) {
		// add it
		tp.push_back(std::pair<int, int>(i - 1, o[i-1]));
	}

	// get n first points ordered by high to low frequency
	std::sort(tp.begin(), tp.end(), [](const auto & a, const auto & b) -> bool {
		return a.second > b.second;
	});

	std::vector<std::pair<int, int>>::const_iterator tpi = tp.begin();
	while (count && tpi != tp.end()) {
		r.push_back(tpi->first);
		tpi++;
		count--;
	}

	return r;
}

static std::string decodeMorse(const std::string & in)
{
	struct MorseSymbol {
		std::string pattern, string;
	};

	static const struct MorseSymbol morse_symbols[] = {
		{ ".-"   , "A" },
		{ "-..." , "B" },
		{ "-.-." , "C" },
		{ "-.."  , "D" },
		{ "."    , "E" },
		{ "..-." , "F" },
		{ "--."  , "G" },
		{ "...." , "H" },
		{ ".."   , "I" },
		{ ".---" , "J" },
		{ "-.-"  , "K" },
		{ ".-.." , "L" },
		{ "--"   , "M" },
		{ "-."   , "N" },
		{ "---"  , "O" },
		{ ".--." , "P" },
		{ "--.-" , "Q" },
		{ ".-."  , "R" },
		{ "..."  , "S" },
		{ "-"    , "T" },
		{ "..-"  , "U" },
		{ "...-" , "V" },
		{ ".--"  , "W" },
		{ "-..-" , "X" },
		{ "-.--" , "Y" },
		{ "--.." , "Z" },
		{ "-----", "0" },
		{ ".----", "1" },
		{ "..---", "2" },
		{ "...--", "3" },
		{ "....-", "4" },
		{ ".....", "5" },
		{ "-....", "6" },
		{ "--...", "7" },
		{ "---..", "8" },
		{ "----.", "9" },
		{ "---...", ":" },
		{ "-....-", "-" },
		{ ".-.-.-", "." }
	};

	std::string out = in;

	for (const auto & m : morse_symbols) {
		std::string from = std::string(" ") + m.pattern + " ";
		std::string to   = std::string(" ") + m.string + " ";
		out = replace_all(out, from, to);
	}

	out = replace_all(out, " ", "");
	out = replace_all(out, "|", " ");

	return out;
}

template <typename T>
T stringTo(const std::string & s)
{
	std::stringstream ss(s);
	T v;
	ss >> v;
	return v;
}

}

using namespace Util;

class VideoMorseDecode {
public :
	struct Options {
		double x0, y0, x1, y1;
		int start_frame, end_frame;
		std::string json_file_name;
		std::string video_file_name;
	};

	// summary of each frame
	struct Frame {
		unsigned time; // frame index (timestamp would be better)
		unsigned luminance; // average luminance from selected area
	};

	// store pulse or break signal duration
	struct Signal {
		unsigned state; // 0 = break, 1 = pulse
		unsigned duration; // in frames
	};

	VideoMorseDecode();

	void processFrame(
		const AVFrame *frame, int width, int height, int frame_index
	);

	bool parseOptions(int argc, char *argv[]);
	bool run();

private :
	void calculateHistogram();
	void processStateChanges();
	std::string processSignals();

	// command-line options
	Options m_options;

	// average luminance of frame -> number of frames
	std::vector<unsigned> m_frame_luminance_histogram;

	// stream to write JSON report
	std::ostream * m_json_stream;
	std::ofstream m_json_file;

	std::vector<Frame> m_frames;
	std::vector<Signal> m_signals;

	int m_mean_luminance;
};

VideoMorseDecode::VideoMorseDecode()
{
	m_frame_luminance_histogram.resize(256);
}

void VideoMorseDecode::processFrame(
	const AVFrame *frame, int width, int height, int frame_index
)
{
	int r, g, b, i, x, y, s = 0, t = 0;
	int x0 = width  * m_options.x0;
	int y0 = height * m_options.y0;
	int x1 = width  * m_options.x1;
	int y1 = height * m_options.y1;

	if (m_options.start_frame != -1 && frame_index < m_options.start_frame) {
		// ignore frame
		return;
	}

	if (m_options.end_frame != -1 && frame_index > m_options.end_frame) {
		// ignore frame
		return;
	}

	t = 0;
	for (y = y0; y < y1; y++) {
		const uint8_t *row = (const uint8_t *)(frame->data[0]+y*frame->linesize[0]);
		s = 0;
		for (x = x0; x < x1; x++) {
			r = (int)row[x*3];
			g = (int)row[x*3+1];
			b = (int)row[x*3+2];
			i = b; // use blue channel, works best for the BF4 lantern
			if (i < 0) { i = 0; }
			if (i > 255) { i = 255; }
			s += i;
		}
		t += s / (x1 - x0);
	}
	t /= y1 - y0;
	m_frame_luminance_histogram[t]++;

	Frame f;
	f.time = frame_index;
	f.luminance = t;
	m_frames.push_back(f);
}

void VideoMorseDecode::calculateHistogram()
{
	unsigned i, n, yi, j, y, sum = 0, max = 0;

	m_mean_luminance = 0;
	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++) {
			n = i * 16 + j;
			m_mean_luminance += n * m_frame_luminance_histogram[n];
			sum += m_frame_luminance_histogram[n];
		}
	}
	m_mean_luminance /= sum;

	std::string delim = "";

	*m_json_stream << "\"frame_hist\": [";
	for (const auto & e : m_frame_luminance_histogram) {
		*m_json_stream << delim << e;
		delim = ",";
	}
	*m_json_stream << "]\n";

	*m_json_stream << ",\"frame_hist_mean\": " << m_mean_luminance << "\n";
}

void VideoMorseDecode::processStateChanges()
{
	int state = 0, last_state = 0, last_time = 0;

	for (const auto & frame : m_frames) {
		if (frame.luminance < m_mean_luminance) {
			state = 0;
		} else if (frame.luminance >= m_mean_luminance) {
			state = 1;
		}
		if (state != last_state) {
			Signal signal;
			signal.state = last_state;
			signal.duration = frame.time - last_time;
			last_time = frame.time;
			m_signals.push_back(signal);
		}

		last_state = state;
	}
}

std::string VideoMorseDecode::processSignals()
{
	std::map<int, int> off_hist, on_hist;
	const int gaussian_window_size = 3;

	for (const auto & signal : m_signals) {
		if (signal.state == 0) {
			off_hist[signal.duration]++;
		} else {
			on_hist[signal.duration]++;
		}
	}

	auto off_time_peaks = get_local_maximums(off_hist, 3, gaussian_window_size);
	std::sort(std::begin(off_time_peaks), std::end(off_time_peaks));

	std::vector<int> off_thresholds(2);
	off_thresholds[0] = (off_time_peaks[0] + off_time_peaks[1]) / 2;
	off_thresholds[1] = (off_time_peaks[1] + off_time_peaks[2]) / 2;

	auto on_time_peaks = get_local_maximums(on_hist, 2, gaussian_window_size);
	std::sort(std::begin(on_time_peaks), std::end(on_time_peaks));

	std::vector<int> on_thresholds(1);
	on_thresholds[0] = (on_time_peaks[0] + on_time_peaks[1]) / 2;

	std::string morse;
	for (const auto & signal : m_signals) {
		if (signal.state == 0) {
			if (signal.duration < off_thresholds[0]) {
				// next symbol
			} else if (signal.duration >= off_thresholds[0] && signal.duration < off_thresholds[1]) {
				morse += " ";
			} else if (signal.duration >= off_thresholds[1]) {
				morse += " | ";
			}
		} else {
			if (signal.duration < on_thresholds[0]) {
				morse += ".";
			} else if (signal.duration >= on_thresholds[0]) {
				morse += "-";
			}
		}
	}

	std::string delim;

	*m_json_stream << ",\"hist_off\": [";
	delim = "";
	for (const auto & e : off_hist) {
		*m_json_stream << delim << "{" << e.first << ": " << e.second << "}";
		delim = ",";
	}
	*m_json_stream << "]\n";

	*m_json_stream << ",\"hist_on\": [";
	delim = "";
	for (const auto & e : on_hist) {
		*m_json_stream << delim << "{" << e.first << ": " << e.second << "}";
		delim = ",";
	}
	*m_json_stream << "]\n";

	*m_json_stream << ",\"off_time_peaks\": [";
	delim = "";
	for (const auto & e : off_time_peaks) {
		*m_json_stream << delim << e;
		delim = ",";
	}
	*m_json_stream << "]\n";

	*m_json_stream << ",\"off_thresholds\": [";
	delim = "";
	for (const auto & e : off_thresholds) {
		*m_json_stream << delim << e;
		delim = ",";
	}
	*m_json_stream << "]\n";

	*m_json_stream << ",\"on_time_peaks\": [";
	delim = "";
	for (const auto & e : on_time_peaks) {
		*m_json_stream << delim << e;
		delim = ",";
	}
	*m_json_stream << "]\n";

	*m_json_stream << ",\"on_thresholds\": [";
	delim = "";
	for (const auto & e : on_thresholds) {
		*m_json_stream << delim << e;
		delim = ",";
	}
	*m_json_stream << "]\n";

	return morse;
}

bool VideoMorseDecode::parseOptions(int argc, char *argv[])
{
	if (argc != 9) {
		std::cerr
			<< "usage: " << argv[0]
			<< " <video_filename> <json_filename>"
			<< " <start_frame> <end_frame>"
			<< " <x0> <y0> <x1> <y1>"
			<< "\n";
		return false;
	}

	m_options.video_file_name = argv[1];
	m_options.json_file_name = argv[2];
	m_options.start_frame = stringTo<int>(argv[3]);
	m_options.end_frame = stringTo<int>(argv[4]);
	m_options.x0 = stringTo<double>(argv[5]);
	m_options.y0 = stringTo<double>(argv[6]);
	m_options.x1 = stringTo<double>(argv[7]);
	m_options.y1 = stringTo<double>(argv[8]);

	if (m_options.json_file_name == "-") {
		m_json_stream = &std::cout;
	} else {
		m_json_file = std::ofstream(m_options.json_file_name);
		m_json_stream = &m_json_file;
	}

	return true;
}

bool VideoMorseDecode::run()
{
	// FFmpeg stuff
	AVFormatContext *format_context = NULL;
	AVCodecContext *codec_context = NULL;
	AVCodec *codec = NULL;
	AVFrame *frame = NULL;
	AVFrame *frame_rgb = NULL;
	AVDictionary *options_dict = NULL;
	struct SwsContext *sws_ctx = NULL;
	AVPacket packet;

	unsigned frame_index = 0;
	int video_stream = -1;
	int frame_finished = 0;
	size_t frame_bytes = 0;
	uint8_t *buffer = NULL;
	const char *video_file_name = NULL;
	const char *json_file_name = NULL;

	av_register_all();

	if (avformat_open_input(
		&format_context, m_options.video_file_name.c_str(), NULL, NULL) != 0
	) {
		std::cerr << "failed to open video file\n";
		return false;
	}

	if (avformat_find_stream_info(format_context, NULL) < 0) {
		std::cerr << "failed to find video stream\n";
		return false;
	}

	av_dump_format(format_context, 0, m_options.video_file_name.c_str(), 0);

	for (unsigned i = 0; i < format_context->nb_streams; i++) {
		const auto & codec = format_context->streams[i]->codec;
		if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream = i;
			break;
		}
	}

	if (video_stream == -1) {
		std::cerr << "failed to find video stream\n";
		return false;
	}

	codec_context = format_context->streams[video_stream]->codec;

	codec = avcodec_find_decoder(codec_context->codec_id);
	if (codec == NULL) {
		std::cerr << "unsupported video codec\n";
		return false;
	}

	if (avcodec_open2(codec_context, codec, &options_dict) < 0) {
		std::cerr << "unsupported video codec\n";
		return false;
	}

	frame = av_frame_alloc();
	if (frame == NULL) {
		std::cerr << "failed to allocate frame\n";
		return false;
	}

	frame_rgb = av_frame_alloc();
	if (frame_rgb == NULL) {
		std::cerr << "failed to allocate frame\n";
		return false;
	}

	frame_bytes = avpicture_get_size(PIX_FMT_RGB24,
		codec_context->width, codec_context->height);
	buffer = (uint8_t *)av_malloc(frame_bytes * sizeof(uint8_t));

	// convert to RGB24 pixel format
	sws_ctx = sws_getContext(
		codec_context->width, codec_context->height,
		codec_context->pix_fmt,
		codec_context->width, codec_context->height,
		PIX_FMT_RGB24, SWS_BILINEAR,
		NULL, NULL, NULL
	);

	avpicture_fill((AVPicture *)frame_rgb, buffer, PIX_FMT_RGB24,
		codec_context->width, codec_context->height);

	frame_index = 0;
	while (av_read_frame(format_context, &packet) >= 0) {
		if (packet.stream_index == video_stream) {
			avcodec_decode_video2(codec_context, frame, &frame_finished, &packet);
			if (frame_finished) {
				sws_scale(sws_ctx, (uint8_t const * const *)frame->data,
					frame->linesize, 0, codec_context->height,
					frame_rgb->data, frame_rgb->linesize
				);
				processFrame(frame_rgb,
					codec_context->width, codec_context->height, frame_index);
				frame_index++;
			}
		}
		av_free_packet(&packet);
	}

	*m_json_stream << "{\n";

	calculateHistogram();
	processStateChanges();

	auto morse = processSignals();
	*m_json_stream << ",\"morse\": \"" << morse << "\"\n";

	auto message = decodeMorse(morse);
	*m_json_stream << ",\"message\": \"" << message << "\"\n";

	*m_json_stream << "}\n";

	av_free(buffer);
	av_free(frame_rgb);
	av_free(frame);
	avcodec_close(codec_context);
	avformat_close_input(&format_context);

	return true;
}

int main(int argc, char *argv[])
{
	std::shared_ptr<VideoMorseDecode> vmd =
		std::make_shared<VideoMorseDecode>();

	vmd->parseOptions(argc, argv);
	vmd->run();

	return 0;
}
