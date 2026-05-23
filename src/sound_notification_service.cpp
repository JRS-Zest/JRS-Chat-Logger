#include "sound_notification_service.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#undef PlaySound
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "winmm.lib")

// C# SoundNotificationService.cs の完全移植
// NAudio 相当の音量制御を waveOut API で実現

namespace fs = std::filesystem;

SoundNotificationService::SoundNotificationService(const std::wstring& basePath) {
    std::wstring base = basePath;
    if (base.empty()) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        base = fs::path(exePath).parent_path().wstring();
    }
    soundsFolder_ = (fs::path(base) / L"sounds").wstring();
}

SoundNotificationService::~SoundNotificationService() {
    Shutdown();
}

void SoundNotificationService::Shutdown() {
    if (disposed_) return;
    disposed_ = true;
    running_ = false;
    InterruptPlayback();
    playCv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void SoundNotificationService::StartBackgroundLoad() {
    // バックグラウンドでWAVファイルを読み込み
    std::thread([this]() {
        try { LoadSounds(); } catch (...) {}
    }).detach();

    // 専用再生ワーカー起動
    running_ = true;
    workerThread_ = std::thread(&SoundNotificationService::WorkerLoop, this);
}

static std::vector<uint8_t> ReadFileBytes(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

void SoundNotificationService::LoadSounds() {
    if (!fs::exists(soundsFolder_)) {
        try { fs::create_directories(soundsFolder_); } catch (...) {}
    }

    auto tryLoad = [&](const std::wstring& filename) -> std::wstring {
        auto path = (fs::path(soundsFolder_) / filename).wstring();
        if (fs::exists(path)) {
            auto data = ReadFileBytes(path);
            if (!data.empty()) {
                std::lock_guard<std::mutex> lk(cacheMutex_);
                wavCache_[path] = std::move(data);
            }
            return path;
        }
        return L"";
    };

    itemSoundPath_ = tryLoad(L"item.wav");
    invFullSoundPath_ = tryLoad(L"inv_full.wav");
    whisperSoundPath_ = tryLoad(L"whisper.wav");
    optionSoundPath_ = tryLoad(L"option.wav");
    bellSoundPath_ = tryLoad(L"bell.wav");
    dropSoundPath_ = tryLoad(L"Ultimate.wav");
}

// WAVデータにボリュームを適用（16bit PCM前提）
static void ApplyVolume(std::vector<uint8_t>& wavData, float vol) {
    if (std::abs(vol - 1.0f) < 0.001f) return;
    if (wavData.size() < 44) return; // WAVヘッダ最小サイズ

    // RIFFヘッダを検証
    if (wavData[0] != 'R' || wavData[1] != 'I' || wavData[2] != 'F' || wavData[3] != 'F') return;

    // "fmt " チャンクを探して bitsPerSample を取得
    uint16_t bitsPerSample = 0;
    for (size_t i = 12; i + 8 < wavData.size(); ) {
        uint32_t chunkSize = *reinterpret_cast<uint32_t*>(&wavData[i + 4]);
        if (wavData[i] == 'f' && wavData[i+1] == 'm' && wavData[i+2] == 't' && wavData[i+3] == ' ') {
            // fmt チャンク内のオフセット14が bitsPerSample (i+8 がfmtデータ先頭)
            if (i + 8 + 16 <= wavData.size()) {
                bitsPerSample = *reinterpret_cast<uint16_t*>(&wavData[i + 8 + 14]);
            }
            break;
        }
        i += 8 + chunkSize;
        if (chunkSize % 2 != 0) i++; // パディング
    }
    if (bitsPerSample == 0) return;

    // "data" チャンクを探す
    size_t dataOffset = 0;
    size_t dataSize = 0;
    for (size_t i = 12; i + 8 < wavData.size(); ) {
        uint32_t chunkSize = *reinterpret_cast<uint32_t*>(&wavData[i + 4]);
        if (wavData[i] == 'd' && wavData[i+1] == 'a' && wavData[i+2] == 't' && wavData[i+3] == 'a') {
            dataSize = chunkSize;
            dataOffset = i + 8;
            break;
        }
        i += 8 + chunkSize;
        if (chunkSize % 2 != 0) i++; // パディング
    }
    if (dataOffset == 0 || dataOffset + dataSize > wavData.size()) return;

    // ボリューム適用
    if (bitsPerSample == 16) {
        auto* samples = reinterpret_cast<int16_t*>(&wavData[dataOffset]);
        size_t numSamples = dataSize / 2;
        for (size_t i = 0; i < numSamples; i++) {
            float s = static_cast<float>(samples[i]) * vol;
            s = std::clamp(s, -32768.0f, 32767.0f);
            samples[i] = static_cast<int16_t>(s);
        }
    } else if (bitsPerSample == 8) {
        for (size_t i = 0; i < dataSize; i++) {
            float s = (static_cast<float>(wavData[dataOffset + i]) - 128.0f) * vol + 128.0f;
            s = std::clamp(s, 0.0f, 255.0f);
            wavData[dataOffset + i] = static_cast<uint8_t>(s);
        }
    }
}

void SoundNotificationService::PlaySound(const std::wstring& soundPath) {
    if (soundPath.empty()) return;
    if (volume <= 0.001f) return;

    std::vector<uint8_t> cached;
    {
        std::lock_guard<std::mutex> lk(cacheMutex_);
        auto it = wavCache_.find(soundPath);
        if (it != wavCache_.end()) {
            cached = it->second; // コピー（ボリューム適用のため）
        }
    }

    if (cached.empty()) {
        // キャッシュミス: ファイルから読み込み
        cached = ReadFileBytes(soundPath);
        if (cached.empty()) return;
        {
            std::lock_guard<std::mutex> lk(cacheMutex_);
            wavCache_[soundPath] = cached; // キャッシュに追加
        }
    }

    // ボリューム適用
    ApplyVolume(cached, volume);

    // latest-onlyポリシーでワーカーに渡す
    {
        std::lock_guard<std::mutex> lk(latestMutex_);
        latestBuffer_ = std::move(cached);
        hasNewBuffer_ = true;
    }
    // 再生中の音を即時停止
    InterruptPlayback();
    playCv_.notify_one();
}

// WAVバッファからfmt/dataチャンクを解析してwaveOut APIで再生する
bool SoundNotificationService::PlayWavBuffer(const std::vector<uint8_t>& wav) {
    if (wav.size() < 44) return false;
    if (wav[0] != 'R' || wav[1] != 'I' || wav[2] != 'F' || wav[3] != 'F') return false;

    // fmt チャンク探索
    WAVEFORMATEX wfx{};
    bool fmtFound = false;
    for (size_t i = 12; i + 8 < wav.size(); ) {
        uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(&wav[i + 4]);
        if (wav[i] == 'f' && wav[i+1] == 'm' && wav[i+2] == 't' && wav[i+3] == ' ') {
            if (i + 8 + 16 <= wav.size()) {
                const uint8_t* f = &wav[i + 8];
                wfx.wFormatTag      = *reinterpret_cast<const uint16_t*>(f + 0);
                wfx.nChannels       = *reinterpret_cast<const uint16_t*>(f + 2);
                wfx.nSamplesPerSec  = *reinterpret_cast<const uint32_t*>(f + 4);
                wfx.nAvgBytesPerSec = *reinterpret_cast<const uint32_t*>(f + 8);
                wfx.nBlockAlign     = *reinterpret_cast<const uint16_t*>(f + 12);
                wfx.wBitsPerSample  = *reinterpret_cast<const uint16_t*>(f + 14);
                wfx.cbSize = 0;
                fmtFound = true;
            }
            break;
        }
        i += 8 + chunkSize;
        if (chunkSize % 2 != 0) i++;
    }
    if (!fmtFound || wfx.wFormatTag != WAVE_FORMAT_PCM) return false;

    // data チャンク探索
    size_t dataOffset = 0;
    uint32_t dataSize = 0;
    for (size_t i = 12; i + 8 < wav.size(); ) {
        uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(&wav[i + 4]);
        if (wav[i] == 'd' && wav[i+1] == 'a' && wav[i+2] == 't' && wav[i+3] == 'a') {
            dataOffset = i + 8;
            dataSize = chunkSize;
            break;
        }
        i += 8 + chunkSize;
        if (chunkSize % 2 != 0) i++;
    }
    if (dataOffset == 0 || dataOffset + dataSize > wav.size()) return false;

    // waveOut で再生
    HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!hEvent) return false;

    HWAVEOUT hwo = nullptr;
    MMRESULT mr = waveOutOpen(&hwo, WAVE_MAPPER, &wfx, (DWORD_PTR)hEvent, 0, CALLBACK_EVENT);
    if (mr != MMSYSERR_NOERROR) {
        CloseHandle(hEvent);
        return false;
    }

    // waveOutOpen が WOM_OPEN でイベントをシグナルするのでリセット
    ResetEvent(hEvent);

    // 即時停止用にハンドルを保存
    {
        std::lock_guard<std::mutex> lk(hwoMutex_);
        currentHwo_ = hwo;
    }

    WAVEHDR hdr{};
    hdr.lpData = (LPSTR)&wav[dataOffset];
    hdr.dwBufferLength = dataSize;
    waveOutPrepareHeader(hwo, &hdr, sizeof(hdr));
    waveOutWrite(hwo, &hdr, sizeof(hdr));

    // WOM_DONE を待機（最大10秒、または InterruptPlayback で即時復帰）
    WaitForSingleObject(hEvent, 10000);

    // ハンドルをクリア
    {
        std::lock_guard<std::mutex> lk(hwoMutex_);
        currentHwo_ = nullptr;
    }

    waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr));
    waveOutClose(hwo);
    CloseHandle(hEvent);
    return true;
}

void SoundNotificationService::WorkerLoop() {
    while (running_) {
        std::vector<uint8_t> buf;
        {
            std::unique_lock<std::mutex> lk(latestMutex_);
            playCv_.wait_for(lk, std::chrono::milliseconds(100), [this]{
                return hasNewBuffer_ || !running_;
            });
            if (!running_) break;
            if (!hasNewBuffer_) continue;
            buf = std::move(latestBuffer_);
            hasNewBuffer_ = false;
        }

        if (buf.empty()) continue;

        try {
            PlayWavBuffer(buf);
        } catch (...) {}
    }
}

// 再生中の音を即時停止（waveOutReset で WOM_DONE をトリガーし WaitForSingleObject を解放する）
void SoundNotificationService::InterruptPlayback() {
    std::lock_guard<std::mutex> lk(hwoMutex_);
    if (currentHwo_) {
        waveOutReset((HWAVEOUT)currentHwo_);
    }
}

void SoundNotificationService::PlayItemSound() {
    if (!isItemSoundEnabled) return;
    PlaySound(itemSoundPath_);
}

void SoundNotificationService::PlayOptionSound() {
    if (!isOptionSoundEnabled) return;
    PlaySound(optionSoundPath_);
}

void SoundNotificationService::PlayInventoryFullSound() {
    if (!isInventoryFullSoundEnabled) return;
    PlaySound(invFullSoundPath_);
}

void SoundNotificationService::PlayWhisperSound() {
    if (!isWhisperSoundEnabled) return;
    PlaySound(whisperSoundPath_);
}

void SoundNotificationService::PlayBellSound() {
    if (!isBellSoundEnabled) return;
    PlaySound(bellSoundPath_);
}

void SoundNotificationService::PlayDropSound() {
    if (!isDropSoundEnabled) return;
    PlaySound(dropSoundPath_);
}
