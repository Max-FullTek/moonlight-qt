#include "../session.h"
#include "audiostream.h"

int Session::getAudioRendererCapabilities(int /* audioConfiguration */)
{
    return AudioStream::capabilities();
}

bool Session::testAudio(int audioConfiguration)
{
    return AudioStream::testConfiguration(audioConfiguration);
}

int Session::arInit(int, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void*, int)
{
    s_ActiveSession->m_AudioStream.initialize(*opusConfig);
    return 0;
}

void Session::arCleanup()
{
    s_ActiveSession->m_AudioStream.cleanup();
}

void Session::arDecodeAndPlaySample(char* sampleData, int sampleLength)
{
    s_ActiveSession->m_AudioStream.decodeAndPlay(sampleData, sampleLength);
}
