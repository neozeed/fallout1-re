#include "int/sound.h"

#include <io.h>
#include <limits.h>
#include <math.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>

#include "plib/gnw/debug.h"
#include "plib/gnw/memory.h"
#include "plib/gnw/winmain.h"

typedef struct FadeSound {
    Sound* sound;
    int deltaVolume;
    int targetVolume;
    int initialVolume;
    int currentVolume;
    int field_14;
    struct FadeSound* prev;
    struct FadeSound* next;
} FadeSound;

//static_assert(sizeof(Sound) == 156, "wrong size");

static void* defaultMalloc(size_t size);
static void* defaultRealloc(void* ptr, size_t size);
static void defaultFree(void* ptr);
static long soundFileSize(int fileHandle);
static long soundTellData(int fileHandle);
static int soundWriteData(int fileHandle, const void* buf, unsigned int size);
static int soundReadData(int fileHandle, void* buf, unsigned int size);
static int soundOpenData(const char* filePath, int flags);
static int soundSeekData(int fileHandle, long offset, int origin);
static int soundCloseData(int fileHandle);
static char* defaultMangler(char* fname);
static void refreshSoundBuffers(Sound* sound);
static int preloadBuffers(Sound* sound);
static int addSoundData(Sound* sound, unsigned char* buf, int size);
//#if _MSC_VER < 1000
#if 0
typedef unsigned long * DWORD_PTR;
#endif
static void CALLBACK doTimerEvent(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);
static void removeTimedEvent(unsigned int* timerId);
static void removeFadeSound(FadeSound* fadeSound);
static void fadeSounds();
static int internalSoundFade(Sound* sound, int duration, int targetVolume, int a4);

// 0x507E04
static FadeSound* fadeHead = NULL;

// 0x507E08
static FadeSound* fadeFreeList = NULL;

// 0x507E0C
static unsigned int fadeEventHandle = UINT_MAX;

// 0x507E10
static int defaultChannel = 2;

// 0x507E14
static SoundMallocFunc* mallocPtr = defaultMalloc;

// 0x507E18
static SoundReallocFunc* reallocPtr = defaultRealloc;

// 0x507E1C
static SoundFreeFunc* freePtr = defaultFree;

// 0x507E20
static SoundFileIO defaultStream = {
    soundOpenData,
    soundCloseData,
    soundReadData,
    soundWriteData,
    soundSeekData,
    soundTellData,
    soundFileSize,
    -1,
};

// 0x507E40
static SoundFileNameMangler* nameMangler = defaultMangler;

// 0x507E44
static const char* errorMsgs[SOUND_ERR_COUNT] = {
    "sound.c: No error",
    "sound.c: SOS driver not loaded",
    "sound.c: SOS invalid pointer",
    "sound.c: SOS detect initialized",
    "sound.c: SOS fail on file open",
    "sound.c: SOS memory fail",
    "sound.c: SOS invalid driver ID",
    "sound.c: SOS no driver found",
    "sound.c: SOS detection failure",
    "sound.c: SOS driver loaded",
    "sound.c: SOS invalid handle",
    "sound.c: SOS no handles",
    "sound.c: SOS paused",
    "sound.c: SOS not paused",
    "sound.c: SOS invalid data",
    "sound.c: SOS drv file fail",
    "sound.c: SOS invalid port",
    "sound.c: SOS invalid IRQ",
    "sound.c: SOS invalid DMA",
    "sound.c: SOS invalid DMA IRQ",
    "sound.c: no device",
    "sound.c: not initialized",
    "sound.c: no sound",
    "sound.c: function not supported",
    "sound.c: no buffers available",
    "sound.c: file not found",
    "sound.c: already playing",
    "sound.c: not playing",
    "sound.c: already paused",
    "sound.c: not paused",
    "sound.c: invalid handle",
    "sound.c: no memory available",
    "sound.c: unknown error",
};

// 0x6651A0
static int soundErrorno;

// 0x6651A4
static int masterVol;

// 0x6651A8
LPDIRECTSOUNDBUFFER primaryDSBuffer;

// 0x6651AC
static int sampleRate;

// Number of sounds currently playing.
//
// 0x6651B0
static int numSounds;

// 0x6651B4
static int deviceInit;

// 0x6651B8
static int dataSize;

// 0x6651BC
static int numBuffers;

// 0x6651C0
static bool driverInit;

// 0x6651C4
static Sound* soundMgrList;

// 0x6651C8
LPDIRECTSOUND soundDSObject;

// 0x499C80
static void* defaultMalloc(size_t size)
{
    return malloc(size);
}

// 0x499C88
static void* defaultRealloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

// 0x499C90
static void defaultFree(void* ptr)
{
    free(ptr);
}

// 0x499C98
void soundRegisterAlloc(SoundMallocFunc* mallocProc, SoundReallocFunc* reallocProc, SoundFreeFunc* freeProc)
{
    mallocPtr = mallocProc;
    reallocPtr = reallocProc;
    freePtr = freeProc;
}

// 0x499CAC
static long soundFileSize(int fileHandle)
{
    long pos;
    long size;

    pos = tell(fileHandle);
    size = lseek(fileHandle, 0, SEEK_END);
    lseek(fileHandle, pos, SEEK_SET);

    return size;
}

// 0x499CE0
static long soundTellData(int fileHandle)
{
    return tell(fileHandle);
}

// 0x499CE8
static int soundWriteData(int fileHandle, const void* buf, unsigned int size)
{
    return write(fileHandle, buf, size);
}

// 0x499CF0
static int soundReadData(int fileHandle, void* buf, unsigned int size)
{
    return read(fileHandle, buf, size);
}

// 0x499CF8
static int soundOpenData(const char* filePath, int flags)
{
    return open(filePath, flags);
}

// 0x499D04
static int soundSeekData(int fileHandle, long offset, int origin)
{
    return lseek(fileHandle, offset, origin);
}

// 0x499D0C
static int soundCloseData(int fileHandle)
{
    return close(fileHandle);
}

// 0x499D1C
static char* defaultMangler(char* fname)
{
    return fname;
}

// 0x499D20
const char* soundError(int err)
{
    if (err == -1) {
        err = soundErrorno;
    }

    if (err < 0 || err > SOUND_UNKNOWN_ERROR) {
        err = SOUND_UNKNOWN_ERROR;
    }

    return errorMsgs[err];
}

// 0x499D40
static void refreshSoundBuffers(Sound* sound)
{
    DWORD readPos;
    DWORD writePos;
    HRESULT hr;
    VOID* audioPtr1;
    VOID* audioPtr2;
    DWORD audioBytes1;
    DWORD audioBytes2;
	int v6,v53;
	unsigned char* audioPtr;
    int audioBytes;

    if (sound->field_3C & 0x80) {
        return;
    }

    hr = IDirectSoundBuffer_GetCurrentPosition(sound->directSoundBuffer, &readPos, &writePos);
    if (hr != DS_OK) {
        return;
    }

    if (readPos < sound->field_74) {
        sound->field_64 += readPos + sound->field_78 * sound->field_7C - sound->field_74;
    } else {
        sound->field_64 += readPos - sound->field_74;
    }

    if (sound->field_3C & 0x0100) {
        if (sound->field_44 & 0x20) {
            if (sound->field_3C & 0x0200) {
                sound->field_3C |= 0x80;
            }
        } else {
            if (sound->field_60 <= sound->field_64) {
                sound->field_3C |= 0x0280;
            }
        }
    }
    sound->field_74 = readPos;

    if (sound->field_60 < sound->field_64) {
        int v3;
        do {
            v3 = sound->field_64 - sound->field_60;
            sound->field_64 = v3;
        } while (v3 > sound->field_60);
    }

    v6 = readPos / sound->field_7C;
    if (sound->field_70 == v6) {
        return;
    }

    if (sound->field_70 > v6) {
        v53 = v6 + sound->field_78 - sound->field_70;
    } else {
        v53 = v6 - sound->field_70;
    }

    if (sound->field_7C * v53 >= sound->readLimit) {
        v53 = (sound->readLimit + sound->field_7C - 1) / sound->field_7C;
    }

    if (v53 < sound->field_5C) {
        return;
    }

    hr = IDirectSoundBuffer_Lock(sound->directSoundBuffer, sound->field_7C * sound->field_70, sound->field_7C * v53, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, 0);
    if (hr == DSERR_BUFFERLOST) {
        IDirectSoundBuffer_Restore(sound->directSoundBuffer);
        hr = IDirectSoundBuffer_Lock(sound->directSoundBuffer, sound->field_7C * sound->field_70, sound->field_7C * v53, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, 0);
    }

    if (hr != DS_OK) {
        return;
    }

    if (audioBytes1 + audioBytes2 != sound->field_7C * v53) {
        debug_printf("locked memory region not big enough, wanted %d (%d * %d), got %d (%d + %d)\n", sound->field_7C * v53, v53, sound->field_7C, audioBytes1 + audioBytes2, audioBytes1, audioBytes2);
        debug_printf("Resetting readBuffers from %d to %d\n", v53, (audioBytes1 + audioBytes2) / sound->field_7C);

        v53 = (audioBytes1 + audioBytes2) / sound->field_7C;
        if (v53 < sound->field_5C) {
            debug_printf("No longer above read buffer size, returning\n");
            return;
        }
    }
    audioPtr = (unsigned char*)audioPtr1;
    audioBytes = audioBytes1;
    while (--v53 != -1) {
        int bytesRead;
        if (sound->field_3C & 0x0200) {
            bytesRead = sound->field_7C;
            memset(sound->field_20, 0, bytesRead);
        } else {
            int bytesToRead = sound->field_7C;
            if (sound->field_58 != -1) {
                int pos = sound->io.tell(sound->io.fd);
                if (bytesToRead + pos > sound->field_58) {
                    bytesToRead = sound->field_58 - pos;
                }
            }

            bytesRead = sound->io.read(sound->io.fd, sound->field_20, bytesToRead);
            if (bytesRead < sound->field_7C) {
                if (!(sound->field_3C & 0x20) || (sound->field_3C & 0x0100)) {
                    memset(sound->field_20 + bytesRead, 0, sound->field_7C - bytesRead);
                    sound->field_3C |= 0x0200;
                    bytesRead = sound->field_7C;
                } else {
					int v20;
                    while (bytesRead < sound->field_7C) {
                        if (sound->field_50 == -1) {
                            sound->io.seek(sound->io.fd, sound->field_54, SEEK_SET);
                            if (sound->callback != NULL) {
                                sound->callback(sound->callbackUserData, 0x0400);
                            }
                        } else {
                            if (sound->field_50 <= 0) {
                                sound->field_58 = -1;
                                sound->field_54 = 0;
                                sound->field_50 = 0;
                                sound->field_3C &= ~0x20;
                                bytesRead += sound->io.read(sound->io.fd, sound->field_20 + bytesRead, sound->field_7C - bytesRead);
                                break;
                            }

                            sound->field_50--;
                            sound->io.seek(sound->io.fd, sound->field_54, SEEK_SET);

                            if (sound->callback != NULL) {
                                sound->callback(sound->callbackUserData, 0x400);
                            }
                        }

                        if (sound->field_58 == -1) {
                            bytesToRead = sound->field_7C - bytesRead;
                        } else {
                            int pos = sound->io.tell(sound->io.fd);
                            if (sound->field_7C + bytesRead + pos <= sound->field_58) {
                                bytesToRead = sound->field_7C - bytesRead;
                            } else {
                                bytesToRead = sound->field_58 - bytesRead - pos;
                            }
                        }

                        v20 = sound->io.read(sound->io.fd, sound->field_20 + bytesRead, bytesToRead);
                        bytesRead += v20;
                        if (v20 < bytesToRead) {
                            break;
                        }
                    }
                }
            }
        }

        if (bytesRead > audioBytes) {
            if (audioBytes != 0) {
                memcpy(audioPtr, sound->field_20, audioBytes);
            }

            if (audioPtr2 != NULL) {
                memcpy(audioPtr2, sound->field_20 + audioBytes, bytesRead - audioBytes);
                audioPtr = (unsigned char*)audioPtr2 + bytesRead - audioBytes;
                audioBytes = audioBytes2 - bytesRead;
            } else {
                debug_printf("Hm, no second write pointer, but buffer not big enough, this shouldn't happen\n");
            }
        } else {
            memcpy(audioPtr, sound->field_20, bytesRead);
            audioPtr += bytesRead;
            audioBytes -= bytesRead;
        }
    }

    IDirectSoundBuffer_Unlock(sound->directSoundBuffer, audioPtr1, audioBytes1, audioPtr2, audioBytes2);

    sound->field_70 = v6;

    return;
}

// 0x49A1E4
int soundInit(int a1, int a2, int a3, int a4, int rate)
{
    HRESULT hr;
    DSBUFFERDESC dsbdesc;
    WAVEFORMATEX pcmwf;
    DSCAPS dscaps;
    DWORD v24;

    if (GNW95_DirectSoundCreate(0, &soundDSObject, 0) != DS_OK) {
        soundDSObject = NULL;
        soundErrorno = SOUND_SOS_DETECTION_FAILURE;
        return soundErrorno;
    }

    if (IDirectSound_SetCooperativeLevel(soundDSObject, GNW95_hwnd, DSSCL_EXCLUSIVE) != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    sampleRate = rate;
    dataSize = a4;
    numBuffers = a2;
    driverInit = true;
    deviceInit = 1;

    memset(&dsbdesc, 0, sizeof(dsbdesc));
    dsbdesc.dwSize = sizeof(dsbdesc);
    dsbdesc.dwFlags = DSCAPS_PRIMARYMONO;
    dsbdesc.dwBufferBytes = 0;

    hr = IDirectSound_CreateSoundBuffer(soundDSObject, &dsbdesc, &primaryDSBuffer, NULL);
    if (hr != DS_OK) {
        switch (hr) {
        case DSERR_ALLOCATED:
            debug_printf("%s:%s\n", "CreateSoundBuffer", "DSERR_ALLOCATED");
            break;
        case DSERR_BADFORMAT:
            debug_printf("%s:%s\n", "CreateSoundBuffer", "DSERR_BADFORMAT");
            break;
        case DSERR_INVALIDPARAM:
            debug_printf("%s:%s\n", "CreateSoundBuffer", "DSERR_INVALIDPARAM");
            break;
        case DSERR_NOAGGREGATION:
            debug_printf("%s:%s\n", "CreateSoundBuffer", "DSERR_NOAGGREGATION");
            break;
        case DSERR_OUTOFMEMORY:
            debug_printf("%s:%s\n", "CreateSoundBuffer", "DSERR_OUTOFMEMORY");
            break;
        case DSERR_UNINITIALIZED:
            debug_printf("%s:%s\n", "CreateSoundBuffer", "DSERR_UNINITIALIZED");
            break;
        case DSERR_UNSUPPORTED:
            debug_printf("%s:%s\n", "CreateSoundBuffer", "DSERR_UNSUPPORTED");
            break;
        }

        exit(1);
    }

    memset(&pcmwf, 0, sizeof(pcmwf));

    memset(&dscaps, 0, sizeof(dscaps));
    dscaps.dwSize = sizeof(dscaps);

    hr = IDirectSound_GetCaps(soundDSObject, &dscaps);
    if (hr != DS_OK) {
        debug_printf("soundInit: Error getting primary buffer parameters\n");
        goto out;
    }

    pcmwf.nSamplesPerSec = rate;
    pcmwf.wFormatTag = WAVE_FORMAT_PCM;

    if (dscaps.dwFlags & DSCAPS_PRIMARY16BIT) {
        pcmwf.wBitsPerSample = 16;
    } else {
        pcmwf.wBitsPerSample = 8;
    }

    pcmwf.nChannels = (dscaps.dwFlags & DSCAPS_PRIMARYSTEREO) ? 2 : 1;
    pcmwf.nBlockAlign = pcmwf.wBitsPerSample * pcmwf.nChannels / 8;
    pcmwf.nSamplesPerSec = rate;
    pcmwf.cbSize = 0;
    pcmwf.nAvgBytesPerSec = pcmwf.nBlockAlign * rate;

    debug_printf("soundInit: Setting primary buffer to: %d bit, %d channels, %d rate\n", pcmwf.wBitsPerSample, pcmwf.nChannels, rate);
    hr = IDirectSoundBuffer_SetFormat(primaryDSBuffer, &pcmwf);
    if (hr != DS_OK) {
        debug_printf("soundInit: Couldn't change rate to %d\n", rate);

        switch (hr) {
        case DSERR_BADFORMAT:
            debug_printf("%s:%s\n", "SetFormat", "DSERR_BADFORMAT");
            break;
        case DSERR_INVALIDCALL:
            debug_printf("%s:%s\n", "SetFormat", "DSERR_INVALIDCALL");
            break;
        case DSERR_INVALIDPARAM:
            debug_printf("%s:%s\n", "SetFormat", "DSERR_INVALIDPARAM");
            break;
        case DSERR_OUTOFMEMORY:
            debug_printf("%s:%s\n", "SetFormat", "DSERR_OUTOFMEMORY");
            break;
        case DSERR_PRIOLEVELNEEDED:
            debug_printf("%s:%s\n", "SetFormat", "DSERR_PRIOLEVELNEEDED");
            break;
        case DSERR_UNSUPPORTED:
            debug_printf("%s:%s\n", "SetFormat", "DSERR_UNSUPPORTED");
            break;
        }

        goto out;
    }

    hr = IDirectSoundBuffer_GetFormat(primaryDSBuffer, &pcmwf, sizeof(WAVEFORMATEX), &v24);
    if (hr != DS_OK) {
        debug_printf("soundInit: Couldn't read new settings\n");
        goto out;
    }

    debug_printf("soundInit: Primary buffer settings set to: %d bit, %d channels, %d rate\n", pcmwf.wBitsPerSample, pcmwf.nChannels, pcmwf.nSamplesPerSec);

    if (dscaps.dwFlags & DSCAPS_EMULDRIVER) {
        debug_printf("soundInit: using DirectSound emulated drivers\n");
    }

out:

    soundSetMasterVolume(VOLUME_MAX);
    soundErrorno = SOUND_NO_ERROR;

    return 0;
}

// 0x49A5D8
void soundClose()
{
    while (soundMgrList != NULL) {
        Sound* next = soundMgrList->next;
        soundDelete(soundMgrList);
        soundMgrList = next;
    }

    if (fadeEventHandle != -1) {
        removeTimedEvent(&fadeEventHandle);
    }

    while (fadeFreeList != NULL) {
        FadeSound* next = fadeFreeList->next;
        freePtr(fadeFreeList);
        fadeFreeList = next;
    }

    if (primaryDSBuffer != NULL) {
        IDirectSoundBuffer_Release(primaryDSBuffer);
        primaryDSBuffer = NULL;
    }

    if (soundDSObject != NULL) {
        IDirectSound_Release(soundDSObject);
        soundDSObject = NULL;
    }

    soundErrorno = SOUND_NO_ERROR;
    driverInit = false;
}

// 0x49A688
Sound* soundAllocate(int a1, int a2)
{
	Sound* sound;
	WAVEFORMATEX* wfxFormat;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return NULL;
    }

    sound = (Sound*)mallocPtr(sizeof(*sound));
    memset(sound, 0, sizeof(*sound));

    memcpy(&(sound->io), &defaultStream, sizeof(defaultStream));

    wfxFormat = (WAVEFORMATEX*)mallocPtr(sizeof(*wfxFormat));
    memset(wfxFormat, 0, sizeof(*wfxFormat));

    wfxFormat->wFormatTag = 1;
    wfxFormat->nChannels = 1;

    if (a2 & 0x08) {
        wfxFormat->wBitsPerSample = 16;
    } else {
        wfxFormat->wBitsPerSample = 8;
    }

    if (!(a2 & 0x02)) {
        a2 |= 0x02;
    }

    wfxFormat->nSamplesPerSec = sampleRate;
    wfxFormat->nBlockAlign = wfxFormat->nChannels * (wfxFormat->wBitsPerSample / 8);
    wfxFormat->cbSize = 0;
    wfxFormat->nAvgBytesPerSec = wfxFormat->nBlockAlign * wfxFormat->nSamplesPerSec;

    sound->field_3C = a2;
    sound->field_44 = a1;
    sound->field_7C = dataSize;
    sound->field_64 = 0;
    sound->directSoundBuffer = 0;
    sound->field_40 = 0;
    sound->directSoundBufferDescription.dwSize = sizeof(DSBUFFERDESC);
    sound->directSoundBufferDescription.dwFlags = DSBCAPS_GETCURRENTPOSITION2;
    sound->field_78 = numBuffers;
    sound->readLimit = sound->field_7C * numBuffers;

    if (a2 & 0x2) {
        sound->directSoundBufferDescription.dwFlags |= DSBCAPS_CTRLVOLUME;
    }

    if (a2 & 0x4) {
        sound->directSoundBufferDescription.dwFlags |= DSBCAPS_CTRLPAN;
    }

    if (a2 & 0x40) {
        sound->directSoundBufferDescription.dwFlags |= DSBCAPS_CTRLFREQUENCY;
    }

    sound->directSoundBufferDescription.lpwfxFormat = wfxFormat;

    if (a1 & 0x10) {
        sound->field_50 = -1;
        sound->field_3C |= 0x20;
    }

    sound->field_58 = -1;
    sound->field_5C = 1;
    sound->volume = VOLUME_MAX;
    sound->prev = NULL;
    sound->field_54 = 0;
    sound->next = soundMgrList;

    if (soundMgrList != NULL) {
        soundMgrList->prev = sound;
    }

    soundMgrList = sound;

    return sound;
}

// 0x49A88C
static int preloadBuffers(Sound* sound)
{
    unsigned char* buf;
    int bytes_read;
    int result;
    int v15;
    unsigned char* v14;
    int size;

    size = sound->io.filelength(sound->io.fd);
    sound->field_60 = size;

    if (sound->field_44 & 0x02) {
        if (!(sound->field_3C & 0x20)) {
            sound->field_3C |= 0x0120;
        }

        if (sound->field_78 * sound->field_7C >= size) {
            if (size / sound->field_7C * sound->field_7C != size) {
                size = (size / sound->field_7C + 1) * sound->field_7C;
            }
        } else {
            size = sound->field_78 * sound->field_7C;
        }
    } else {
        sound->field_44 &= ~(0x03);
        sound->field_44 |= 0x01;
    }

    buf = (unsigned char*)mallocPtr(size);
    bytes_read = sound->io.read(sound->io.fd, buf, size);
    if (bytes_read != size) {
        if (!(sound->field_3C & 0x20) || (sound->field_3C & (0x01 << 8))) {
            memset(buf + bytes_read, 0, size - bytes_read);
        } else {
            v14 = buf + bytes_read;
            v15 = bytes_read;
            while (size - v15 > bytes_read) {
                memcpy(v14, buf, bytes_read);
                v15 += bytes_read;
                v14 += bytes_read;
            }

            if (v15 < size) {
                memcpy(v14, buf, size - v15);
            }
        }
    }

    result = soundSetData(sound, buf, size);
    freePtr(buf);

    if (sound->field_44 & 0x01) {
        sound->io.close(sound->io.fd);
        sound->io.fd = -1;
    } else {
        if (sound->field_20 == NULL) {
            sound->field_20 = (unsigned char*)mallocPtr(sound->field_7C);
        }
    }

    return result;
}

// 0x49AA1C
int soundLoad(Sound* sound, char* filePath)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    sound->io.fd = sound->io.open(nameMangler(filePath), 0x0200);
    if (sound->io.fd == -1) {
        soundErrorno = SOUND_FILE_NOT_FOUND;
        return soundErrorno;
    }

    return preloadBuffers(sound);
}

// 0x49AA88
int soundRewind(Sound* sound)
{
    HRESULT hr;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (sound->field_44 & 0x02) {
        sound->io.seek(sound->io.fd, 0, SEEK_SET);
        sound->field_70 = 0;
        sound->field_74 = 0;
        sound->field_64 = 0;
        sound->field_3C &= 0xFD7F;
        hr = IDirectSoundBuffer_SetCurrentPosition(sound->directSoundBuffer, 0);
        preloadBuffers(sound);
    } else {
        hr = IDirectSoundBuffer_SetCurrentPosition(sound->directSoundBuffer, 0);
    }

    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    sound->field_40 &= ~SOUND_FLAG_SOUND_IS_DONE;

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49AB4C
static int addSoundData(Sound* sound, unsigned char* buf, int size)
{
    HRESULT hr;
    void* audio_ptr_1;
    DWORD audio_bytes_1;
    void* audio_ptr_2;
    DWORD audio_bytes_2;

    hr = IDirectSoundBuffer_Lock(sound->directSoundBuffer, 0, size, &audio_ptr_1, &audio_bytes_1, &audio_ptr_2, &audio_bytes_2, DSBLOCK_FROMWRITECURSOR);
    if (hr == DSERR_BUFFERLOST) {
        IDirectSoundBuffer_Restore(sound->directSoundBuffer);
        hr = IDirectSoundBuffer_Lock(sound->directSoundBuffer, 0, size, &audio_ptr_1, &audio_bytes_1, &audio_ptr_2, &audio_bytes_2, DSBLOCK_FROMWRITECURSOR);
    }

    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    memcpy(audio_ptr_1, buf, audio_bytes_1);

    if (audio_ptr_2 != NULL) {
        memcpy(audio_ptr_2, buf + audio_bytes_1, audio_bytes_2);
    }

    hr = IDirectSoundBuffer_Unlock(sound->directSoundBuffer, audio_ptr_1, audio_bytes_1, audio_ptr_2, audio_bytes_2);
    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49AC44
int soundSetData(Sound* sound, unsigned char* buf, int size)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (sound->directSoundBuffer == NULL) {
        sound->directSoundBufferDescription.dwBufferBytes = size;

        if (IDirectSound_CreateSoundBuffer(soundDSObject, &(sound->directSoundBufferDescription), &(sound->directSoundBuffer), NULL) != DS_OK) {
            soundErrorno = SOUND_UNKNOWN_ERROR;
            return soundErrorno;
        }
    }

    return addSoundData(sound, buf, size);
}

// 0x49ACC0
int soundPlay(Sound* sound)
{
    HRESULT hr;
    DWORD readPos;
    DWORD writePos;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    // TODO: Check.
    if (sound->field_40 & SOUND_FLAG_SOUND_IS_DONE) {
        soundRewind(sound);
    }

    soundVolume(sound, sound->volume);

    hr = IDirectSoundBuffer_Play(sound->directSoundBuffer, 0, 0, sound->field_3C & 0x20 ? DSBPLAY_LOOPING : 0);

    IDirectSoundBuffer_GetCurrentPosition(sound->directSoundBuffer, &readPos, &writePos);
    sound->field_70 = readPos / sound->field_7C;

    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    sound->field_40 |= SOUND_FLAG_SOUND_IS_PLAYING;

    ++numSounds;

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49ADAC
int soundStop(Sound* sound)
{
    HRESULT hr;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (!(sound->field_40 & SOUND_FLAG_SOUND_IS_PLAYING)) {
        soundErrorno = SOUND_NOT_PLAYING;
        return soundErrorno;
    }

    hr = IDirectSoundBuffer_Stop(sound->directSoundBuffer);
    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    sound->field_40 &= ~SOUND_FLAG_SOUND_IS_PLAYING;
    numSounds--;

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49AE60
int soundDelete(Sound* sample)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sample == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (sample->io.fd != -1) {
        sample->io.close(sample->io.fd);
        sample->io.fd = -1;
    }

    soundMgrDelete(sample);

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49AEC4
int numSoundsPlaying()
{
    return numSounds;
}

// 0x49AECC
int soundContinue(Sound* sound)
{
    HRESULT hr;
    DWORD status;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (!(sound->field_40 & SOUND_FLAG_SOUND_IS_PLAYING) || (sound->field_40 & SOUND_FLAG_SOUND_IS_PAUSED)) {
        soundErrorno = SOUND_NOT_PLAYING;
        return soundErrorno;
    }

    if (sound->field_40 & SOUND_FLAG_SOUND_IS_DONE) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    hr = IDirectSoundBuffer_GetStatus(sound->directSoundBuffer, &status);
    if (hr != DS_OK) {
        debug_printf("Error in soundContinue, %x\n", hr);

        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    if (!(sound->field_3C & 0x80) && (status & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING))) {
        if (!(sound->field_40 & SOUND_FLAG_SOUND_IS_PAUSED) && (sound->field_44 & 0x02)) {
            refreshSoundBuffers(sound);
        }
    } else if (!(sound->field_40 & SOUND_FLAG_SOUND_IS_PAUSED)) {
        if (sound->callback != NULL) {
            sound->callback(sound->callbackUserData, 1);
            sound->callback = NULL;
        }

        if (sound->field_44 & 0x04) {
            sound->callback = NULL;
            soundDelete(sound);
        } else {
            sound->field_40 |= SOUND_FLAG_SOUND_IS_DONE;

            if (sound->field_40 & SOUND_FLAG_SOUND_IS_PLAYING) {
                --numSounds;
            }

            soundStop(sound);

            sound->field_40 &= ~(SOUND_FLAG_SOUND_IS_DONE | SOUND_FLAG_SOUND_IS_PLAYING);
        }
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49B008
bool soundPlaying(Sound* sound)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return false;
    }

    if (sound == NULL || sound->directSoundBuffer == 0) {
        soundErrorno = SOUND_NO_SOUND;
        return false;
    }

    return (sound->field_40 & SOUND_FLAG_SOUND_IS_PLAYING) != 0;
}

// 0x49B048
bool soundDone(Sound* sound)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return false;
    }

    if (sound == NULL || sound->directSoundBuffer == 0) {
        soundErrorno = SOUND_NO_SOUND;
        return false;
    }

    return (sound->field_40 & SOUND_FLAG_SOUND_IS_DONE) != 0;
}

// 0x49B088
bool soundFading(Sound* sound)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return false;
    }

    if (sound == NULL || sound->directSoundBuffer == 0) {
        soundErrorno = SOUND_NO_SOUND;
        return false;
    }

    return (sound->field_40 & SOUND_FLAG_SOUND_IS_FADING) != 0;
}

// 0x49B0C8
bool soundPaused(Sound* sound)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return false;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return false;
    }

    return (sound->field_40 & SOUND_FLAG_SOUND_IS_PAUSED) != 0;
}

// 0x49B108
int soundFlags(Sound* sound, int a2)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return 0;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return 0;
    }

    return sound->field_3C & a2;
}

// 0x49B148
int soundType(Sound* sound, int a2)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return 0;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return 0;
    }

    return sound->field_44 & a2;
}

// 0x49B188
int soundLength(Sound* sound)
{
    int bytesPerSec;
    int v3;
    int v4;
    int result;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    bytesPerSec = sound->directSoundBufferDescription.lpwfxFormat->nAvgBytesPerSec;
    v3 = sound->field_60;
    v4 = v3 % bytesPerSec;
    result = v3 / bytesPerSec;
    if (v4 != 0) {
        result += 1;
    }

    return result;
}

// 0x49B284
int soundLoop(Sound* sound, int a2)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (a2) {
        sound->field_3C |= 0x20;
        sound->field_50 = a2;
    } else {
        sound->field_50 = 0;
        sound->field_58 = -1;
        sound->field_54 = 0;
        sound->field_3C &= ~(0x20);
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49B2EC
int soundVolumeHMItoDirectSound(int volume)
{
    double normalizedVolume;

    if (volume > VOLUME_MAX) {
        volume = VOLUME_MAX;
    }

    if (volume != 0) {
		//JASON was log2
        normalizedVolume = -1000.0 * log(32767.0 / volume);
        normalizedVolume = max(min(normalizedVolume, 0.0), -10000.0);
    } else {
        normalizedVolume = -10000.0;
    }

    return (int)normalizedVolume;
}

// 0x49B38C
int soundVolume(Sound* sound, int volume)
{
    int normalizedVolume;
    HRESULT hr;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    sound->volume = volume;

    if (sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_ERROR;
        return soundErrorno;
    }

    normalizedVolume = soundVolumeHMItoDirectSound(masterVol * volume / VOLUME_MAX);

    hr = IDirectSoundBuffer_SetVolume(sound->directSoundBuffer, normalizedVolume);
    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49B400
int soundGetVolume(Sound* sound)
{
    long volume;
    int v13;
    int v8;
    int diff;

    if (!deviceInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    IDirectSoundBuffer_GetVolume(sound->directSoundBuffer, &volume);

    if (volume == -10000) {
        v13 = 0;
    } else {
        // TODO: Check.
        volume = -volume;
        v13 = (int)(32767.0 / pow(2.0, (volume * 0.001)));
    }

    v8 = VOLUME_MAX * v13 / masterVol;
    diff = abs(v8 - sound->volume);
    if (diff > 20) {
        debug_printf("Actual sound volume differs significantly from noted volume actual %x stored %x, diff %d.", v8, sound->volume, diff);
    }

    return sound->volume;
}

// 0x49B570
int soundSetCallback(Sound* sound, SoundCallback* callback, void* userData)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    sound->callback = callback;
    sound->callbackUserData = userData;

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49B5AC
int soundSetChannel(Sound* sound, int channels)
{
    LPWAVEFORMATEX format;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (channels == 3) {
        format = sound->directSoundBufferDescription.lpwfxFormat;

        format->nBlockAlign = (2 * format->wBitsPerSample) / 8;
        format->nChannels = 2;
        format->nAvgBytesPerSec = format->nBlockAlign * sampleRate;
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49B630
int soundSetReadLimit(Sound* sound, int readLimit)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_DEVICE;
        return soundErrorno;
    }

    sound->readLimit = readLimit;

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49B664
int soundPause(Sound* sound)
{
    HRESULT hr;
    DWORD readPos;
    DWORD writePos;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (!(sound->field_40 & SOUND_FLAG_SOUND_IS_PLAYING)) {
        soundErrorno = SOUND_NOT_PLAYING;
        return soundErrorno;
    }

    if (sound->field_40 & SOUND_FLAG_SOUND_IS_PAUSED) {
        soundErrorno = SOUND_ALREADY_PAUSED;
        return soundErrorno;
    }

    hr = IDirectSoundBuffer_GetCurrentPosition(sound->directSoundBuffer, &readPos, &writePos);
    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    sound->field_48 = readPos;
    sound->field_40 |= SOUND_FLAG_SOUND_IS_PAUSED;

    return soundStop(sound);
}

// 0x49B770
int soundUnpause(Sound* sound)
{
    HRESULT hr;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL || sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if ((sound->field_40 & SOUND_FLAG_SOUND_IS_PLAYING) != 0) {
        soundErrorno = SOUND_NOT_PAUSED;
        return soundErrorno;
    }

    if (!(sound->field_40 & SOUND_FLAG_SOUND_IS_PAUSED)) {
        soundErrorno = SOUND_NOT_PAUSED;
        return soundErrorno;
    }

    hr = IDirectSoundBuffer_SetCurrentPosition(sound->directSoundBuffer, sound->field_48);
    if (hr != DS_OK) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    sound->field_40 &= ~SOUND_FLAG_SOUND_IS_PAUSED;
    sound->field_48 = 0;

    return soundPlay(sound);
}

// 0x49B87C
int soundSetFileIO(Sound* sound, SoundOpenProc* openProc, SoundCloseProc* closeProc, SoundReadProc* readProc, SoundWriteProc* writeProc, SoundSeekProc* seekProc, SoundTellProc* tellProc, SoundFileLengthProc* fileLengthProc)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (openProc != NULL) {
        sound->io.open = openProc;
    }

    if (closeProc != NULL) {
        sound->io.close = closeProc;
    }

    if (readProc != NULL) {
        sound->io.read = readProc;
    }

    if (writeProc != NULL) {
        sound->io.write = writeProc;
    }

    if (seekProc != NULL) {
        sound->io.seek = seekProc;
    }

    if (tellProc != NULL) {
        sound->io.tell = tellProc;
    }

    if (fileLengthProc != NULL) {
        sound->io.filelength = fileLengthProc;
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49B8F8
void soundMgrDelete(Sound* sound)
{
    FadeSound* curr;
    Sound* v10;
    Sound* v11;

    if (sound->field_40 & SOUND_FLAG_SOUND_IS_FADING) {
        curr = fadeHead;

        while (curr != NULL) {
            if (sound == curr->sound) {
                break;
            }

            curr = curr->next;
        }

        removeFadeSound(curr);
    }

    if (sound->directSoundBuffer != NULL) {
        // NOTE: Uninline.
        if (!soundPlaying(sound)) {
            soundStop(sound);
        }

        if (sound->callback != NULL) {
            sound->callback(sound->callbackUserData, 1);
        }

        IDirectSoundBuffer_Release(sound->directSoundBuffer);
        sound->directSoundBuffer = NULL;
    }

    if (sound->field_90 != NULL) {
        sound->field_90(sound->field_8C);
    }

    if (sound->field_20 != NULL) {
        freePtr(sound->field_20);
        sound->field_20 = NULL;
    }

    if (sound->directSoundBufferDescription.lpwfxFormat != NULL) {
        freePtr(sound->directSoundBufferDescription.lpwfxFormat);
    }

    v10 = sound->next;
    if (v10 != NULL) {
        v10->prev = sound->prev;
    }

    v11 = sound->prev;
    if (v11 != NULL) {
        v11->next = sound->next;
    } else {
        soundMgrList = sound->next;
    }

    freePtr(sound);
}

// 0x49BAF8
int soundSetMasterVolume(int volume)
{
	Sound* curr;

    if (volume < VOLUME_MIN || volume > VOLUME_MAX) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    masterVol = volume;

    curr = soundMgrList;
    while (curr != NULL) {
        soundVolume(curr, curr->volume);
        curr = curr->next;
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49BB48
static void CALLBACK doTimerEvent(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    void (*fn)();

    if (dwUser != 0) {
        fn = (void (*)())dwUser;
        fn();
    }
}

// 0x49BB94
static void removeTimedEvent(unsigned int* timerId)
{
    if (*timerId != -1) {
        timeKillEvent(*timerId);
        *timerId = -1;
    }
}

// 0x49BBB4
int soundGetPosition(Sound* sound)
{
    DWORD playPos;
    DWORD writePos;

    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    IDirectSoundBuffer_GetCurrentPosition(sound->directSoundBuffer, &playPos, &writePos);

    if ((sound->field_44 & 0x02) != 0) {
        if (playPos < sound->field_74) {
            playPos += sound->field_64 + sound->field_78 * sound->field_7C - sound->field_74;
        } else {
            playPos -= sound->field_74 + sound->field_64;
        }
    }

    return playPos;
}

// 0x49BC48
int soundSetPosition(Sound* sound, int a2)
{
    if (!driverInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (sound->directSoundBuffer == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    if (sound->field_44 & 0x02) {
		int bytes_read;
		int v17;

        int v6 = a2 / sound->field_7C % sound->field_78;

        IDirectSoundBuffer_SetCurrentPosition(sound->directSoundBuffer, v6 * sound->field_7C + a2 % sound->field_7C);

        sound->io.seek(sound->io.fd, v6 * sound->field_7C, SEEK_SET);
        bytes_read = sound->io.read(sound->io.fd, sound->field_20, sound->field_7C);
        if (bytes_read < sound->field_7C) {
            if (sound->field_44 & 0x02) {
                sound->io.seek(sound->io.fd, 0, SEEK_SET);
                sound->io.read(sound->io.fd, sound->field_20 + bytes_read, sound->field_7C - bytes_read);
            } else {
                memset(sound->field_20 + bytes_read, 0, sound->field_7C - bytes_read);
            }
        }

        v17 = v6 + 1;
        sound->field_64 = a2;

        if (v17 < sound->field_78) {
            sound->field_70 = v17;
        } else {
            sound->field_70 = 0;
        }

        soundContinue(sound);
    } else {
        IDirectSoundBuffer_SetCurrentPosition(sound->directSoundBuffer, a2);
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49BDAC
static void removeFadeSound(FadeSound* fadeSound)
{
    FadeSound* prev;
    FadeSound* next;
    FadeSound* tmp;

    if (fadeSound == NULL) {
        return;
    }

    if (fadeSound->sound == NULL) {
        return;
    }

    if (!(fadeSound->sound->field_40 & SOUND_FLAG_SOUND_IS_FADING)) {
        return;
    }

    prev = fadeSound->prev;
    if (prev != NULL) {
        prev->next = fadeSound->next;
    } else {
        fadeHead = fadeSound->next;
    }

    next = fadeSound->next;
    if (next != NULL) {
        next->prev = fadeSound->prev;
    }

    fadeSound->sound->field_40 &= ~SOUND_FLAG_SOUND_IS_FADING;
    fadeSound->sound = NULL;

    tmp = fadeFreeList;
    fadeFreeList = fadeSound;
    fadeSound->next = tmp;
}

// 0x49BE2C
static void fadeSounds()
{
    FadeSound* ptr;

    ptr = fadeHead;
    while (ptr != NULL) {
        if ((ptr->currentVolume > ptr->targetVolume || ptr->currentVolume + ptr->deltaVolume < ptr->targetVolume) && (ptr->currentVolume < ptr->targetVolume || ptr->currentVolume + ptr->deltaVolume > ptr->targetVolume)) {
            ptr->currentVolume += ptr->deltaVolume;
            soundVolume(ptr->sound, ptr->currentVolume);
        } else {
            if (ptr->targetVolume == 0) {
                if (ptr->field_14) {
                    soundPause(ptr->sound);
                    soundVolume(ptr->sound, ptr->initialVolume);
                } else {
                    if (ptr->sound->field_44 & 0x04) {
                        soundDelete(ptr->sound);
                    } else {
                        soundStop(ptr->sound);

                        ptr->initialVolume = ptr->targetVolume;
                        ptr->currentVolume = ptr->targetVolume;
                        ptr->deltaVolume = 0;

                        soundVolume(ptr->sound, ptr->targetVolume);
                    }
                }
            }

            removeFadeSound(ptr);
        }
    }

    if (fadeHead == NULL) {
        // NOTE: Uninline.
        removeTimedEvent(&fadeEventHandle);
    }
}

// 0x49BF04
static int internalSoundFade(Sound* sound, int duration, int targetVolume, int a4)
{
    FadeSound* ptr;
    bool v14;

    if (!deviceInit) {
        soundErrorno = SOUND_NOT_INITIALIZED;
        return soundErrorno;
    }

    if (sound == NULL) {
        soundErrorno = SOUND_NO_SOUND;
        return soundErrorno;
    }

    ptr = NULL;
    if (sound->field_40 & SOUND_FLAG_SOUND_IS_FADING) {
        ptr = fadeHead;
        while (ptr != NULL) {
            if (ptr->sound == sound) {
                break;
            }

            ptr = ptr->next;
        }
    }

    if (ptr == NULL) {
        if (fadeFreeList != NULL) {
            ptr = fadeFreeList;
            fadeFreeList = fadeFreeList->next;
        } else {
            ptr = (FadeSound*)mallocPtr(sizeof(FadeSound));
        }

        if (ptr != NULL) {
            if (fadeHead != NULL) {
                fadeHead->prev = ptr;
            }

            ptr->sound = sound;
            ptr->prev = NULL;
            ptr->next = fadeHead;
            fadeHead = ptr;
        }
    }

    if (ptr == NULL) {
        soundErrorno = SOUND_NO_MEMORY_AVAILABLE;
        return soundErrorno;
    }

    ptr->targetVolume = targetVolume;
    ptr->initialVolume = soundGetVolume(sound);
    ptr->currentVolume = ptr->initialVolume;
    ptr->field_14 = a4;
    // TODO: Check.
    ptr->deltaVolume = 8 * (125 * (targetVolume - ptr->initialVolume)) / (40 * duration);

    sound->field_40 |= SOUND_FLAG_SOUND_IS_FADING;

    if (driverInit) {
        if (sound->directSoundBuffer != NULL) {
            v14 = (sound->field_40 & SOUND_FLAG_SOUND_IS_PLAYING) == 0;
        } else {
            soundErrorno = SOUND_NO_SOUND;
            v14 = true;
        }
    } else {
        soundErrorno = SOUND_NOT_INITIALIZED;
        v14 = true;
    }

    if (v14) {
        soundPlay(sound);
    }

    if (fadeEventHandle != -1) {
        soundErrorno = SOUND_NO_ERROR;
        return soundErrorno;
    }

    fadeEventHandle = timeSetEvent(40, 10, doTimerEvent, (DWORD_PTR)fadeSounds, 1);
    if (fadeEventHandle == 0) {
        soundErrorno = SOUND_UNKNOWN_ERROR;
        return soundErrorno;
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}

// 0x49C088
int soundFade(Sound* sound, int duration, int targetVolume)
{
    return internalSoundFade(sound, duration, targetVolume, 0);
}

// 0x49C0D0
void soundFlushAllSounds()
{
    while (soundMgrList != NULL) {
        soundDelete(soundMgrList);
    }
}

// 0x49C15C
void soundUpdate()
{
    Sound* curr = soundMgrList;
    while (curr != NULL) {
        // Sound can be deallocated in `soundContinue`.
        Sound* next = curr->next;
        soundContinue(curr);
        curr = next;
    }
}

// 0x49C17C
int soundSetDefaultFileIO(SoundOpenProc* openProc, SoundCloseProc* closeProc, SoundReadProc* readProc, SoundWriteProc* writeProc, SoundSeekProc* seekProc, SoundTellProc* tellProc, SoundFileLengthProc* fileLengthProc)
{
    if (openProc != NULL) {
        defaultStream.open = openProc;
    }

    if (closeProc != NULL) {
        defaultStream.close = closeProc;
    }

    if (readProc != NULL) {
        defaultStream.read = readProc;
    }

    if (writeProc != NULL) {
        defaultStream.write = writeProc;
    }

    if (seekProc != NULL) {
        defaultStream.seek = seekProc;
    }

    if (tellProc != NULL) {
        defaultStream.tell = tellProc;
    }

    if (fileLengthProc != NULL) {
        defaultStream.filelength = fileLengthProc;
    }

    soundErrorno = SOUND_NO_ERROR;
    return soundErrorno;
}
