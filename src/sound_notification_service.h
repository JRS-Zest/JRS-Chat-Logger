#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>

// C# SoundNotificationService.cs の完全移植
// NAudio の代わりに Windows waveOut API で音量制御付き再生

class SoundNotificationService {
public:
    SoundNotificationService(const std::wstring& basePath = L"");
    ~SoundNotificationService();

    // バックグラウンド読み込み開始（フォーム表示後に呼ぶ）
    void StartBackgroundLoad();

    // 各種サウンド再生
    void PlayItemSound();
    void PlayOptionSound();
    void PlayInventoryFullSound();
    void PlayWhisperSound();
    void PlayBellSound();
    void PlayDropSound();

    // サウンド有効/無効
    bool isItemSoundEnabled = true;
    bool isInventoryFullSoundEnabled = true;
    bool isWhisperSoundEnabled = true;
    bool isBellSoundEnabled = true;
    bool isOptionSoundEnabled = true;
    bool isDropSoundEnabled = true;

    // 音量 (0.0 - 1.0)
    float volume = 0.5f;

    // サウンドファイル存在チェック
    bool HasItemSound() const { return !itemSoundPath_.empty(); }
    bool HasInventoryFullSound() const { return !invFullSoundPath_.empty(); }
    bool HasWhisperSound() const { return !whisperSoundPath_.empty(); }
    bool HasBellSound() const { return !bellSoundPath_.empty(); }
    bool HasOptionSound() const { return !optionSoundPath_.empty(); }
    bool HasDropSound() const { return !dropSoundPath_.empty(); }

    void Shutdown();

private:
    void LoadSounds();
    void PlaySound(const std::wstring& soundPath);
    void WorkerLoop();
    bool PlayWavBuffer(const std::vector<uint8_t>& wav);
    void InterruptPlayback();

    // WAVキャッシュ（キー: ファイルパス, 値: バイトデータ）
    std::unordered_map<std::wstring, std::vector<uint8_t>> wavCache_;
    std::mutex cacheMutex_;

    std::wstring soundsFolder_;
    std::wstring itemSoundPath_;
    std::wstring invFullSoundPath_;
    std::wstring whisperSoundPath_;
    std::wstring bellSoundPath_;
    std::wstring optionSoundPath_;
    std::wstring dropSoundPath_;

    // 専用再生ワーカー（latest-onlyポリシー）
    std::thread workerThread_;
    std::mutex latestMutex_;
    std::condition_variable playCv_;
    std::vector<uint8_t> latestBuffer_;
    bool hasNewBuffer_ = false;
    std::atomic<bool> running_{false};
    bool disposed_ = false;

    // 現在再生中の waveOut ハンドル（即時停止用）
    void* currentHwo_ = nullptr;
    std::mutex hwoMutex_;
};
