#include <jni.h>
#include <string>
#include <android/log.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
}

#include <iostream>
#include <map>
#include <vector>
#include <algorithm>

struct Media {
    AVPacket* encoder_packet = av_packet_alloc();
    AVPacket* decoder_packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVAudioFifo* buffer;
    AVFrame* output_frame = nullptr;
    AVFormatContext* input_format_context = nullptr;
    AVFormatContext* output_format_context = nullptr;
    AVCodec* decoder = nullptr;
    AVCodecContext* decoder_context = nullptr;
    AVStream* input_stream = nullptr;
    AVStream* output_stream = nullptr;
    int audio_stream_index;
    AVCodec* encoder = nullptr;
    AVCodecContext* encoder_context = nullptr;
};

static Media* open_input_file(const char* url) {
    AVFormatContext* context = nullptr;
    if (avformat_open_input(&context, url, nullptr, nullptr) < 0)
        return nullptr;

    if (avformat_find_stream_info(context, nullptr) < 0)
        return nullptr;

    // Find the audio stream here
    int audio_stream_index = av_find_best_stream(context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    AVStream* audio_stream = context->streams[audio_stream_index];

    // Find the decoder
    AVCodec* decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!decoder) {
        avformat_close_input(&context);
        return nullptr;
    }
    AVCodecContext* decoder_context = avcodec_alloc_context3(decoder);
    if (!decoder_context) {
        avformat_close_input(&context);
        return nullptr;
    }

    // Copy the codec parameters to the decoder context
    if (avcodec_parameters_to_context(decoder_context, audio_stream->codecpar) < 0) {
        return nullptr;
    }

    // Open the codec
    if (avcodec_open2(decoder_context, decoder, nullptr) < 0) {
        return nullptr;
    }

    av_dump_format(context, audio_stream_index, url, false);

    Media* media = new Media;
    media->input_format_context = context;
    media->audio_stream_index = audio_stream_index;
    media->decoder_context = decoder_context;
    media->decoder = decoder;
    media->input_stream = audio_stream;

    media->buffer = av_audio_fifo_alloc(decoder_context->sample_fmt, decoder_context->channels, 1);

    std::cout << "Decoder context sample rate: " << decoder_context->sample_rate << std::endl;

    return media;
}

static bool write_frame(Media* media) {
    media->encoder_packet->stream_index = media->output_stream->index;
    return av_interleaved_write_frame(media->output_format_context, media->encoder_packet) >= 0;

}

static AVFrame* allocate_audio_frame(AVSampleFormat format, uint64_t channel_layout, int sample_rate, int nb_samples) {
    AVFrame* frame = av_frame_alloc();

    if (!frame) {
        return nullptr;
    }

    frame->format = format;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        if (av_frame_get_buffer(frame, 0) < 0) {
            std::cout << "Could not allocate buffers for the frame" << std::endl;
            __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not allocate buffers for the frame");
        }
    }

    return frame;
}

static bool open_output_file(Media* media, const char* url) {
    AVStream* output_stream;
    AVCodecContext* encoder_context;
    AVCodec* encoder;
    AVFormatContext* output_format_context;

    if (int ret = avformat_alloc_output_context2(&output_format_context, nullptr, nullptr, url) < 0) {
        std::cout << "Could not allocate output context" << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not create output context");
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Reason: %s", av_err2str(ret));
        return false;
    }

    std::cout << "Finding encoder..." << std::endl;
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Finding encoder...");
    encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!encoder) {
        std::cout << "No encoder found!" << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "No encoder found!");
        return false;
    }

    output_stream = avformat_new_stream(output_format_context, encoder);
    encoder_context = avcodec_alloc_context3(encoder);
    if (!encoder_context) {
        std::cout << "Could not allocate encoder context" << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not allocate encoder context");
        return false;
    }

    encoder_context->sample_rate = media->decoder_context->sample_rate;
    encoder_context->channel_layout = media->decoder_context->channel_layout;
    encoder_context->channels = av_get_channel_layout_nb_channels(encoder_context->channel_layout);
    encoder_context->sample_fmt = media->decoder_context->sample_fmt;
    encoder_context->time_base = {1, encoder_context->sample_rate};

    if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(encoder_context, encoder, nullptr) < 0) {
        std::cout << "Could not open encoder" << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not open encoder!");
        return false;
    }

    if (avcodec_parameters_from_context(output_stream->codecpar, encoder_context) < 0) {
        std::cout << "Could not copy params from encoder context" << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not copy params from encoder context");
    }

    output_format_context->metadata = media->input_format_context->metadata;

    output_stream->time_base = encoder_context->time_base;
    int ret;
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_format_context->pb, url, AVIO_FLAG_WRITE);
        if (ret < 0) {
            return false;
        }
    }
    av_dump_format(output_format_context, 0, url, true);

    ret = avformat_write_header(output_format_context, nullptr);
    if (ret < 0) {
        std::cout << "Could not write header" << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not write header");
        return false;
    }

    media->output_stream = output_stream;
    media->encoder = encoder;
    media->encoder_context = encoder_context;
    media->output_format_context = output_format_context;
    media->output_frame = allocate_audio_frame(media->decoder_context->sample_fmt, output_stream->codecpar->channel_layout, media->decoder_context->sample_rate, encoder_context->frame_size);

    return true;
}

static bool close_input_file(Media* media) {
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Closing input file...");
    avformat_close_input(&media->input_format_context);

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Done closing input files!");
    return true;
}

static bool close_output_file(Media* media) {
    bool completed = true;
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Closing output file...");
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Writing trailer...");
    completed = av_write_trailer(media->output_format_context) >= 0;
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Wrote trailer!!!");
    avcodec_free_context(&media->encoder_context);
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Freed encoder context");
    avformat_free_context(media->output_format_context);
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Freed the output format context");

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Finished closing output file");
    return completed;
}

static bool send_packet(Media* media) {
    return avcodec_send_packet(media->decoder_context, media->decoder_packet) >= 0;
}

static bool receive_frame(Media* media) {
    if (avcodec_receive_frame(media->decoder_context, media->frame) < 0) {
        return false;
    }

    int written = av_audio_fifo_write(media->buffer, (void**)media->frame->data, media->frame->nb_samples);

    std::cout << "Samples written: " << written << std::endl;

    av_frame_unref(media->frame);

    return true;
}

static bool send_frame(Media* media) {
    int ret;
    if ((ret = avcodec_send_frame(media->encoder_context, media->output_frame)) < 0) {
        char* buf = new char[1024];
        av_strerror(ret, buf, 1024);
        std::cout << "Ret is " << ret << ", Error: " << buf << std::endl;
        return false;
    }

    return true;
}

static bool receive_packet(Media* media) {
    return avcodec_receive_packet(media->encoder_context, media->encoder_packet) >= 0;
}

static bool fill_output_frame(Media* media, bool forced = false) {
    if (!forced) {
        int size = av_audio_fifo_size(media->buffer);
        if (size < media->encoder_context->frame_size) return false;
    }

    int read = av_audio_fifo_read(media->buffer, (void**)media->output_frame->data, media->output_frame->nb_samples);

    std::cout << "Read: " << read << std::endl;
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Read: %d", read);

    return read;
}

extern "C"
JNIEXPORT jlong JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_nativeInitialize(JNIEnv *env, jobject thiz, jstring input_file,
                                             jstring output_file) {
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Starting library initialization...");
    jboolean isCopy = JNI_TRUE;
    auto media = open_input_file(env->GetStringUTFChars(input_file, &isCopy));
    if (!media) return -1;

    if (!open_output_file(media, env->GetStringUTFChars(output_file, &isCopy))) {
        delete media;
        return -1;
    }

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Successfully initialized the library!");

    return reinterpret_cast<jlong>(media);
}

int frames_written = 0;

extern "C"
JNIEXPORT jboolean JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_nativeConvert(JNIEnv *env, jobject thiz, jlong media_id) {
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Coming back to start the native conversion");
    auto* media = reinterpret_cast<Media*>(media_id);
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "This didn't trigger an exception! What did then?");
    if (!media) return false;

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Starting work now...");
    while (av_read_frame(media->input_format_context, media->decoder_packet) >= 0) {
        if (media->decoder_packet->stream_index != media->audio_stream_index) continue;
        if (send_packet(media)) {
            while (receive_frame(media)) {
                // Send to the encoder
                // Fill output frame with data
                if (!fill_output_frame(media)) {
                    continue;
                } else {
                    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Filled output frame with data");
                }

                if (send_frame(media)) {
                    while (receive_packet(media)) {
                        std::cout << "Received packet" << std::endl;
                        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Received packet");
                        if (write_frame(media)) {
                            __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Wrote frame %d of %lld", ++frames_written, media->input_stream->nb_frames);
                        } else {
                            std::cout << "Could not write frame!" << std::endl;
                            __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not write frame!");
                        }
                    }
                } else {
                    std::cout << "Not able to send frame to encoder!" << std::endl;
                    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Not able to send frame to encoder!");
                }
            }
        }

        av_packet_unref(media->decoder_packet);
    }

    // Drain the buffer
    while (fill_output_frame(media, true) > 0) {
        if (send_frame(media)) {
            while (receive_packet(media)) {
                std::cout << "Received final packet(s)" << std::endl;
                __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Received final packet(s)");
                if (write_frame(media)) {
                    std::cout << "Wrote final frame(s)!" << std::endl;
                    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Wrote final frame(s)!");
                } else {
                    std::cout << "Could not write final frame!" << std::endl;
                    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not write final frame!");
                }
            }
        } else {
            std::cout << "Not able to send frame to encoder!" << std::endl;
            __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Not able to send frame to encoder!");
        }
    }

    if (close_output_file(media)) {
        std::cout << "Output file closed!" << std::endl;
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Output file closed!");
    }

    if (close_input_file(media)) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Closed input file");
    }

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Deleting media...");
    delete media;

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Deleted media");

    return true;
}

static jclass create_java_class(JNIEnv* env, const std::string& fullQualifiedName) {
    return env->FindClass(fullQualifiedName.c_str());
}

static std::map<std::string, std::string> get_metadata_list(std::string filePath) {
    std::map<std::string, std::string> metadatas;

    AVFormatContext* formatContext = nullptr;

    if (avformat_open_input(&formatContext, filePath.c_str(), NULL, NULL) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Not able to open input file");
        return metadatas;
    }

    if (avformat_find_stream_info(formatContext, NULL) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to find information about this input stream");
        return metadatas;
    }

    AVDictionaryEntry* tag = nullptr;
    while ((tag = av_dict_get(formatContext->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        metadatas[std::string(tag->key)] = std::string(tag->value);
    }

    avformat_close_input(&formatContext);

    return metadatas;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_getAllMetadata(JNIEnv *env, jobject thiz, jstring path) {

    jclass hashMapClass = create_java_class(env, "java/util/HashMap");
    jmethodID init = env->GetMethodID(hashMapClass, "<init>", "()V");

    jobject hashMap = env->NewObject(hashMapClass, init);

    jmethodID putMethodID = env->GetMethodID(hashMapClass, "put", "(Ljava/lang/object;Ljava/lang/object;)Ljava/lang/object;");
    jboolean isCopy;

    auto metadata_list = get_metadata_list(std::string(env->GetStringUTFChars(path, &isCopy)));

    std::for_each(metadata_list.begin(), metadata_list.end(), [&](const std::pair<std::string, std::string>& pair) {
        jstring key_java = env->NewStringUTF(pair.first.c_str());
        jstring value_java = env->NewStringUTF(pair.second.c_str());

        env->CallObjectMethod(hashMap, putMethodID, key_java, value_java);

        env->DeleteLocalRef(key_java);
        env->DeleteLocalRef(value_java);
    });

    jobject hashMapGlobal = env->NewGlobalRef(hashMap);
    env->DeleteLocalRef(hashMap);
    env->DeleteLocalRef(hashMapClass);

    return hashMapGlobal;
}