idf_component_register(
    SRCS "ili9341.c" "font.c" "video_show.c" "video_data.c"
         "i2s_sound.c" "audio_data.c" "main.c"
    INCLUDE_DIRS "."
    # 视频帧数据、音频 PCM 都以二进制文件直接嵌进固件。
    # 符号名分别是 _binary_video_data_bin_start / _binary_audio_data_bin_start。
    # 必须用 EMBED_FILES，不能用 EMBED_TXTFILES —— 后者会在末尾多补一个 0 字节。
    EMBED_FILES "video_data.bin" "audio_data.bin"
)
