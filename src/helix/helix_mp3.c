#include "helix_mp3.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_heap_caps.h"

#ifndef ALWAYS_INLINE
#define ALWAYS_INLINE inline __attribute__((always_inline))
#endif

#define HELIX_MP3_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define HELIX_MP3_SAMPLES_PER_FRAME 2

static int helix_mp3_skip_id3v2_tag(helix_mp3_t *mp3)
{
    const size_t id3v2_frame_header_size = 10;
    const size_t id3v2_frame_offset = 0;
    const size_t id3v2_frame_magic_string_length = 3;
    const char *id3v2_frame_magic_string = "ID3";

    uint8_t frame_buffer[id3v2_frame_header_size];

    /* Seek to the beginning of the frame and read frame's header */
    if (mp3->io->seek(mp3->io->user_data, id3v2_frame_offset) != 0) {
        return -EIO;
    }
    if (mp3->io->read(mp3->io->user_data, frame_buffer, id3v2_frame_header_size) != id3v2_frame_header_size) {
        return -EIO;
    }

    /* Check magic */
    if (strncmp((const char *)frame_buffer, id3v2_frame_magic_string, id3v2_frame_magic_string_length) != 0) {
        mp3->io->seek(mp3->io->user_data, id3v2_frame_offset);
        return 0;
    }

    /* The tag size (minus the 10-byte header) is encoded into four bytes,
     * but the most significant bit needs to be masked in each byte. */
    const size_t id3v2_tag_total_size = (((frame_buffer[6] & 0x7F) << 21) | ((frame_buffer[7] & 0x7F) << 14) |
                                        ((frame_buffer[8] & 0x7F) << 7) | ((frame_buffer[9] & 0x7F) << 0)) +
                                        id3v2_frame_header_size;

    /* Skip the tag */
    if (mp3->io->seek(mp3->io->user_data, id3v2_frame_offset + id3v2_tag_total_size) != 0) {
        return -EIO;
    }
    return id3v2_tag_total_size;
}

static size_t helix_mp3_fill_mp3_buffer(helix_mp3_t *mp3)
{
    /* Move remaining data to the beginning of the buffer */
    memmove(&mp3->mp3_buffer[0], mp3->mp3_read_ptr, mp3->mp3_buffer_bytes_left);

    /* Read new data */
    const size_t bytes_to_read = HELIX_MP3_DATA_CHUNK_SIZE - mp3->mp3_buffer_bytes_left;
    const size_t bytes_read = mp3->io->read(mp3->io->user_data, &mp3->mp3_buffer[mp3->mp3_buffer_bytes_left], sizeof(*mp3->mp3_buffer) * bytes_to_read);

    /* Zero-pad to avoid finding false sync word from old data */
    if (bytes_read < bytes_to_read) {
        memset(&mp3->mp3_buffer[mp3->mp3_buffer_bytes_left + bytes_read], 0, bytes_to_read - bytes_read);
    }

    return bytes_read;
}

static ALWAYS_INLINE void helix_mp3_convert_pcm_mono_to_stereo(helix_mp3_t *mp3)
{
    // Fast 32-bit in-place register packing (Halves bus transactions)
    uint32_t *dst32 = (uint32_t *)mp3->pcm_buffer;
    int16_t *src16  = mp3->pcm_buffer;

    for (int32_t i = mp3->pcm_samples_left - 1; i >= 0; --i) {
        uint32_t sample = (uint32_t)(uint16_t)src16[i];
        dst32[i] = (sample << 16) | sample;
    }
    mp3->pcm_samples_left *= 2;
}

static size_t helix_mp3_decode_next_frame(helix_mp3_t *mp3)
{
    size_t pcm_samples_read;

    while (1) {
        if (mp3->mp3_buffer_bytes_left < HELIX_MP3_MIN_DATA_CHUNK_SIZE) {
            const size_t bytes_read = helix_mp3_fill_mp3_buffer(mp3);
            mp3->mp3_buffer_bytes_left += bytes_read;
            mp3->mp3_read_ptr = &mp3->mp3_buffer[0];
        }

        const int offset = MP3FindSyncWord(mp3->mp3_read_ptr, mp3->mp3_buffer_bytes_left);
        if (offset < 0) {
            pcm_samples_read = 0;
            break; // Out of data
        }
        mp3->mp3_read_ptr += offset;
        mp3->mp3_buffer_bytes_left -= offset;

        const int err = MP3Decode(mp3->dec, &mp3->mp3_read_ptr, &mp3->mp3_buffer_bytes_left, mp3->pcm_buffer, 0);
        if (err == ERR_MP3_NONE) {
            MP3FrameInfo frame_info;
            MP3GetLastFrameInfo(mp3->dec, &frame_info);

            mp3->current_sample_rate = frame_info.samprate;
            mp3->current_bitrate = frame_info.bitrate;
            mp3->pcm_samples_left = frame_info.outputSamps;
            if (frame_info.nChans == 1) {
                helix_mp3_convert_pcm_mono_to_stereo(mp3); // Output data always in 2-channel format
            }

            pcm_samples_read = mp3->pcm_samples_left;
            break;
        }
        else if (err == ERR_MP3_MAINDATA_UNDERFLOW) {
            continue; // Get more data from file
        }
        else {
            pcm_samples_read = 0;
            break; // Out of data
        }
    }

    return pcm_samples_read;
}

static int helix_mp3_seek(void *user_data, int offset)
{
    return fseek((FILE *)user_data, offset, SEEK_SET);
}

static size_t helix_mp3_read(void *user_data, void *buffer, size_t size)
{
    return fread(buffer, sizeof(uint8_t), size, (FILE *)user_data);
}

static helix_mp3_io_t default_io =
{
    .seek = helix_mp3_seek,
    .read = helix_mp3_read
};

int helix_mp3_init(helix_mp3_t *mp3, const helix_mp3_io_t *io)
{
    /* Sanity check */
    if ((mp3 == NULL) || (io == NULL)) {
        return -EINVAL;
    }

    /* Initialize decoder context */
    memset(mp3, 0, sizeof(*mp3));
    mp3->io = io;

    int err = 0;
    do {
        /* Initialize decoder */
        mp3->dec = MP3InitDecoder();
        if (mp3->dec == NULL) {
            err = -ENOMEM;
            break;
        }
        
        /* Initialize 16-byte aligned buffers in ultra-fast INTERNAL SRAM */
        mp3->mp3_buffer = (uint8_t *)heap_caps_aligned_alloc(16, HELIX_MP3_DATA_CHUNK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (mp3->mp3_buffer == NULL) {
            err = -ENOMEM;
            break;
        }
        mp3->pcm_buffer = (int16_t *)heap_caps_aligned_alloc(16, HELIX_MP3_MAX_SAMPLES_PER_FRAME * sizeof(*mp3->pcm_buffer), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (mp3->pcm_buffer == NULL) {
            err = -ENOMEM;
            break;
        }

        /* Skip ID3V2 tag */
        if (helix_mp3_skip_id3v2_tag(mp3) < 0) {
            err = -EIO;
            break;
        }

        /* Decode first frame */
        if (helix_mp3_decode_next_frame(mp3) == 0) {
            err = -ENOTSUP;
            break;
        }
    } while (0);

    if (err) {
        if (mp3->pcm_buffer) heap_caps_free(mp3->pcm_buffer);
        if (mp3->mp3_buffer) heap_caps_free(mp3->mp3_buffer);
        if (mp3->dec)        MP3FreeDecoder(mp3->dec);
    }
    return err;  
}

int helix_mp3_init_file(helix_mp3_t *mp3, const char *path)
{
    /* Open input file */
    FILE *fd = fopen(path, "rb");
    if (fd == NULL) {
       return -ENOENT;
    }
    default_io.user_data = fd;

    /* Initialize decoder */
    const int err = helix_mp3_init(mp3, &default_io);
    if (err) {
        fclose(fd);
        return err;
    }
    return 0;
}

int helix_mp3_deinit(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return -EINVAL;
    }

    if (mp3->io->read == default_io.read) {
        fclose((FILE *)mp3->io->user_data);
    }
    if (mp3->pcm_buffer) heap_caps_free(mp3->pcm_buffer);
    if (mp3->mp3_buffer) heap_caps_free(mp3->mp3_buffer);
    if (mp3->dec)        MP3FreeDecoder(mp3->dec);

    return 0;
}

uint32_t helix_mp3_get_sample_rate(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return 0;
    }
    return mp3->current_sample_rate;
}

uint32_t helix_mp3_get_bitrate(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return 0;
    }
    return mp3->current_bitrate;
}

size_t helix_mp3_get_pcm_frames_decoded(helix_mp3_t *mp3)
{
    if (mp3 == NULL) {
        return 0;
    }
    return mp3->current_pcm_frame;
}

size_t helix_mp3_read_pcm_frames_s16(helix_mp3_t *mp3, int16_t *buffer, size_t frames_to_read)
{
    if ((mp3 == NULL) || (buffer == NULL) || (frames_to_read == 0)) {
        return 0;
    }

    size_t samples_to_read = frames_to_read * HELIX_MP3_SAMPLES_PER_FRAME;
    size_t samples_read = 0;

    while (1) {
        const size_t samples_to_consume = HELIX_MP3_MIN(mp3->pcm_samples_left, samples_to_read);
        
        /* Get samples from in-memory PCM buffer */
        memcpy(&buffer[samples_read], &mp3->pcm_buffer[HELIX_MP3_MAX_SAMPLES_PER_FRAME - mp3->pcm_samples_left], samples_to_consume * sizeof(*mp3->pcm_buffer));

        mp3->current_pcm_frame += (samples_to_consume / HELIX_MP3_SAMPLES_PER_FRAME);
        mp3->pcm_samples_left -= samples_to_consume;
        samples_read += samples_to_consume;
        samples_to_read -= samples_to_consume;

        /* In-memory PCM buffer is fully used up, decode next frame */
        if (mp3->pcm_samples_left == 0) {
            if (helix_mp3_decode_next_frame(mp3) == 0) {
                break;
            }
        }

        /* Job done */
        if (samples_to_read == 0) {
            break;
        } 
    }

    return (samples_read / HELIX_MP3_SAMPLES_PER_FRAME);
}