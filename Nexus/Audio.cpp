#include "RetroEngine.hpp"
#include <cmath>
#include <iostream>

int globalSFXCount = 0;
int stageSFXCount  = 0;

int masterVolume  = MAX_VOLUME;
int trackID       = -1;
int sfxVolume     = MAX_VOLUME;
int bgmVolume     = MAX_VOLUME;
bool audioEnabled = false;

int nextChannelPos;
bool musicEnabled;
int musicStatus;
TrackInfo musicTracks[TRACK_COUNT];
SFXInfo sfxList[SFX_COUNT];

ChannelInfo sfxChannels[CHANNEL_COUNT];

MusicPlaybackInfo musInfo;

int trackBuffer = -1;

#if !RETRO_USING_SDL2 && !RETRO_USING_SDL1
// from https://github.com/cpuimage/resampler, by Zhihan Gao
uint64_t Resample_s16(const int16_t *input, int16_t *output, int inSampleRate, int outSampleRate, uint64_t inputSize,
                      uint32_t channels
) {
    uint64_t outputSize = (uint64_t) (inputSize * (double) outSampleRate / (double) inSampleRate);
    outputSize -= outputSize % channels;
    if (output == NULL)
        return outputSize;
    if (input == NULL)
        return 0;
    double stepDist = ((double) inSampleRate / (double) outSampleRate);
    const uint64_t fixedFraction = (1LL << 32);
    const double normFixed = (1.0 / (1LL << 32));
    uint64_t step = ((uint64_t) (stepDist * fixedFraction + 0.5));
    uint64_t curOffset = 0;
    for (uint32_t i = 0; i < outputSize; i += 1) {
        for (uint32_t c = 0; c < channels; c += 1) {
            *output++ = (int16_t) (input[c] + (input[c + channels] - input[c]) * (
                    (double) (curOffset >> 32) + ((curOffset & (fixedFraction - 1)) * normFixed)
            )
            );
        }
        curOffset += step;
        input += (curOffset >> 32) * channels;
        curOffset &= (fixedFraction - 1);
    }
    return outputSize;
}

#define ADJUST_VOLUME(s, v) (s = (s * v) / MAX_VOLUME)

static Sint16* WavDataToBuffer(void* data, int num_frames, int num_channels,
    int bit_depth) 
{
    int outSz = num_frames * 2 * 2;
    Sint16* src = (Sint16*)data;
    unsigned char* src8 = (unsigned char*)data;
    Sint16* out = new Sint16[outSz / num_channels];
	
    printf("WavDataToBuffer(num_frames=%d, num_channels=%d, bit_depth=%d)\n",
		num_frames, num_channels, bit_depth);

    if (num_channels == 2 && bit_depth == 16) {
        memcpy(out, src, outSz);
    } else if (num_channels == 1 && bit_depth == 16) {
        for (int x = 0; x < num_frames; x++) {
            out[x * 2] = src[x];
            out[(x * 2) + 1] = src[x];
        }
    } else if (num_channels == 2 && bit_depth == 8) {
        for (int x = 0; x < num_frames; x++)
            out[x] = (src8[x] << 8) ^ 0x8000;
    } else if (num_channels == 1 && bit_depth == 8) {
        for (int x = 0; x < num_frames; x++) {
            out[x * 2] = (src8[x] << 8) ^ 0x8000;
            out[(x * 2) + 1] = (src8[x] << 8) ^ 0x8000;
        }
    }

#ifdef RETRO_BIG_ENDIAN
    if (bit_depth == 16) {
        Uint16 s;
 
        for (int x = 0; x < num_frames; x++) {
            s = out[x*2]; 
            out[x*2] = (s>>8) | (s&0xff)<<8;
            s = out[(x * 2) + 1];
            out[(x * 2) + 1] = (s>>8) | (s&0xff)<<8;
        }
    }
#endif

    return out;
}
		
static unsigned char *musicFifoBuf = NULL;

static int musicFifoPos=0;
static int musicFifoSize=0;
static bool musicFifoOnly = false;

void musicFifoPush(void *data, int len) {
	if ((musicFifoPos + len) > musicFifoSize) {
		musicFifoSize = musicFifoPos + len + 0x4000;
		musicFifoBuf = (unsigned char*)realloc(musicFifoBuf, musicFifoSize);
	}
	
	memcpy(&musicFifoBuf[musicFifoPos], data, len);
	musicFifoPos += len;
}

void musicFifoPop(void *dst, int len) {
	memcpy(dst, musicFifoBuf, len);
	
	for (int x = 0; x < musicFifoPos; x++)
            musicFifoBuf[x] = musicFifoBuf[x+len];
	
	musicFifoPos -= len;
}

void musicFifoReset(void) {
	bzero(musicFifoBuf, musicFifoSize);
	musicFifoPos=0;
	musicFifoOnly=false;
}
		
#endif

#if RETRO_WSSAUDIO
int wssSampleRate;

bool wssaudio_init_device(int rate) {    
    if (!w_sound_device_init(28, rate)) { // 28 = High-Definition Audio
	printLog("Unable to open HDA audio device: %s", w_get_error_message());
    } else {
        printLog("WSS audio HDA init OK.");
	return true;
    }
    
    if (!w_sound_device_init(3, rate)) { // 3 = AC97 auto detected
        printLog("Unable to open AC97 audio device: %s", w_get_error_message());
    } else {
        printLog("WSS audio AC97 init OK.");
        return true;
    }

    if (!w_sound_device_init(1, rate)) { // 1 = Sound Blaster autodetect
        printLog("Unable to open Sound Blaster audio device: %s", w_get_error_message());
    } else {
        printLog("WSS audio Sound Blaster init OK.");
        return true;
    }
    
    return false;
}
#endif

#if RETRO_USING_SDL1 || RETRO_USING_SDL2
SDL_AudioSpec audioDeviceFormat;

#if RETRO_USING_SDL2
SDL_AudioDeviceID audioDevice;
#endif

#define LockAudioDevice()   SDL_LockAudio()
#define UnlockAudioDevice() SDL_UnlockAudio()

#define AUDIO_FREQUENCY (44100)
#define AUDIO_FORMAT    (AUDIO_S16SYS) /**< Signed 16-bit samples */
#define AUDIO_SAMPLES   (0x800)
#define AUDIO_CHANNELS  (2)

#define ADJUST_VOLUME(s, v) (s = (s * v) / MAX_VOLUME)

#else
#define LockAudioDevice()   ;
#define UnlockAudioDevice() ;
#endif



#define MIX_BUFFER_SAMPLES (256)

int InitAudioPlayback()
{
    StopAllSfx(); //"init"
#if RETRO_USING_SDL1 || RETRO_USING_SDL2
    SDL_AudioSpec want;
    want.freq     = AUDIO_FREQUENCY;
    want.format   = AUDIO_FORMAT;
    want.samples  = AUDIO_SAMPLES;
    want.channels = AUDIO_CHANNELS;
    want.callback = ProcessAudioPlayback;

    #if RETRO_USING_SDL2
    if ((audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &audioDeviceFormat, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) > 0) {
        audioEnabled = true;
        SDL_PauseAudioDevice(audioDevice, 0);
    }
    else {
        printLog("Unable to open audio device: %s", SDL_GetError());
        audioEnabled = false;
        return true; // no audio but game wont crash now
    }

#elif RETRO_USING_SDL1
    if (SDL_OpenAudio(&want, &audioDeviceFormat) == 0) {
        audioEnabled = true;
        SDL_PauseAudio(0);
    }
    else {
        printLog("Unable to open audio device: %s", SDL_GetError());
        audioEnabled = false;
        return true; // no audio but game wont crash now
    }
#endif // !RETRO_USING_SDL1

#endif

#if RETRO_USING_ALLEGRO4

#if RETRO_WSSAUDIO
    w_set_device_master_volume(0);

    audioEnabled = wssaudio_init_device(44100);
    
    if (audioEnabled) {
        wssSampleRate = w_get_nominal_sample_rate();
	printLog("Audio sample rate = %d", wssSampleRate); 
    }
    
    sleep(3);
    
    musInfo.stream = new Sint16[AUDIO_SAMPLES * 2];
#elif RETRO_DOSSOUND
    __dpmi_regs i;
    
    bzero(&i, sizeof(__dpmi_regs));

// Check if the interrupt vector for DOSSound is hooked up.
// If it isn't, it means that DOSSound was not installed.

    i.h.ah = 0x35;
    i.h.al = DOSSOUND_INT;

    __dpmi_int(0x21, &i);
    
    audioEnabled = i.x.es || i.x.bx;

    if (!audioEnabled) {
        printLog("Audio not enabled because DOSSound was not installed!");
    } else {
	bzero(&i, sizeof(__dpmi_regs));
    
        i.h.ah = 0x40; // Return Vendor/Device

        __dpmi_int(DOSSOUND_INT, &i);
    
        printLog("Vendor ID = 0x%04x", i.x.bx);
        printLog("Device ID = 0x%04x", i.x.cx);

        i.h.ah = 0x50; // Return segments of internal buffers
    
        __dpmi_int(DOSSOUND_INT, &i);
    
        musInfo.bufAddr = i.x.bx << 4;
        musInfo.bufSize = i.x.cx << 4;
    
        printLog("Buffer address = 0x%05x", musInfo.bufAddr);
        printLog("Size = %d", musInfo.bufSize);

        i.h.ah = 0x03; // Query Status
    
        __dpmi_int(DOSSOUND_INT, &i);
    
        musInfo.curBufNumAddr = (i.x.es << 4) + i.x.di + 11;
        musInfo.sampleRate = 44100;

        printLog("curBufNumAddr address = 0x%05x", musInfo.curBufNumAddr);
    
        i.h.ah = 0x10; // Set Volume
        i.h.bh = 0; // volume for left (max)
        i.h.bl = 0; // volume for right (max)
    
        __dpmi_int(DOSSOUND_INT, &i);
    
        i.h.ah = 0x51; // Play PCM data
        i.x.bx = musInfo.sampleRate;
    
        __dpmi_int(DOSSOUND_INT, &i);
    
       musInfo.stream = new Sint16[musInfo.bufSize / 2];
    }
    
    sleep(3);
#else   
    if (install_sound(DIGI_AUTODETECT, 0, NULL) == -1) {
        printLog("Unable to open audio device: %s", allegro_error);
        audioEnabled = false;
        return true;
    }
    
    musInfo.stream = play_audio_stream(AUDIO_SAMPLES, 16, 1, AUDIO_FREQUENCY, 255, 127);
#endif

    audioEnabled = true;
#endif

    LoadGlobalSfx();

    return true;
}

void LoadGlobalSfx()
{
    FileInfo info;
    FileInfo infoStore;
    char strBuffer[0x100];
    byte fileBuffer = 0;
    int fileBuffer2 = 0;

    if (LoadFile("Data/Game/GameConfig.bin", &info)) {
        infoStore = info;

        FileRead(&fileBuffer, 1);
        FileRead(strBuffer, fileBuffer);
        strBuffer[fileBuffer] = 0;

        FileRead(&fileBuffer, 1);
        FileRead(&strBuffer, fileBuffer); // Load 'Data'
        strBuffer[fileBuffer] = 0;

        FileRead(&fileBuffer, 1);
        FileRead(strBuffer, fileBuffer);
        strBuffer[fileBuffer] = 0;

        // Read Script Paths
        byte scriptCount = 0;
        FileRead(&scriptCount, 1);
        for (byte s = 0; s < scriptCount; ++s) {
            FileRead(&fileBuffer, 1);
            FileRead(strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;
        }

        byte varCnt = 0;
        FileRead(&varCnt, 1);
        for (byte v = 0; v < varCnt; ++v) {
            FileRead(&fileBuffer, 1);
            FileRead(strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;

            // Read Variable Value
            FileRead(&fileBuffer2, 4);
        }

        // Read SFX
        FileRead(&fileBuffer, 1);
        globalSFXCount = fileBuffer;
        for (byte s = 0; s < globalSFXCount; ++s) {
            FileRead(&fileBuffer, 1);
            FileRead(strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;

            GetFileInfo(&infoStore);
            LoadSfx(strBuffer, s);
            SetFileInfo(&infoStore);
        }

        CloseFile();
    }

    // sfxDataPosStage = sfxDataPos;
    nextChannelPos = 0;
    for (int i = 0; i < CHANNEL_COUNT; ++i) sfxChannels[i].sfxID = -1;
}

//#if RETRO_USING_SDL1 || RETRO_USING_SDL2
size_t readVorbis(void *mem, size_t size, size_t nmemb, void *ptr)
{
    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    return FileRead2(&info->fileInfo, mem, (int)(size * nmemb), true);
}
int seekVorbis(void *ptr, ogg_int64_t offset, int whence)
{
    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    switch (whence) {
        case SEEK_SET: whence = 0; break;
        case SEEK_CUR: whence = (int)GetFilePosition2(&info->fileInfo); break;
        case SEEK_END: whence = info->fileInfo.vFileSize; break;
        default: break;
    }
    SetFilePosition2(&info->fileInfo, (int)(whence + offset));
    return GetFilePosition2(&info->fileInfo) <= info->fileInfo.vFileSize;
}
long tellVorbis(void *ptr)
{
    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    return GetFilePosition2(&info->fileInfo);
}
int closeVorbis(void *ptr)
{
    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    return CloseFile2(&info->fileInfo);
}
//#endif

void ProcessMusicStream(Sint32 *stream, size_t bytes_wanted)
{
    if (!musInfo.loaded)
        return;
    switch (musicStatus) {
        case MUSIC_READY:
        case MUSIC_PLAYING: {
#if RETRO_USING_SDL2
            while (SDL_AudioStreamAvailable(musInfo.stream) < bytes_wanted) {
                // We need more samples: get some
                long bytes_read = ov_read(&musInfo.vorbisFile, (char *)musInfo.buffer, sizeof(musInfo.buffer), 0, 2, 1, &musInfo.vorbBitstream);

                if (bytes_read == 0) {
                    // We've reached the end of the file
                    if (musInfo.trackLoop) {
                        ov_pcm_seek(&musInfo.vorbisFile, 0);
                        continue;
                    }
                    else {
                        musicStatus = MUSIC_STOPPED;
                        break;
                    }
                }

                if (SDL_AudioStreamPut(musInfo.stream, musInfo.buffer, bytes_read) == -1)
                    return;
            }

            // Now that we know there are enough samples, read them and mix them
            int bytes_done = SDL_AudioStreamGet(musInfo.stream, musInfo.buffer, bytes_wanted);
            if (bytes_done == -1) {
                return;
            }
            if (bytes_done != 0)
                ProcessAudioMixing(stream, musInfo.buffer, bytes_done / sizeof(Sint16), (bgmVolume * masterVolume) / MAX_VOLUME, 0);
#endif

#if RETRO_USING_SDL1
            size_t bytes_gotten = 0;
            byte *buffer        = (byte *)malloc(bytes_wanted);
            memset(buffer, 0, bytes_wanted);
            while (bytes_gotten < bytes_wanted) {
                // We need more samples: get some
                long bytes_read =
                    ov_read(&musInfo.vorbisFile, (char *)musInfo.buffer,
                            sizeof(musInfo.buffer) > (bytes_wanted - bytes_gotten) ? (bytes_wanted - bytes_gotten) : sizeof(musInfo.buffer), 0, 2, 1,
                            &musInfo.vorbBitstream);

                if (bytes_read == 0) {
                    // We've reached the end of the file
                    if (musInfo.trackLoop) {
                        ov_pcm_seek(&musInfo.vorbisFile, musInfo.loopPoint);
                        continue;
                    }
                    else {
                        musicStatus = MUSIC_STOPPED;
                        break;
                    }
                }

                if (bytes_read > 0) {
                    memcpy(buffer + bytes_gotten, musInfo.buffer, bytes_read);
                    bytes_gotten += bytes_read;
                }
                else {
                    printLog("Music read error: vorbis error: %d", bytes_read);
                }
            }

            if (bytes_gotten > 0) {
                SDL_AudioCVT convert;
                MEM_ZERO(convert);
                int cvtResult = SDL_BuildAudioCVT(&convert, musInfo.spec.format, musInfo.spec.channels, musInfo.spec.freq, audioDeviceFormat.format,
                                                  audioDeviceFormat.channels, audioDeviceFormat.freq);
                if (cvtResult == 0) {
                    if (convert.len_mult > 0) {
                        convert.buf = (byte *)malloc(bytes_gotten * convert.len_mult);
                        convert.len = bytes_gotten;
                        memcpy(convert.buf, buffer, bytes_gotten);
                        SDL_ConvertAudio(&convert);
                    }
                }

                if (cvtResult == 0)
                    ProcessAudioMixing(stream, (const Sint16 *)convert.buf, bytes_gotten / sizeof(Sint16), (bgmVolume * masterVolume) / MAX_VOLUME,
                                       0);

                if (convert.len > 0 && convert.buf)
                    free(convert.buf);
            }
            if (bytes_wanted > 0)
                free(buffer);
#endif
            break;
        } 
        case MUSIC_STOPPED:
        case MUSIC_PAUSED:
        case MUSIC_LOADING:
            // dont play
            break;
    }
}

void ProcessAudioPlayback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata; // Unused
	
    if (!audioEnabled)
        return;

#if RETRO_USING_SDL2 || RETRO_USING_SDL1
    Sint16 *output_buffer = (Sint16 *)stream;

    size_t samples_remaining = (size_t)len / sizeof(Sint16);
    while (samples_remaining != 0) {
        Sint32 mix_buffer[MIX_BUFFER_SAMPLES];
        memset(mix_buffer, 0, sizeof(mix_buffer));

        const size_t samples_to_do = (samples_remaining < MIX_BUFFER_SAMPLES) ? samples_remaining : MIX_BUFFER_SAMPLES;

        // Mix music
        ProcessMusicStream(mix_buffer, samples_to_do * sizeof(Sint16));

        // Mix SFX
        for (byte i = 0; i < CHANNEL_COUNT; ++i) {
            ChannelInfo *sfx = &sfxChannels[i];
            if (sfx == NULL)
                continue;

            if (sfx->sfxID < 0)
                continue;

            if (sfx->samplePtr) {
                Sint16 buffer[MIX_BUFFER_SAMPLES];

                size_t samples_done = 0;
                while (samples_done != samples_to_do) {
                    size_t sampleLen = (sfx->sampleLength < samples_to_do - samples_done) ? sfx->sampleLength : samples_to_do - samples_done;
                    memcpy(&buffer[samples_done], sfx->samplePtr, sampleLen * sizeof(Sint16));

                    samples_done += sampleLen;
                    sfx->samplePtr += sampleLen;
                    sfx->sampleLength -= sampleLen;

                    if (sfx->sampleLength == 0) {
                        if (sfx->loopSFX) {
                            sfx->samplePtr    = sfxList[sfx->sfxID].buffer;
                            sfx->sampleLength = sfxList[sfx->sfxID].length;
                        }
                        else {
                            StopSfx(sfx->sfxID);
                            break;
                        }
                    }
                }

#if RETRO_USING_SDL1 || RETRO_USING_SDL2
                ProcessAudioMixing(mix_buffer, buffer, samples_done, sfxVolume, sfx->pan);
#endif
            }
        }

        // Clamp mixed samples back to 16-bit and write them to the output buffer
        for (size_t i = 0; i < sizeof(mix_buffer) / sizeof(*mix_buffer); ++i) {
            const Sint16 max_audioval = ((1 << (16 - 1)) - 1);
            const Sint16 min_audioval = -(1 << (16 - 1));

            const Sint32 sample = mix_buffer[i];

            if (sample > max_audioval)
                *output_buffer++ = max_audioval;
            else if (sample < min_audioval)
                *output_buffer++ = min_audioval;
            else
                *output_buffer++ = sample;
        }

        samples_remaining -= samples_to_do;
    }
#else
// Generic (platform-independent) code that mixes music and SFX together into the stream
    int numbytes = len * 2 * 2;
    Sint16* streamS = (Sint16*)stream;
    char b[256];
    
    memset(stream, 0x0, numbytes);
    if (musicStatus == MUSIC_READY || musicStatus == MUSIC_PLAYING) {
        int bytesRead = 0;

        while (!musicFifoOnly && bytesRead < numbytes) {
            int r = ov_read(&musInfo.vorbisFile, (char*)b, 256, isBigEndian, 2, 1, &musInfo.vorbBitstream);

            if (r == 0) {
                // We've reached the end of the file
                if (musInfo.trackLoop) {
                    ov_pcm_seek(&musInfo.vorbisFile, musInfo.loopPoint);
                    continue;
                } else {
                    musicStatus = MUSIC_STOPPED;
                    break;
                }
            }else if (r == OV_HOLE || r == OV_EBADLINK ||
		r == OV_EINVAL) {
		musicStatus = MUSIC_STOPPED;
		break;
	    }

            bytesRead += 256;
	    musicFifoPush(b, 256);
        }

	musicFifoPop(stream, numbytes);
	
	if (musicFifoPos > 0 && musicFifoPos % numbytes == 0)
		musicFifoOnly = true;
	else if (musicFifoPos == 0 && musicFifoOnly)
		musicFifoOnly = false;

        if (bytesRead > 0) {
            for (int x = 0; (x < bytesRead / 2) && (x < numbytes / 2); x++) {
                Sint32 c = ADJUST_VOLUME(streamS[x], (bgmVolume * masterVolume) / MAX_VOLUME);

		streamS[x] = (c > 0x7FFF) ? 0x7FFF : ( (c < -0x7FFF) ? -0x7FFF : c);
            }
        }
    }

    for (byte i = 0; i < CHANNEL_COUNT; ++i) {
        ChannelInfo* sfx = &sfxChannels[i];
        if (sfx == NULL)
            continue;

        if (sfx->sfxID < 0)
            continue;

        if (sfx->samplePtr) {
            Sint16 buffer[len * 2];

            memset(buffer, 0, numbytes);

            size_t samples_done = 0;
            int samples_to_do = len * 2;

            while (samples_done != samples_to_do) {
                size_t sampleLen = (sfx->sampleLength < samples_to_do - samples_done) ? sfx->sampleLength : samples_to_do - samples_done;
                memcpy(&buffer[samples_done], sfx->samplePtr, sampleLen * sizeof(Sint16));

                samples_done += sampleLen;
                sfx->samplePtr += sampleLen;
                sfx->sampleLength -= sampleLen;

                if (sfx->sampleLength <= 0) {
                    if (sfx->loopSFX) {
                        sfx->samplePtr = sfxList[sfx->sfxID].buffer;
                        sfx->sampleLength = sfxList[sfx->sfxID].length;
                    } else {
                        StopSfx(sfx->sfxID);
                        break;
                    }
                }
            }

            for (int x = 0; (x < samples_done) && (x < numbytes / 2); x++) {
                Sint32 c = buffer[x] / 2 + streamS[x];
                c = ADJUST_VOLUME(c, (sfxVolume * 100) / MAX_VOLUME);

                streamS[x] = (c > 0x7FFF) ? 0x7FFF : ( (c < -0x7FFF) ? -0x7FFF : c);
            }
        }
    }

#if RETRO_USING_ALLEGRO4 && !RETRO_WSSAUDIO && !RETRO_DOSSOUND
// Allegro needs unsigned samples
	for (int x = 0; x < len * 2; x++)
		streamS[x] ^= 0x8000;
#endif
	
#endif
}

#if RETRO_USING_SDL1 || RETRO_USING_SDL2
void ProcessAudioMixing(Sint32 *dst, const Sint16 *src, int len, int volume, sbyte pan)
{
    if (volume == 0)
        return;

    if (volume > MAX_VOLUME)
        volume = MAX_VOLUME;

    float panL = 0;
    float panR = 0;
    int i      = 0;

    if (pan < 0) {
        panR = 1.0f - abs(pan / 100.0f);
        panL = 1.0f;
    }
    else if (pan > 0) {
        panL = 1.0f - abs(pan / 100.0f);
        panR = 1.0f;
    }

    while (len--) {
        Sint32 sample = *src++;
        ADJUST_VOLUME(sample, volume);

        if (pan != 0) {
            if ((i % 2) != 0) {
                sample *= panR;
            }
            else {
                sample *= panL;
            }
        }

        *dst++ += sample;

        i++;
    }
}
#endif

void LoadMusic(void *userdata)
{
    (void)userdata;

    if (trackBuffer < 0 || trackBuffer >= TRACK_COUNT) {
        StopMusic();
        return;
    }

    TrackInfo *trackPtr = &musicTracks[trackBuffer];

    if (!trackPtr->fileName[0]) {
        StopMusic();
        return;
    }

    if (musInfo.loaded)
        StopMusic();

    if (LoadFile2(trackPtr->fileName, &musInfo.fileInfo)) {
        musInfo.trackLoop = trackPtr->trackLoop;
        musInfo.loaded    = true;

        unsigned long long samples = 0;
        ov_callbacks callbacks;

        callbacks.read_func  = readVorbis;
        callbacks.seek_func  = seekVorbis;
        callbacks.tell_func  = tellVorbis;
        callbacks.close_func = closeVorbis;

        int error = ov_open_callbacks(&musInfo, &musInfo.vorbisFile, NULL, 0, callbacks);
        if (error != 0) {
        }

        musInfo.vorbBitstream = -1;
        musInfo.vorbisFile.vi = ov_info(&musInfo.vorbisFile, -1);

#if RETRO_USING_SDL2
        musInfo.stream = SDL_NewAudioStream(AUDIO_S16, musInfo.vorbisFile.vi->channels, musInfo.vorbisFile.vi->rate, audioDeviceFormat.format,
                                            audioDeviceFormat.channels, audioDeviceFormat.freq);
        if (!musInfo.stream) {
            printLog("Failed to create stream: %s", SDL_GetError());
        }
#endif

#if RETRO_USING_SDL1
        musInfo.spec.format   = AUDIO_S16;
        musInfo.spec.channels = musInfo.vorbisFile.vi->channels;
        musInfo.spec.freq     = (int)musInfo.vorbisFile.vi->rate;
#endif

        musInfo.buffer = new Sint16[MIX_BUFFER_SAMPLES];

        musicStatus  = MUSIC_PLAYING;
        masterVolume = MAX_VOLUME;
        trackID      = trackBuffer;
        trackBuffer  = -1;
    }
}

void SetMusicTrack(char *filePath, byte trackID, bool loop)
{
    LockAudioDevice();
    TrackInfo *track = &musicTracks[trackID];
    StrCopy(track->fileName, "Data/Music/");
    StrAdd(track->fileName, filePath);
    track->trackLoop = loop;
    UnlockAudioDevice();
}
bool PlayMusic(int track)
{
    if (!audioEnabled)
        return false;

    LockAudioDevice();
    if (track < 0 || track >= TRACK_COUNT) {
        StopMusic();
        trackBuffer = -1;
        return false;
    }
    trackBuffer = track;
    musicStatus = MUSIC_LOADING;
  //  SDL_CreateThread((SDL_ThreadFunction)LoadMusic, "LoadMusic", NULL);
    
    LoadMusic(NULL);
    UnlockAudioDevice();
    return true;
}

void LoadSfx(char *filePath, byte sfxID)
{
    if (!audioEnabled)
        return;

    FileInfo info;
    char fullPath[0x80];

    StrCopy(fullPath, "Data/SoundFX/");
    StrAdd(fullPath, filePath);

    if (LoadFile(fullPath, &info)) {
        byte *sfx = new byte[info.fileSize];
        FileRead(sfx, info.fileSize);
        CloseFile();

        //Un-encrypt sfx
        if (info.encrypted) {
            for (int i = 0; i < info.fileSize; ++i) sfx[i] ^= 0xFF;
        }

#if RETRO_USING_SDL1 || RETRO_USING_SDL2
        SDL_LockAudio();
        SDL_RWops *src = SDL_RWFromMem(sfx, info.fileSize);
        if (src == NULL) {
            printLog("Unable to open sfx: %s", info.fileName);
        }
        else {
            SDL_AudioSpec wav_spec;
            uint wav_length;
            byte *wav_buffer;
            SDL_AudioSpec *wav = SDL_LoadWAV_RW(src, 0, &wav_spec, &wav_buffer, &wav_length);

            SDL_RWclose(src);
            delete[] sfx;
            if (wav == NULL) {
                printLog("Unable to read sfx: %s", info.fileName);
            }
            else {
                SDL_AudioCVT convert;
                if (SDL_BuildAudioCVT(&convert, wav->format, wav->channels, wav->freq, audioDeviceFormat.format, audioDeviceFormat.channels,
                                      audioDeviceFormat.freq)
                    > 0) {
                    convert.buf = (byte *)malloc(wav_length * convert.len_mult);
                    convert.len = wav_length;
                    memcpy(convert.buf, wav_buffer, wav_length);
                    SDL_ConvertAudio(&convert);

                    StrCopy(sfxList[sfxID].name, filePath);
                    sfxList[sfxID].buffer = (Sint16 *)convert.buf;
                    sfxList[sfxID].length = convert.len_cvt / sizeof(Sint16);
                    sfxList[sfxID].loaded = true;
                    SDL_FreeWAV(wav_buffer);
                }
                else {
                    StrCopy(sfxList[sfxID].name, filePath);
                    sfxList[sfxID].buffer = (Sint16 *)wav_buffer;
                    sfxList[sfxID].length = wav_length / sizeof(Sint16);
                    sfxList[sfxID].loaded = true;
                }
            }
        }
        SDL_UnlockAudio();
#else
// platform-independent WAV loading code, quite dumb
	    int z =	12;
	    int zid, zs=-8;
	    
	    do {
	        z+=zs+8;    
	        zid = sfx[z] | (sfx[z+1] << 8) | (sfx[z+2] << 16) | (sfx[z+3]<<24);		    
	        zs = sfx[z+4] | (sfx[z+5] << 8) | (sfx[z+6] << 16) | (sfx[z+7]<<24);
	    } while(zid != 0x20746d66); // fmt
	    
            int sample_rate = sfx[z+12] | (sfx[z+13] << 8) | (sfx[z+14] << 16) | (sfx[z+15] << 24);
	    int bit_depth = sfx[z+22] | (sfx[z+23] << 8);
            int num_channels = sfx[z+10] | (sfx[z+11] << 8);

	    z += zs + 8;
	    
	    zs = -8;
	    
	    do {
	        z+=zs+8;    
	        zid = sfx[z] | (sfx[z+1] << 8) | (sfx[z+2] << 16) | (sfx[z+3]<<24);		    
	        zs = sfx[z+4] | (sfx[z+5] << 8) | (sfx[z+6] << 16) | (sfx[z+7]<<24);
	    } while(zid != 0x61746164); // data
	    
	    int data_size = sfx[z+4] | (sfx[z+5] << 8) | (sfx[z+6] << 16) | (sfx[z+7] << 24);
	    int num_frames = (data_size / num_channels) / (bit_depth / 8);
	    
            StrCopy(sfxList[sfxID].name, filePath);
            sfxList[sfxID].buffer = WavDataToBuffer(&sfx[z+8], num_frames, num_channels,
                bit_depth);
            sfxList[sfxID].length = num_frames * 2;
            sfxList[sfxID].loaded = true;
		
            delete[] sfx;
#endif
    }
}
void PlaySfx(int sfx, bool loop)
{
    LockAudioDevice();
    int sfxChannelID = nextChannelPos++;
    for (int c = 0; c < CHANNEL_COUNT; ++c) {
        if (sfxChannels[c].sfxID == sfx) {
            sfxChannelID = c;
            break;
        }
    }

    ChannelInfo *sfxInfo  = &sfxChannels[sfxChannelID];
    sfxInfo->sfxID        = sfx;
    sfxInfo->samplePtr    = sfxList[sfx].buffer;
    sfxInfo->sampleLength = sfxList[sfx].length;
    sfxInfo->loopSFX      = loop;
    sfxInfo->pan          = 0;
    if (nextChannelPos == CHANNEL_COUNT)
        nextChannelPos = 0;
    UnlockAudioDevice();
}
void SetSfxAttributes(int sfx, int loopCount, sbyte pan)
{
    LockAudioDevice();
    int sfxChannel = -1;
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        if (sfxChannels[i].sfxID == sfx || sfxChannels[i].sfxID == -1) {
            sfxChannel = i;
            break;
        }
    }
    if (sfxChannel == -1)
        return; // wasn't found

    // TODO: is this right? should it play an sfx here? without this rings dont play any sfx so I assume it must be?
    ChannelInfo *sfxInfo  = &sfxChannels[sfxChannel];
    sfxInfo->samplePtr    = sfxList[sfx].buffer;
    sfxInfo->sampleLength = sfxList[sfx].length;
    sfxInfo->loopSFX      = loopCount == -1 ? sfxInfo->loopSFX : loopCount;
    sfxInfo->pan          = pan;
    sfxInfo->sfxID        = sfx;
    UnlockAudioDevice();
}
