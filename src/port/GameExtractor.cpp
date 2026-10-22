#ifdef _WIN32
#include <Windows.h>
#include <winuser.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#endif
#include "port/build.h"
#include "GameExtractor.h"
#include <cstdio>
#include <cstring>
#include <array>
#include <unordered_map>
#include <fstream>
#include <algorithm>

#include "ship/Context.h"
#include "spdlog/spdlog.h"
#include <port/Engine.h>

#include "Companion.h"
#if !defined(__IOS__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__WINRT__)
#include "portable-file-dialogs.h"
#endif

// Defined unconditionally (declared in the header for all platforms).
std::string GameExtractor::sLastError;

namespace {

constexpr uint32_t kDvdMagic = 0xC2339F3D;
constexpr uint32_t kGczMagic = 0xB10BC001;
constexpr uint64_t kGczUncompressedFlag = 1ULL << 63;
constexpr size_t kCisoHeaderSize = 0x8000;
constexpr size_t kDiscHeaderSize = 0x20;

struct SupportedDisc {
    const char* gameId;
    uint8_t revision;
    const char* name;
};

const SupportedDisc kSupportedDiscs[] = {
    { "GAFE01", 0, "Animal Crossing (USA)" },
};

const std::unordered_map<std::string, std::string> kKnownUnsupportedDiscs = {
    { "GAFJ01", "Doubutsu no Mori+ (Japan)" },
    { "GAFP01", "Animal Crossing (Europe)" },
    { "GAFU01", "Animal Crossing (Australia)" },
};

const char* const kDiscExtensions[] = { ".iso", ".gcm", ".ciso", ".gcz" };

std::filesystem::path Utf8ToFsPath(const std::string& utf8) {
    return std::filesystem::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
}

uint32_t ReadBE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint32_t ReadLE32(const uint8_t* p) {
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

uint64_t ReadLE64(const uint8_t* p) {
    return ((uint64_t)ReadLE32(p + 4) << 32) | ReadLE32(p);
}

bool ReadAt(std::ifstream& in, uint64_t offset, uint8_t* out, size_t size) {
    in.seekg((std::streamoff)offset, std::ios::beg);
    in.read(reinterpret_cast<char*>(out), (std::streamsize)size);
    return (size_t)in.gcount() == size;
}

bool IsDiscExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    for (const char* known : kDiscExtensions) {
        if (ext == known) {
            return true;
        }
    }
    return false;
}

enum class HeaderResult { Ok, Unchecked, Invalid };

HeaderResult ReadImageHeader(const std::filesystem::path& path, std::array<uint8_t, kDiscHeaderSize>& header) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return HeaderResult::Invalid;
    }
    std::array<uint8_t, 0x20> lead{};
    if (!ReadAt(in, 0, lead.data(), lead.size())) {
        return HeaderResult::Invalid;
    }

    if (std::memcmp(lead.data(), "CISO", 4) == 0) {
        uint8_t present = 0;
        if (!ReadAt(in, 8, &present, 1) || present == 0) {
            return HeaderResult::Invalid;
        }
        return ReadAt(in, kCisoHeaderSize, header.data(), header.size()) ? HeaderResult::Ok : HeaderResult::Invalid;
    }

    if (ReadLE32(lead.data()) == kGczMagic) {
        const uint32_t numBlocks = ReadLE32(lead.data() + 0x1C);
        if (numBlocks == 0) {
            return HeaderResult::Invalid;
        }
        uint8_t pointerBytes[8];
        if (!ReadAt(in, 0x20, pointerBytes, sizeof(pointerBytes))) {
            return HeaderResult::Invalid;
        }
        const uint64_t firstBlock = ReadLE64(pointerBytes);
        if ((firstBlock & kGczUncompressedFlag) == 0) {
            return HeaderResult::Unchecked;
        }
        const uint64_t dataStart = 0x20 + (uint64_t)numBlocks * 8 + (uint64_t)numBlocks * 4;
        return ReadAt(in, dataStart + (firstBlock & ~kGczUncompressedFlag), header.data(), header.size())
                   ? HeaderResult::Ok
                   : HeaderResult::Invalid;
    }

    std::copy(lead.begin(), lead.end(), header.begin());
    return HeaderResult::Ok;
}

std::optional<std::string> IdentifyHeader(const std::array<uint8_t, kDiscHeaderSize>& header) {
    if (ReadBE32(header.data() + 0x1C) != kDvdMagic) {
        GameExtractor::sLastError = "Not a GameCube disc (missing the disc header magic).";
        return std::nullopt;
    }
    const std::string gameId(reinterpret_cast<const char*>(header.data()), 6);
    const uint8_t revision = header[7];
    for (const auto& disc : kSupportedDiscs) {
        if (gameId == disc.gameId && revision == disc.revision) {
            return std::string(disc.name);
        }
    }
    const auto known = kKnownUnsupportedDiscs.find(gameId);
    if (known != kKnownUnsupportedDiscs.end()) {
        GameExtractor::sLastError = known->second + " (" + gameId + ") is not supported yet. Only GAFE01 rev 0 is.";
    } else if (gameId == "GAFE01") {
        GameExtractor::sLastError = "GAFE01 revision " + std::to_string(revision) + " is not supported. Only rev 0 is.";
    } else {
        GameExtractor::sLastError = "Disc " + gameId + " is not Animal Crossing.";
    }
    return std::nullopt;
}

} // namespace

bool GameExtractor::IsDiscInputAvailable() {
#ifdef TORCH_GAMECUBE_SUPPORT
    return true;
#else
    return false;
#endif
}

std::string GameExtractor::GetCurrentAssetName() {
    return Companion::Instance != nullptr ? Companion::Instance->GetCurrentAssetName() : std::string();
}

std::optional<std::string> GameExtractor::DetectVersion(const std::string& discPath) {
    std::error_code ec;
    const std::filesystem::path path = Utf8ToFsPath(discPath);
    std::array<uint8_t, kDiscHeaderSize> header{};

    if (std::filesystem::is_directory(path, ec)) {
        if (!std::filesystem::exists(path / "sys" / "main.dol", ec)) {
            sLastError = "The folder has no sys/main.dol. Select the root of an extracted disc.";
            return std::nullopt;
        }
        std::ifstream boot(path / "sys" / "boot.bin", std::ios::binary);
        if (!boot.is_open() || !ReadAt(boot, 0, header.data(), header.size())) {
            return std::string("Extracted GameCube disc (no sys/boot.bin, not verified)");
        }
        return IdentifyHeader(header);
    }

    if (!std::filesystem::is_regular_file(path, ec)) {
        sLastError = "File not found: " + discPath;
        return std::nullopt;
    }
    switch (ReadImageHeader(path, header)) {
        case HeaderResult::Ok:
            return IdentifyHeader(header);
        case HeaderResult::Unchecked:
            return std::string("GameCube disc (compressed GCZ header, not verified)");
        default:
            sLastError = "Could not read a GameCube disc header from " + discPath;
            return std::nullopt;
    }
}

bool GameExtractor::RunStandalone(std::string disc) {
    sLastError.clear();
    const auto version = DetectVersion(disc);
    if (!version.has_value()) {
        SPDLOG_INFO("Not a supported disc: {} ({})", disc, sLastError);
        return false;
    }
    SPDLOG_INFO("Selected disc {}: {}", disc, *version);
    this->mGamePath = Utf8ToFsPath(disc);
    return true;
}

bool GameExtractor::SelectGameFromUI(bool pickFolder) {
    sLastError.clear();
#if !defined(__IOS__) && !defined(__ANDROID__) && !defined(__SWITCH__) && !defined(__EMSCRIPTEN__) && \
    !defined(__WINRT__)
    if (!pfd::settings::available()) {
        SPDLOG_ERROR("portable-file-dialogs is not available on this system.");
        sLastError = "No file dialog is available. Pass the disc path on the command line instead.";
        return false;
    }

    std::string discPath;
    if (pickFolder) {
        discPath = pfd::select_folder("Select an extracted Animal Crossing disc folder", ".").result();
    } else {
        auto selection = pfd::open_file("Select an Animal Crossing disc image", ".",
                                        { "GameCube disc images", "*.iso *.gcm *.ciso *.gcz", "All files", "*" })
                             .result();
        if (!selection.empty()) {
            discPath = selection[0];
        }
    }
    if (discPath.empty()) {
        return false;
    }
    return RunStandalone(discPath);
#else
    return false;
#endif
}

std::vector<std::pair<std::string, std::string>>
GameExtractor::FindSupportedDiscs(const std::vector<std::string>& searchPaths) {
    std::vector<std::pair<std::string, std::string>> out;
    std::vector<std::string> seenVersions;
    for (const auto& dir : searchPaths) {
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(Utf8ToFsPath(dir), ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(Utf8ToFsPath(dir), ec)) {
            if (entry.is_directory() || !IsDiscExtension(entry.path())) {
                continue;
            }
            const std::string full = entry.path().generic_string();
            const auto version = DetectVersion(full);
            if (!version.has_value()) {
                continue;
            }
            if (std::find(seenVersions.begin(), seenVersions.end(), *version) != seenVersions.end()) {
                continue;
            }
            seenVersions.push_back(*version);
            out.emplace_back(full, *version);
        }
    }
    sLastError.clear();
    return out;
}

void GameExtractor::WritePortVersion() {
    Companion::Instance->SetVersion(std::to_string(gBuildVersionMajor) + "." + std::to_string(gBuildVersionMinor) +
                                    "." + std::to_string(gBuildVersionPatch));
}

std::string GameExtractor::GetDiscPath() {
    return mGamePath.generic_string();
}

bool GameExtractor::RunTorch(std::atomic<size_t>& assetCount, const std::string& assetsPath,
                             const std::string& gamePath, bool process) {
    if (!IsDiscInputAvailable()) {
        sLastError = "GameCube disc input is not implemented in this build's Torch yet.";
        SPDLOG_ERROR("{}", sLastError);
        return false;
    }

    Companion::Instance = new Companion(this->mGamePath, ArchiveType::O2R, false, assetsPath, gamePath);
    if (process) {
        this->WritePortVersion();
    } else {
        Companion::Instance->SetProcess(false);
    }
    try {
        Companion::Instance->Init(ExportType::Binary, assetCount, process);
#ifdef __EMSCRIPTEN__
        Companion::Instance->Process(assetCount);
#endif
    } catch (const std::exception& e) {
        sLastError = e.what();
        SPDLOG_ERROR("Failed to process O2R: {}", e.what());
        return false;
    }

    return true;
}

bool GameExtractor::Parse(std::atomic<size_t>& totalAssets, std::string appShortName) {
    sLastError.clear();
    return RunTorch(totalAssets, Ship::Context::GetAppBundlePath(), Ship::Context::GetAppDirectoryPath(appShortName),
                    false);
}

bool GameExtractor::GenerateOTR(std::string appShortName) {
    std::atomic<size_t> assetCount{ 0 };
    return GenerateOTR(assetCount, appShortName);
}

bool GameExtractor::GenerateOTR(std::atomic<size_t>& assetCount, std::string appShortName) {
    return GenerateOTRTo(assetCount, Ship::Context::GetAppBundlePath(),
                         Ship::Context::GetAppDirectoryPath(appShortName));
}

bool GameExtractor::GenerateOTRTo(std::atomic<size_t>& assetCount, const std::string& assetsPath,
                                  const std::string& gamePath) {
    return RunTorch(assetCount, assetsPath, gamePath, true);
}
