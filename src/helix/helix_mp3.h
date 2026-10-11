#pragma once

#include "mp3dec.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Minimum data chunk size containing at least one MP3 frame. Make input buffer twice as big. */
#define HELIX_MP3_MIN_DATA_CHUNK_SIZE 8192
#define HELIX_MP3_DATA_CHUNK_SIZE (HELIX_MP3_MIN_DATA_CHUNK_SIZE * 2)

/* Maximum number of samples that can be extracted from one MP3 frame */
#define HELIX_MP3_MAX_SAMPLES_PER_FRAME (MAX_NCHAN * MAX_NGRAN * MAX_NSAMP)

/** @struct helix_mp3_io_t
 *  I/O interface for custom stream handling.
 *
 *  @var helix_mp3_io_t::seek
 *      Pointer to custom seek function. Offset will always be
 *      given from the beginning of the file. The function
 *      should return 0 on success and non-zero value on failure.
 *  @var helix_mp3_io_t::read
 *      Pointer to custom read function. The function should
 *      return number of bytes read (can be zero) on success
 *      and zero on failure.
 *  @var helix_mp3_io_t::user_data
 *      Pointer to argument that will be passed while invoking
 *      seek or read. Usually pointer to file descriptor. Can
 *      be NULL if not needed.
 */
typedef struct
{
    int (*seek)(void *user_data, int offset);
    size_t (*read)(void *user_data, void *buffer, size_t size);
    void *user_data;
} helix_mp3_io_t;

/** @struct helix_mp3_t
 *  Struct storing internal context of the decoder.
 *  Do not access these fields directly.
 *
 */
typedef struct 
{
    HMP3Decoder dec;
    uint8_t *mp3_buffer;
    uint8_t *mp3_read_ptr;
    int mp3_buffer_bytes_left;
    int16_t *pcm_buffer;
    size_t pcm_samples_left;
    size_t current_pcm_frame;
    uint32_t current_sample_rate;
    uint32_t current_bitrate;
    const helix_mp3_io_t *io;
} helix_mp3_t;


/* Public API */

/**
 * @brief Initializes the decoder using custom I/O callbacks
 *
 * Proper opening and closing of the stream should be done
 * by the user if such operations are required. The stream
 * should be readable upon calling this function.
 *
 * @param mp3 pointer to decoder context
 * @param io pointer to I/O struct filled with valid I/O function pointers
 * @return int appropriate errno code on failure, zero on success
 */
int helix_mp3_init(helix_mp3_t *mp3, const helix_mp3_io_t *io);

/**
 * @brief Initializes the decoder for a given file
 * 
 * @param mp3 pointer to decoder context
 * @param path path to MP3 file to decode
 * @return int appropriate errno code on failure, zero on success
 */
int helix_mp3_init_file(helix_mp3_t *mp3, const char *path);

/**
 * @brief Deinitializes the decoder, freeing all internal resources
 * 
 * @param mp3 pointer to decoder context
 * @return int appropriate errno code on failure, zero on success
 */
int helix_mp3_deinit(helix_mp3_t *mp3);

/**
 * @brief Returns sample rate of the last decoded frame
 * 
 * @param mp3 pointer to decoder context
 * @return uint32_t sample rate value in Hz
 */
uint32_t helix_mp3_get_sample_rate(helix_mp3_t *mp3);

/**
 * @brief Returns bitrate of the last decoded frame
 * 
 * @param mp3 pointer to decoder context
 * @return uint32_t bitrate value in bps
 */
uint32_t helix_mp3_get_bitrate(helix_mp3_t *mp3);

/**
 * @brief Returns number of PCM frames decoded so far
 * 
 * @param mp3 pointer to decoder context
 * @return size_t number of decoded PCM frames
 */
size_t helix_mp3_get_pcm_frames_decoded(helix_mp3_t *mp3);

/**
 * @brief Decodes and reads requested number of PCM frames into the buffer
 * 
 * Keep in mind that this function operates on frames, not samples. Frame is just
 * a number of samples times number of channels. This library always returns 
 * decoded data in 2-channel format, for mono MP3 files both channels just contain 
 * the same data.
 * That means that the provided buffer always has to have size of at least
 * frames_to_read * 2 channels * sizeof(int16_t). Otherwise bad things will happen.
 * 
 * @param mp3 pointer to decoder context
 * @param buffer pointer to buffer big enough to store requested number of PCM frames
 * @param frames_to_read number of PCM frames to read
 * @return size_t number of frames written to buffer (can be less than frames_to_read if e.g. EOF reached)
 */
size_t helix_mp3_read_pcm_frames_s16(helix_mp3_t *mp3, int16_t *buffer, size_t frames_to_read);
