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
#include <memory>
#include <unistd.h>

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
    int percentage = 0;
};

static Media* open_input_file(const char* url) {
    AVFormatContext* context = nullptr;
    if (avformat_open_input(&context, url, nullptr, nullptr) < 0)
        return nullptr;

    if (avformat_find_stream_info(context, nullptr) < 0)
        return nullptr;

    // Find the audio stream here
    int audio_stream_index = av_find_best_stream(context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (audio_stream_index < 0) {
        av_free(context);
        return nullptr;
    }

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
    encoder_context->time_base = media->decoder_context->time_base;

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
//    avformat_close_input(&media->input_format_context);
    av_free(media->input_format_context);
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

    av_packet_free(&media->decoder_packet);
    av_packet_free(&media->encoder_packet);
    av_frame_free(&media->output_frame);
    av_frame_free(&media->frame);

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
Java_tech_smallwonder_mp3fy_MP3fy_initializeNative(JNIEnv *env, jobject thiz, jstring input_file,
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

extern "C"
JNIEXPORT jboolean JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_convertNative(JNIEnv *env, jobject thiz, jlong media_id) {
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Coming back to start the native conversion");
    auto* media = reinterpret_cast<Media*>(media_id);
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
                }

                if (send_frame(media)) {
                    while (receive_packet(media)) {
                        if (write_frame(media)) {
                            if (media->decoder_packet->pts != AV_NOPTS_VALUE) {
                                auto time_in_seconds = media->decoder_packet->pts * ((double)media->input_stream->time_base.num / media->input_stream->time_base.den);
                                media->percentage = (time_in_seconds / (media->input_format_context->duration / AV_TIME_BASE)) * 100;
                                __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Wrote frame %d%s", media->percentage, "%");
                            }
                        }
                    }
                }
            }
        }

        av_packet_unref(media->decoder_packet);
    }

    // Drain the buffer
    while (fill_output_frame(media, true) > 0) {
        if (send_frame(media)) {
            while (receive_packet(media)) {
                if (write_frame(media)) {} else {}
            }
        }
    }

    close_output_file(media);

    close_input_file(media);

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Deleting media...");
    delete media;
    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Deleted media");

    return true;
}

static AVFormatContext* create_context_and_parse_header(const char* url) {
    AVFormatContext* formatContext = nullptr;

    if (avformat_open_input(&formatContext, url, nullptr, nullptr) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Not able to open input file");
        return nullptr;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy",
                            "Unable to find information about this input stream");
        return nullptr;
    }

    return formatContext;
}


static jclass create_java_class(JNIEnv* env, const std::string& fullQualifiedName) {
    return env->FindClass(fullQualifiedName.c_str());
}

static std::map<std::string, std::string> get_all_metadata(AVFormatContext* format_context) {
    std::map<std::string, std::string> metadatas;

    AVDictionaryEntry* tag = nullptr;
    while ((tag = av_dict_get(format_context->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        metadatas[std::string(tag->key)] = std::string(tag->value);

        __android_log_print(ANDROID_LOG_INFO, "MP3Fy",
                            "[Key: %s, Value: %s]", tag->key, tag->value);
    }

    return metadatas;
}

/**
 * Gets the album art from this format context and returns it as a shared pointer (so that the memory doesn't have to be freed by anybody when it goes out of scope)
 * @param format_context The format context associated with the audio file to extract its album art
 * @param size The size of the returned album art.
 * @return
 */
static std::shared_ptr<signed char> get_album_art(AVFormatContext* format_context, int* size) {
    for (int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            AVPacket packet = format_context->streams[i]->attached_pic;
            *size = packet.size;
            std::shared_ptr<signed char> art_ptr(new signed char[packet.size]);
            std::memcpy(art_ptr.get(), packet.data, packet.size);
            return art_ptr;
        }
    }

    return std::shared_ptr<signed char>(nullptr);
}

static std::map<std::string, std::string> get_metadata_list(const std::string& filePath) {
    std::map<std::string, std::string> metadatas;

    auto formatContext = create_context_and_parse_header(filePath.c_str());

    if (!formatContext) {
        return metadatas;
    }

    metadatas = get_all_metadata(formatContext);

//    avformat_close_input(&formatContext);
    av_free(formatContext);

    return metadatas;
}

static jobject get_jni_metadatas(JNIEnv* env, AVFormatContext* context) {
    jclass hashMapClass = create_java_class(env, "java/util/HashMap");
    jmethodID init = env->GetMethodID(hashMapClass, "<init>", "()V");

    jobject hashMap = env->NewObject(hashMapClass, init);

    jmethodID putMethodID = env->GetMethodID(hashMapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    jboolean isCopy;

    auto metadata_list = get_all_metadata(context);

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

static jobject get_jni_bitmap(JNIEnv* env, AVFormatContext* format_context) {
    int size;
    auto album_art = get_album_art(format_context, &size);

    if (album_art != nullptr) {

        jclass bitmap_factory_class = create_java_class(env, "android/graphics/BitmapFactory");
        jmethodID decode_bitmap_method_id = env->GetStaticMethodID(bitmap_factory_class, "decodeByteArray", "([BII)Landroid/graphics/Bitmap;");

        // Create the new byte array
        auto byte_array = env->NewByteArray(size);
        env->SetByteArrayRegion(byte_array, 0, size, album_art.get());

        jobject bitmap = env->CallStaticObjectMethod(bitmap_factory_class, decode_bitmap_method_id, byte_array, 0, size);

        jobject final_bitmap = env->NewGlobalRef(bitmap);

        env->DeleteLocalRef(bitmap_factory_class);

        av_free(format_context);

        return final_bitmap;
    }

    return nullptr;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_getAllMetadataNative(JNIEnv *env, jobject thiz, jstring path) {

    jclass hashMapClass = create_java_class(env, "java/util/HashMap");
    jmethodID init = env->GetMethodID(hashMapClass, "<init>", "()V");

    jobject hashMap = env->NewObject(hashMapClass, init);

    jmethodID putMethodID = env->GetMethodID(hashMapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
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

extern "C"
JNIEXPORT jobject JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_getAudioFileInfoNative(JNIEnv *env, jobject thiz, jstring path) {
    jclass audio_file_info_class = create_java_class(env, "tech/smallwonder/mp3fy/AudioFileInfo");
    jmethodID init = env->GetMethodID(audio_file_info_class, "<init>", "()V");
    jobject audio_file_info = env->NewObject(audio_file_info_class, init);

    jfieldID metadata_list_field = env->GetFieldID(audio_file_info_class, "metadataList", "Ljava/util/HashMap;");
    jfieldID bitrate_field = env->GetFieldID(audio_file_info_class, "bitrate", "I");
    jfieldID duration_field = env->GetFieldID(audio_file_info_class, "duration", "J");
    jfieldID bitmap_field = env->GetFieldID(audio_file_info_class, "albumArt", "Landroid/graphics/Bitmap;");

    jboolean isCopy = JNI_FALSE;

    auto formatContext = create_context_and_parse_header(env->GetStringUTFChars(path, &isCopy));

    if (!formatContext) {
        return nullptr;
    }

    env->SetIntField(audio_file_info, bitrate_field, formatContext->bit_rate);
    env->SetLongField(audio_file_info, duration_field, formatContext->duration);
    env->SetObjectField(audio_file_info, metadata_list_field, get_jni_metadatas(env, formatContext));
    env->SetObjectField(audio_file_info, bitmap_field, get_jni_bitmap(env, formatContext));

    jobject audio_file_info_global = env->NewGlobalRef(audio_file_info);
    env->DeleteLocalRef(audio_file_info_class);
    env->DeleteLocalRef(audio_file_info);

    return audio_file_info_global;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_getAlbumArtNative(JNIEnv *env, jobject thiz, jstring path) {
    jboolean isCopy = JNI_FALSE;
    auto formatContext = create_context_and_parse_header(env->GetStringUTFChars(path, &isCopy));

    if (!formatContext) {
        return nullptr;
    }

    return get_jni_bitmap(env, formatContext);
}extern "C"
JNIEXPORT jint JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_getPercentageNative(JNIEnv *env, jobject thiz, jlong media_id) {
    auto* media = reinterpret_cast<Media*>(media_id);
    if (!media) return -1;

    return media->percentage;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_editMetadataInformationNative(JNIEnv *env, jobject thiz,
                                                                jstring input_file,
                                                                jobjectArray keys,
                                                                jobjectArray values,
                                                                jint length,
                                                                jbyteArray album_art,
                                                                jint album_art_len,
                                                                jint width,
                                                                jint height,
                                                                jstring output_file) {
    jboolean isCopy = JNI_FALSE;

    std::map<std::string, std::string> metadatas;

    // Copy all the metadatas
    for (jsize i = 0; i < length; i++) {
        jobject key = env->GetObjectArrayElement(keys, i);
        jobject value = env->GetObjectArrayElement(values, i);
        auto key_str = env->GetStringUTFChars((jstring)key, &isCopy);
        auto value_str = env->GetStringUTFChars((jstring)value, &isCopy);
        metadatas[key_str] = value_str;
    }

    AVFormatContext* context = nullptr;
    auto input_file_path = env->GetStringUTFChars(input_file, &isCopy);
    auto output_file_path = env->GetStringUTFChars(output_file, &isCopy);
    if (avformat_open_input(&context, input_file_path, nullptr, nullptr) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to open input file");
        return JNI_FALSE;
    }

    if (avformat_find_stream_info(context, nullptr) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "No stream information found!");
        return JNI_FALSE;
    }

    // Find the audio stream here
    int audio_stream_index = av_find_best_stream(context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (audio_stream_index < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to find audio stream index");
        av_free(context);
        return JNI_FALSE;
    }

    AVStream* audio_stream = context->streams[audio_stream_index];

    AVStream* output_stream;
    AVFormatContext* output_format_context;

    if (int ret = avformat_alloc_output_context2(&output_format_context, nullptr, nullptr, output_file_path) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to allocate output context");
        return JNI_FALSE;
    }

    // Find the decoder
    AVCodec* decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!decoder) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "No decoder found for this audio file");
        avformat_close_input(&context);
        return JNI_FALSE;
    }
    AVCodecContext* decoder_context = avcodec_alloc_context3(decoder);
    if (!decoder_context) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to create decoder context");
        av_free(context);
//        avformat_close_input(&context);
        return JNI_FALSE;
    }

    // Copy the codec parameters to the decoder context
    if (avcodec_parameters_to_context(decoder_context, audio_stream->codecpar) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to copy codec parameters to context");
        return JNI_FALSE;
    }

    // Open the codec
    if (avcodec_open2(decoder_context, decoder, nullptr) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to open decoder");
        return JNI_FALSE;
    }

    output_stream = avformat_new_stream(output_format_context, nullptr);
    AVStream *album_art_stream = nullptr;

    AVPacket* packet2;
    AVFrame *frame;
    int attached_pic_stream_index = -1;

    for (int i = 0; i < context->nb_streams; i++) {
        if (context->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            attached_pic_stream_index = i;
            break;
        }
    }

    // Ascertain that we have to add this stream
    if (attached_pic_stream_index != -1 || album_art_len != 0) {
        album_art_stream = avformat_new_stream(output_format_context, nullptr);
        album_art_stream->disposition = AV_DISPOSITION_ATTACHED_PIC;
    }

    if (album_art_len != 0) attached_pic_stream_index = album_art_stream->index;

    // Conditional Start ==
    if (album_art_len != 0 || attached_pic_stream_index != -1) {
        album_art_stream->codecpar->format = AV_PIX_FMT_YUVJ440P;
        album_art_stream->codecpar->width = width;
        album_art_stream->codecpar->height = height;
        album_art_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        album_art_stream->codecpar->codec_id = AV_CODEC_ID_MJPEG;
        album_art_stream->codecpar->codec_tag = 0;
        if (album_art_len != 0) {
            packet2 = av_packet_alloc();
            auto art_ptr = (uint8_t*) env->GetByteArrayElements(album_art, &isCopy);
            packet2->data = art_ptr;
            packet2->size = album_art_len;
            auto dataz = (uint8_t*) av_malloc(album_art_len);
            if (!dataz) {
                return JNI_FALSE;
            }
            memcpy(dataz, art_ptr, album_art_len);
            av_packet_from_data(packet2, dataz, album_art_len);
        } else {
            __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Using already present album art");
            if (avcodec_parameters_copy(album_art_stream->codecpar, context->streams[attached_pic_stream_index]->codecpar) < 0) {
                __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Cannot copy codec parameters");
            }
        }
    }
    // Conditional end ==

    if (avcodec_parameters_copy(output_stream->codecpar, audio_stream->codecpar) < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Copying the codec could've caused some problems. PS: Copying input stream parameters to output stream parameters");
    }

    // COPY THE METADATAS TO THIS PLACE!
    std::for_each(metadatas.begin(), metadatas.end(), [&](std::pair<std::string, std::string> pairs) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Setting metadata: (%s, %s)", pairs.first.c_str(), pairs.second.c_str());
        av_dict_set(&output_format_context->metadata, pairs.first.c_str(), pairs.second.c_str(), 0);
    });

    output_stream->time_base = audio_stream->time_base;
    int ret;
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_format_context->pb, output_file_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Unable to get access to the output file!");
            return JNI_FALSE;
        }
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Opened output file");
    }

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Writing header...");
    ret = avformat_write_header(output_format_context, nullptr);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Could not write header! Error: %s", av_err2str(ret));
        return JNI_FALSE;
    }

    AVPacket* packet = av_packet_alloc();

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Writing frames...");

    bool wrote_album_art = false;

    while (av_read_frame(context, packet) >= 0) {
        if (packet->stream_index == output_stream->index) {
            av_interleaved_write_frame(output_format_context, packet);
            av_packet_unref(packet);
        } else {
            __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Got to the album art section");
            if (album_art_len != 0) {
                // We can only write one album art
                if (wrote_album_art) {
                    av_packet_unref(packet);
                    continue;
                }
                if (packet->stream_index == attached_pic_stream_index) {
                    packet2->stream_index = attached_pic_stream_index;
                    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "This is the index of the packet we're supposed to write");
                    av_interleaved_write_frame(output_format_context, packet2);
                    av_packet_unref(packet2);
                    av_packet_unref(packet);
                    wrote_album_art = true;
                }
                __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Wrote the custom album art packet");
            } else {
                // We can only write one album art
                if (wrote_album_art) {
                    av_packet_unref(packet);
                    continue;
                }
                av_interleaved_write_frame(output_format_context, packet);
                av_packet_unref(packet);
                wrote_album_art = true;
                __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Wrote the normal packet");
            }
        }
    }

    if (album_art_len != 0 && !wrote_album_art) {
        packet2->stream_index = attached_pic_stream_index;
        av_interleaved_write_frame(output_format_context, packet2);
        av_packet_unref(packet2);
    }

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Finishing up...");
    av_write_trailer(output_format_context);
    avformat_free_context(output_format_context);
    av_packet_free(&packet);
    if (album_art_len != 0) av_packet_free(&packet2);
    av_free(context);

    __android_log_print(ANDROID_LOG_INFO, "MP3Fy", "Finished up");
    return JNI_TRUE;
}

extern "C"
JNIEXPORT void JNICALL
Java_tech_smallwonder_mp3fy_MP3fy_pipeStdErrToLogcatNative(JNIEnv *env, jobject thiz) {
    int pipes[2];
    pipe(pipes);
    dup2(pipes[1], STDERR_FILENO);
    close(pipes[1]);
    FILE* inputFile = fdopen(pipes[0], "r");
    char readBuffer[256];
    while (1) {
        fgets(readBuffer, sizeof(readBuffer), inputFile);
        __android_log_write(ANDROID_LOG_ERROR, "MP3fy_Err", readBuffer);
    }
}