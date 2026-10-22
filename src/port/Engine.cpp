#include "Engine.h"
#include <ship/window/gui/IconsFontAwesome4.h>
#include "ShipCompat.h"
#include "ui/PortGui.hpp"
#include "GameExtractor.h"
#include "Companion.h"
#include "ShipInit.hpp"
#include "port/build.h"
#include "port/importer/ResourceType.h"
#include "port/importer/AssetArrayFactory.h"
#include "port/importer/GenericArrayFactory.h"
#include "port/importer/RawTextureFactory.h"
#include "port/importer/KeyFrameFactory.h"
#include "port/audio/ACAudio.h"
#if __has_include("port/importer/ac/AcImporters.h")
#include "port/importer/ac/AcImporters.h"
#define AC_HAS_IMPORTERS 1
#endif
#include "port/interpolation/FrameInterpolation.h"
#include "audio/GameAudio.h"
#include "port/ui/cvar_prefixes.h"
#include "port/ui/Notification.h"
#include "port/ModList.h"
#include "port/net/SatellaClient.h"
#include "port/events/Events.h"
#include "port/console/DevConsole.h"
#include "port/input/GcButtons.h"
#include <libultraship/controller/controldevice/controller/mapping/ControllerDefaultMappings.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <SDL2/SDL.h>
#include <filesystem>
#include <fstream>

#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#if defined(ENABLE_SCRIPTING) && defined(__linux__)
#include <dlfcn.h>
#endif

#include <fast/resource/ResourceType.h>
#include <ship/window/gui/Fonts.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/MatrixFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include <fast/resource/factory/LightFactory.h>
#include <ship/resource/factory/BlobFactory.h>
#include <ship/utils/StringHelper.h>
#include <ship/resource/ResourceType.h>
#include <ship/window/gui/resource/Font.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/O2rArchive.h>
#include "controller/controldeck/ControlDeck.h"

#include <ship/log/Logger.h>
#include <ship/config/Config.h>
#include <ship/config/ConsoleVariable.h>
#include <ship/thread/ThreadPool.h>
#include <ship/debug/Console.h>
#include <ship/debug/CrashHandler.h>
#include <ship/events/Events.h>
#include <ship/window/FileDrop.h>
#include <ship/audio/Audio.h>
#include <fast/debug/GfxDebugger.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <libultraship/bridge/windowbridge.h>
#include <libultraship/bridge/controllerbridge.h>
#include <libultraship/bridge/eventsbridge.h>
#include <libultraship/bridge/audiobridge.h>
#include <libultraship/bridge/crashhandlerbridge.h>
#include <libultraship/bridge/gfxdebuggerbridge.h>
#include <libultraship/bridge/gfxbridge.h>
#ifdef ENABLE_SCRIPTING
#include <libultraship/bridge/scriptingbridge.h>
#include <ship/resource/archive/Archive.h>
#include <ship/scripting/ScriptLoader.h>
#include <ship/security/Keystore.h>
#include "port/api/ModOwner.hpp"
#include "port/api/api.h"
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "port/web/WebUtils.h"
#endif

#define NATIVE_SCREEN_WIDTH 640
#define NATIVE_SCREEN_HEIGHT 480

#define GAME_VERSION_GAFE01_00 0x2AE8F56E

const float imguiScaleOptionToValue[4] = { 0.75f, 1.0f, 1.5f, 2.0f };
std::shared_ptr<Fast::Fast3dWindow> gsFast3dWindow;
#if defined(__IOS__) || defined(__ANDROID__)
const uint32_t defaultImGuiScale = 3;
#else
const uint32_t defaultImGuiScale = 1;
#endif
#if defined(__ANDROID__)
const float initialImGuiScale = 2.0f;
#else
const float initialImGuiScale = 1.0f;
#endif
int32_t previousImGuiScaleIndex = -1;
float previousImGuiScale = initialImGuiScale;
bool portArchiveVersionMatch = false;
std::string assets_path;
static bool sGameArchiveLoaded = false;
static uint32_t sFrameDivisor = 1;

namespace fs = std::filesystem;

extern "C" {
bool prevAltAssets = false;
extern int32_t gAC_PrerenderFb;
}

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
} OTRVersion;

GameEngine* GameEngine::Instance;

namespace {
void WaitForPresentSlot(int fps);
#ifdef __EMSCRIPTEN__
constexpr const char* kWebDiscDir = "/tmp/disc";
bool sWebYielded = false;
#endif
} // namespace

// Read the port version from an OTR file
OTRVersion ReadPortVersionFromOTR(std::string otrPath) {
    OTRVersion version = {};

    // Use a temporary archive instance to load the otr and read the version file
    auto archive = std::make_shared<Ship::O2rArchive>(otrPath);
    if (archive->Open()) {
        auto t = archive->LoadFile("portVersion");
        if (t != nullptr && t->IsLoaded) {
            auto stream = std::make_shared<Ship::MemoryStream>(t->Buffer->data(), t->Buffer->size());
            auto reader = std::make_shared<Ship::BinaryReader>(stream);
            if (t->Buffer->size() == 7) {
                reader->SetEndianness(static_cast<Ship::Endianness>(reader->ReadUByte()));
            } else {
                reader->SetEndianness(Ship::Endianness::Big);
            }
            version.major = reader->ReadUInt16();
            version.minor = reader->ReadUInt16();
            version.patch = reader->ReadUInt16();
        } else {
            SPDLOG_WARN("Failed to read portVersion file from O2R: {}", otrPath);
        }
    } else {
        SPDLOG_WARN("Failed to open O2R for version reading: {}", otrPath);
    }

    return version;
}

// Checks the program version stored in the otr and compares the major value to soh
// For Windows/Mac/Linux if the version doesn't match, offer to
OTRVersion DetectOTRVersion(std::string fileName) {
    bool isOtrOld = false;
    std::string otrPath = Ship::Context::LocateFileAcrossAppDirs(fileName);

    // Doesn't exist so nothing to do here
    if (!std::filesystem::exists(otrPath)) {
        SPDLOG_WARN("O2R file not found at path: {}", otrPath);
        return { INT16_MAX, INT16_MAX, INT16_MAX };
    }

    return ReadPortVersionFromOTR(otrPath);
}

bool VerifyArchiveVersion(OTRVersion version) {
    return version.major == gBuildVersionMajor && version.minor == gBuildVersionMinor;
}

// sends the user back through extraction.
static bool IsArchiveGameVersionUnknown(const std::string& otrPath) {
    auto archive = std::make_shared<Ship::O2rArchive>(otrPath);
    if (!archive->Open()) {
        return false;
    }
    auto t = archive->LoadFile("version");
    if (t == nullptr || !t->IsLoaded || t->Buffer->size() < 5) {
        return false;
    }
    auto stream = std::make_shared<Ship::MemoryStream>(t->Buffer->data(), t->Buffer->size());
    auto reader = std::make_shared<Ship::BinaryReader>(stream);
    reader->SetEndianness(static_cast<Ship::Endianness>(reader->ReadUByte()));
    const uint32_t raw = reader->ReadUInt32();
    const uint32_t swapped = BSWAP32(raw);
    return raw != 0 && raw != GAME_VERSION_GAFE01_00 && swapped != GAME_VERSION_GAFE01_00;
}

bool GameEngine_IsGameArchiveCurrent(const std::string& path) {
    const OTRVersion version = ReadPortVersionFromOTR(path);
    const bool unversioned = version.major == 0 && version.minor == 0 && version.patch == 0;
    return (unversioned || VerifyArchiveVersion(version)) && !IsArchiveGameVersionUnknown(path);
}

static bool ExtractorDataExists(const std::string& installPath) {
    return std::filesystem::exists(installPath + "/config.yml") &&
           std::filesystem::is_directory(installPath + "/assets/ymls");
}

static void AddMissingGcButtonMappings(IEvent*) {
    static bool sDone = false;
    auto controlDeck = ShipCompat::GetControlDeck();
    if (sDone || controlDeck == nullptr || !controlDeck->IsInitialized()) {
        return;
    }
    sDone = true;

    auto controller = controlDeck->GetControllerByPort(0);
    auto x = controller->GetButton(BTN_X);
    auto y = controller->GetButton(BTN_Y);
    if (x == nullptr || y == nullptr || !x->GetAllButtonMappings().empty() || !y->GetAllButtonMappings().empty()) {
        return;
    }
    for (auto button : { x, y }) {
        button->AddDefaultMappings(Ship::PhysicalDeviceType::Keyboard);
        button->AddDefaultMappings(Ship::PhysicalDeviceType::SDLGamepad);
    }
}

GameEngine::GameEngine() {
    this->context = Ship::Context::CreateInstance(PORT_NAME, PORT_NAME);
    this->context->Init();
    ShipCompat::SetContext(this->context);

    auto logger = std::make_shared<Ship::Logger>(
        PORT_NAME, Ship::Context::GetPathRelativeToAppDirectory(std::string("logs/") + PORT_NAME + ".log"));
    context->GetChildren().Add(logger);
    logger->Init();

    const std::string configPath = Ship::Context::GetPathRelativeToAppDirectory(std::string(PORT_NAME) + ".cfg.json");
    const std::string legacyConfigPath = Ship::Context::GetPathRelativeToAppDirectory("acport.cfg.json");
    std::error_code configError;
    if (!fs::exists(configPath, configError) && fs::exists(legacyConfigPath, configError)) {
        fs::rename(legacyConfigPath, configPath, configError);
    }
    auto config = std::make_shared<Ship::Config>(configPath, nullptr);
    context->GetChildren().Add(config);

    auto consoleVariables = std::make_shared<Ship::ConsoleVariable>(config);
    context->GetChildren().Add(consoleVariables);
    CVarSetConsoleVariable(consoleVariables);

    CVarRegisterInteger(CVAR_TEXTURE_FILTER, Fast::FILTER_LINEAR);
    CVarRegisterInteger(CVAR_ENHANCEMENT("Graphics.DitherNoise"), 0);
    CVarRegisterInteger(CVAR_SETTING("InterpolationFPS"), 60);
    CVarRegisterInteger(CVAR_SETTING("FrameRateMode"), FRAME_RATE_60);
#ifdef __WINRT__
    CVarRegisterInteger(CVAR_IMGUI_CONTROLLER_NAV, 1);
#endif

    gsFast3dWindow = std::make_shared<Fast::Fast3dWindow>(std::vector<std::shared_ptr<Ship::GuiWindow>>({}), config,
                                                          consoleVariables);

#if (_DEBUG)
    auto defaultLogLevel = spdlog::level::debug;
#else
    auto defaultLogLevel = spdlog::level::info;
#endif
    auto logLevel =
        static_cast<spdlog::level::level_enum>(CVarGetInteger(CVAR_DEVELOPER_TOOLS("LogLevel"), defaultLogLevel));
    spdlog::set_level(logLevel);
    spdlog::flush_on(logLevel);

    assets_path = Ship::Context::LocateFileAcrossAppDirs(PORT_ARCHIVE);
    portArchiveVersionMatch = std::filesystem::exists(assets_path);
    if (!portArchiveVersionMatch) {
        const std::string message = std::string(PORT_ARCHIVE) +
                                    " was not found next to the executable.\n\nBuild the GeneratePortO2R target "
                                    "(it is part of the default build) and run " PORT_NAME " from its build folder.";
        SPDLOG_CRITICAL("{}", message);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PORT_NAME, message.c_str(), nullptr);
        exit(1);
    }

#ifdef __EMSCRIPTEN__
    auto threadPool = std::make_shared<Ship::ThreadPool>(2);
#else
    auto threadPool =
        std::make_shared<Ship::ThreadPool>(std::max(1, (int32_t)(std::thread::hardware_concurrency() - 3 - 1)));
#endif
    context->GetChildren().Add(threadPool);

#ifdef ENABLE_SCRIPTING
    auto keystore = std::make_shared<Ship::Keystore>(config);
    context->GetChildren().Add(keystore);
    auto resourceManager = std::make_shared<Ship::ResourceManager>(threadPool, keystore);
#else
    auto resourceManager = std::make_shared<Ship::ResourceManager>(threadPool);
#endif
    context->GetChildren().Add(resourceManager);
    ResourceSetResourceManager(resourceManager);

    auto controllerDefaults = std::make_shared<LUS::ControllerDefaultMappings>(
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<Ship::KbScancode>>{
            { BTN_A, { Ship::KbScancode::LUS_KB_X } },
            { BTN_B, { Ship::KbScancode::LUS_KB_C } },
            { BTN_X, { Ship::KbScancode::LUS_KB_V } },
            { BTN_Y, { Ship::KbScancode::LUS_KB_Q } },
            { BTN_L, { Ship::KbScancode::LUS_KB_E } },
            { BTN_R, { Ship::KbScancode::LUS_KB_R } },
            { BTN_Z, { Ship::KbScancode::LUS_KB_Z } },
            { BTN_START, { Ship::KbScancode::LUS_KB_SPACE } },
            { BTN_CUP, { Ship::KbScancode::LUS_KB_ARROWKEY_UP } },
            { BTN_CDOWN, { Ship::KbScancode::LUS_KB_ARROWKEY_DOWN } },
            { BTN_CLEFT, { Ship::KbScancode::LUS_KB_ARROWKEY_LEFT } },
            { BTN_CRIGHT, { Ship::KbScancode::LUS_KB_ARROWKEY_RIGHT } },
            { BTN_DUP, { Ship::KbScancode::LUS_KB_T } },
            { BTN_DDOWN, { Ship::KbScancode::LUS_KB_G } },
            { BTN_DLEFT, { Ship::KbScancode::LUS_KB_F } },
            { BTN_DRIGHT, { Ship::KbScancode::LUS_KB_H } },
        },
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, Ship::KbScancode>>>{},
        std::unordered_map<CONTROLLERBUTTONS_T, std::unordered_set<SDL_GameControllerButton>>{
            { BTN_A, { SDL_CONTROLLER_BUTTON_A } },
            { BTN_B, { SDL_CONTROLLER_BUTTON_B } },
            { BTN_X, { SDL_CONTROLLER_BUTTON_X } },
            { BTN_Y, { SDL_CONTROLLER_BUTTON_Y } },
            { BTN_L, { SDL_CONTROLLER_BUTTON_LEFTSHOULDER } },
            { BTN_START, { SDL_CONTROLLER_BUTTON_START } },
            { BTN_DUP, { SDL_CONTROLLER_BUTTON_DPAD_UP } },
            { BTN_DDOWN, { SDL_CONTROLLER_BUTTON_DPAD_DOWN } },
            { BTN_DLEFT, { SDL_CONTROLLER_BUTTON_DPAD_LEFT } },
            { BTN_DRIGHT, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT } },
        },
        std::unordered_map<Ship::StickIndex, std::vector<std::pair<Ship::Direction, SDL_GameControllerButton>>>{},
        std::unordered_map<CONTROLLERBUTTONS_T, std::vector<std::pair<SDL_GameControllerAxis, int32_t>>>{},
        std::unordered_map<Ship::StickIndex,
                           std::vector<std::pair<Ship::Direction, std::pair<SDL_GameControllerAxis, int32_t>>>>{});
    auto controlDeck = std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>{}, controllerDefaults,
                                                          std::unordered_map<CONTROLLERBUTTONS_T, std::string>{
                                                              { BTN_A, "A" },
                                                              { BTN_B, "B" },
                                                              { BTN_X, "X" },
                                                              { BTN_Y, "Y" },
                                                              { BTN_L, "L" },
                                                              { BTN_R, "R" },
                                                              { BTN_Z, "Z" },
                                                              { BTN_START, "Start" },
                                                              { BTN_CLEFT, "CLeft" },
                                                              { BTN_CRIGHT, "CRight" },
                                                              { BTN_CUP, "CUp" },
                                                              { BTN_CDOWN, "CDown" },
                                                              { BTN_DLEFT, "DLeft" },
                                                              { BTN_DRIGHT, "DRight" },
                                                              { BTN_DUP, "DUp" },
                                                              { BTN_DDOWN, "DDown" },
                                                          },
                                                          gsFast3dWindow, consoleVariables);
    context->GetChildren().Add(controlDeck);
    ControllerSetControlDeck(controlDeck);

    try {
        nlohmann::json rmArgs;
        rmArgs["archivePaths"] = std::vector<std::string>{ assets_path };
        rmArgs["validHashes"] = std::vector<uint32_t>{};
        resourceManager->Init(rmArgs);
    } catch (const std::exception& e) { SPDLOG_WARN("ResourceManager init deferred: {}", e.what()); }

    auto console = std::make_shared<Ship::Console>();
    context->GetChildren().Add(console);
    console->Init();

#ifdef ENABLE_SCRIPTING
    resourceManager->GetArchiveManager()->SetUntrustedArchiveHandler(
        [](Ship::Archive& archive, Ship::KeystoreEntry& key) {
            const auto info = archive.GetManifest();

            std::string message = "A mod from an unknown author was found.\n\n";
            message += "Mod: " + info.Name + "\n";
            message += "Author: " + info.Author + "\n\n";
            message += "Mods can run code on your computer. Trust this author and load the mod?";

            const SDL_MessageBoxButtonData buttons[] = {
                { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
                { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
            };
            const SDL_MessageBoxData messageBox = {
                SDL_MESSAGEBOX_WARNING,
                nullptr,
                "Untrusted Mod Author",
                message.c_str(),
                SDL_arraysize(buttons),
                buttons,
                nullptr,
            };

            int buttonId = 0;
            if (SDL_ShowMessageBox(&messageBox, &buttonId) < 0) {
                return false;
            }
            return buttonId == 1;
        });
#endif

    auto crashHandler = std::make_shared<Ship::CrashHandler>();
    context->GetChildren().Add(crashHandler);
    CrashHandlerSetComponent(crashHandler);

    context->GetChildren().Add(gsFast3dWindow);
    WindowSetWindowComponent(gsFast3dWindow);
    GfxSetFast3dWindow(gsFast3dWindow);

    auto gfxDebugger = std::make_shared<Fast::GfxDebugger>();
    context->GetChildren().Add(gfxDebugger);
    GfxDebuggerSetComponent(gfxDebugger);

    auto fileDropMgr = std::make_shared<Ship::FileDrop>(gsFast3dWindow);
    context->GetChildren().Add(fileDropMgr);
    fileDropMgr->Init();

    gsFast3dWindow->SetTitle(PORT_NAME);
    gsFast3dWindow->Init();

    auto events = std::make_shared<Ship::Events>();
    context->GetChildren().Add(events);
    EventSystemSetEvents(events);
    PortEvents_Init();
    REGISTER_LISTENER(GameLoopTick, EVENT_PRIORITY_NORMAL, AddMissingGcButtonMappings);

    PortGui::SetupMenu();

    if (portArchiveVersionMatch) {
        fontMono = CreateFontWithSize(16.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLarger = CreateFontWithSize(20.0f, "fonts/Inconsolata-Regular.ttf");
        fontMonoLargest = CreateFontWithSize(24.0f, "fonts/Inconsolata-Regular.ttf");
        fontStandard = CreateFontWithSize(16.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLarger = CreateFontWithSize(20.0f, "fonts/Montserrat-Regular.ttf");
        fontStandardLargest = CreateFontWithSize(24.0f, "fonts/Montserrat-Regular.ttf");
        ImGui::GetIO().FontDefault = fontStandardLarger;
    } else {
        fontMono = fontMonoLarger = fontMonoLargest = nullptr;
        fontStandard = fontStandardLarger = fontStandardLargest = nullptr;
    }

    previousImGuiScaleIndex = -1;
    previousImGuiScale = initialImGuiScale;
    ScaleImGui();
}

typedef enum ExtractSteps {
    ES_PORT_ARCHIVE,
    ES_WINDOWS,
    ES_EXTRACT_ARGS,
    ES_EXTRACT,
    ES_VERIFY,
    GS_LOAD,
    GS_COMPILE,
} ExtractSteps;

static bool sGameArchiveFailed = false;

typedef enum PromptSteps {
    PS_FILE_CHECK,
    PS_LOCAL,
    PS_PICK,
    PS_FIRST,
    PS_WAIT,
    PS_NONE,
} PromptSteps;

typedef enum WindowsSteps {
    WS_TEMP,
    WS_PERMS,
    WS_ONEDRIVE,
    WS_DONE,
} WindowsSteps;

bool IsSubpath(const std::filesystem::path& path, const std::filesystem::path& base) {
    auto rel = std::filesystem::relative(path, base);
    return !rel.empty() && rel.native()[0] != '.';
}

bool PathTestCleanup(FILE* tfile) {
    try {
        if (std::filesystem::exists("./text.txt"))
            std::filesystem::remove("./text.txt");
        if (std::filesystem::exists("./test/"))
            std::filesystem::remove("./test/");
    } catch (std::filesystem::filesystem_error const& ex) { return false; }
    return true;
}

void CheckAndCreateModFolder() {
    try {
        std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", PORT_NAME);
        if (!std::filesystem::exists(modsPath)) {
            // Create mods folder relative to app dir
            modsPath = Ship::Context::GetPathRelativeToAppDirectory("mods", PORT_NAME);
            std::string filePath = modsPath + "/custom_mod_files_go_here.txt";
            if (std::filesystem::create_directories(modsPath)) {
                std::ofstream(filePath).close();
            }
        }
    } catch (std::filesystem::filesystem_error const& ex) {
        // Couldn't make the folder, continue silently
        return;
    }
}

static void SetupScriptLoader(std::shared_ptr<Ship::Context> context) {
#ifdef ENABLE_SCRIPTING
    if (ShipCompat::GetScriptLoader() != nullptr) {
        return;
    }

    std::unordered_map<std::string, std::string> defines = {
        { "F3DEX_GBI_2", "1" }, { "TARGET_PC", "1" },   { "VERSION", "VER_GAFE01_00" }, { "DEBUG", "0" },
        { "NDEBUG", "1" },      { "_LANGUAGE_C", "1" }, { "_USE_MATH_DEFINES", "1" },
    };
#ifdef _WIN32
    const std::string buildOptions = "-g -rdynamic -funsigned-char -std=c11";
#elif defined(__ANDROID__)
    defines["__ANDROID_MIN_SDK_VERSION__"] = std::to_string(__ANDROID_MIN_SDK_VERSION__);
    defines["__ANDROID_API__"] = std::to_string(__ANDROID_API__);
    const std::string buildOptions = "-nostdlib -funsigned-char -std=c11";
#else
    const std::string buildOptions = "-rdynamic -funsigned-char -std=c11";
#endif

#ifdef _WIN32
    const std::string tccBase = Ship::Context::GetAppBundlePath() + "/.tcc";
    const std::vector<std::string> includePaths = {
        tccBase + "/include",     tccBase + "/include/tcc",     tccBase + "/include/winapi",
        tccBase + "/include/sys", tccBase + "/include/sec_api",
    };
    const std::vector<std::string> libraries = { PORT_NAME };
#else
    std::string tccBase = Ship::Context::GetPathRelativeToAppDirectory(".tcc");
    if (!std::filesystem::exists(tccBase)) {
        tccBase = Ship::Context::GetAppBundlePath() + "/.tcc";
    }
    std::vector<std::string> includePaths = { tccBase + "/include" };
#ifdef __APPLE__
    if (FILE* fp = popen("xcrun --show-sdk-path 2>/dev/null", "r")) {
        char buf[4096] = {};
        if (fgets(buf, sizeof(buf), fp)) {
            std::string sdkPath(buf);
            sdkPath.erase(sdkPath.find_last_not_of("\n\r \t") + 1);
            if (!sdkPath.empty()) {
                includePaths.push_back(sdkPath + "/usr/include");
            }
        }
        pclose(fp);
    }
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    {
        const auto libcStub = std::filesystem::path(tccBase) / "lib" / "libc.so";
        std::error_code ec;
        const bool stale = std::filesystem::is_symlink(libcStub) && !std::filesystem::exists(libcStub, ec);
        if (!std::filesystem::exists(libcStub, ec) || stale) {
            Dl_info info = {};
            void* libcFunc = dlsym(RTLD_DEFAULT, "printf");
            if (libcFunc != nullptr && dladdr(libcFunc, &info) && info.dli_fname != nullptr && *info.dli_fname) {
                std::filesystem::remove(libcStub, ec);
                std::filesystem::create_symlink(info.dli_fname, libcStub, ec);
                if (ec) {
                    SPDLOG_WARN("ScriptLoader: failed to create libc.so symlink: {}", ec.message());
                }
            }
        }
    }
#endif
#endif
    std::vector<std::string> libraryPaths = { tccBase + "/lib" };
#ifdef __ANDROID__
    Dl_info self = {};
    if (dladdr((void*)&SetupScriptLoader, &self) && self.dli_fname != nullptr) {
        libraryPaths.push_back(std::filesystem::path(self.dli_fname).parent_path().string());
    }
    const std::vector<std::string> libraries = { "tcc1", "main" };
#elif !defined(_WIN32)
    const std::vector<std::string> libraries = {};
#endif

    auto scriptLoader = std::make_shared<Ship::ScriptLoader>(defines, BW_CODE_VERSION, buildOptions, includePaths,
                                                             libraryPaths, libraries, ShipCompat::GetResourceManager());
    scriptLoader->SetApiVersion(BW_API_VERSION);
    scriptLoader->SetEntryHook(
        [](const std::string& name, const std::string& symbol, Ship::Scripting::LibraryFunc_t entry) {
            if (entry != nullptr) {
                ModOwner::Scope scope(name);
                entry();
            }
            if (symbol == "ModExit") {
                ModOwner::Release(name);
            }
        });
    context->GetChildren().Add(scriptLoader);
    ScriptSetLoader(scriptLoader);
#endif
}

void GameEngine::LoadResourceFiles() {
    SetupScriptLoader(context);

    std::string gamePath = Ship::Context::LocateFileAcrossAppDirs(GAME_ARCHIVE, PORT_NAME);
    if (std::filesystem::exists(gamePath)) {
        sGameArchiveFailed = ShipCompat::GetResourceManager()->GetArchiveManager()->AddArchive(gamePath) == nullptr;
        sGameArchiveLoaded = !sGameArchiveFailed;
    }

    const std::string patches_path = Ship::Context::GetPathRelativeToAppDirectory("mods");

    if (!patches_path.empty()) {
        if (!std::filesystem::exists(patches_path)) {
            std::filesystem::create_directories(patches_path);
        }

        if (std::filesystem::is_directory(patches_path)) {
            std::vector<std::filesystem::path> archives;
            for (auto it = std::filesystem::recursive_directory_iterator(patches_path);
                 it != std::filesystem::recursive_directory_iterator(); ++it) {
                const auto& p = *it;
                const auto ext = p.path().extension().string();
                if (p.is_directory() && StringHelper::IEquals(ext, ".disabled")) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (StringHelper::IEquals(ext, ".otr") || StringHelper::IEquals(ext, ".o2r")) {
                    archives.push_back(p.path());
                }

                if (StringHelper::IEquals(ext, ".zip")) {
                    SPDLOG_WARN("Zip files should be only used for development purposes, not for distribution");
                    archives.push_back(p.path());
                }
            }
            std::sort(archives.begin(), archives.end());
            for (const auto& archive : archives) {
                ShipCompat::GetResourceManager()->GetArchiveManager()->AddArchive(archive.generic_string());
            }

            std::vector<std::filesystem::path> folders;
            for (const auto& p : std::filesystem::directory_iterator(patches_path)) {
                if (p.is_directory() && !StringHelper::IEquals(p.path().extension().string(), ".disabled")) {
                    folders.push_back(p.path());
                }
            }
            std::sort(folders.begin(), folders.end());
            for (const auto& folder : folders) {
                SPDLOG_INFO("Found mod directory: {}", folder.generic_string());
                ShipCompat::GetResourceManager()->GetArchiveManager()->AddArchive(folder.generic_string());
            }
        }
    }

    ModList::Apply();

#ifdef ENABLE_SCRIPTING
    totalScripts = 0;
    const auto loadedArchives = ShipCompat::GetResourceManager()->GetArchiveManager()->GetArchives();
    for (const auto& archive : *loadedArchives) {
        const auto& info = archive->GetManifest();
        if (!info.Main.empty() || !info.Binaries.empty()) {
            totalScripts++;
        }
    }
#endif
}

#ifdef ENABLE_SCRIPTING
static std::vector<std::string> CompileScripts(const std::function<void(const std::string&)>& onStart,
                                               const std::function<void()>& onFinish) {
    std::vector<std::string> errors;
    auto scripts = ShipCompat::GetScriptLoader();
    if (scripts == nullptr) {
        return errors;
    }
    ModList::ClearStatus();
    const auto archives = ShipCompat::GetResourceManager()->GetArchiveManager()->GetArchives();
    for (const auto& archive : *archives) {
        const auto& info = archive->GetManifest();
        if (info.Main.empty() && info.Binaries.empty()) {
            continue;
        }
        if (onStart) {
            onStart(info.Name);
        }
        try {
            scripts->Compile(archive);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Failed to build {}: {}", info.Name, e.what());
            errors.push_back(info.Name + ": " + e.what());
            ModList::SetStatus(info.Name, std::string("Failed to build: ") + e.what(), true);
        }
        if (onFinish) {
            onFinish();
        }
    }
    return errors;
}

static void ReportLoad(const std::shared_ptr<Ship::ScriptLoader>& scripts) {
    for (const auto& error : scripts->GetLoadErrors()) {
        for (auto& entry : ModList::Entries()) {
            const std::string& name = entry.archive->GetManifest().Name;
            if (error.rfind(name + ": ", 0) == 0) {
                entry.status = "Skipped: " + error.substr(name.size() + 2);
                entry.failed = true;
            }
        }
    }
    for (auto& entry : ModList::Entries()) {
        if (entry.enabled && entry.status.empty()) {
            entry.status = "Loaded";
        }
    }
    for (const auto& error : scripts->GetLoadErrors()) {
        Notification::Emit({ .message = "Skipped " + error,
                             .messageColor = ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                             .remainingTime = 8.0f,
                             .mute = true });
    }
}
#endif

void GameEngine::LoadScripts() {
#ifdef ENABLE_SCRIPTING
    auto scripts = ShipCompat::GetScriptLoader();
    if (scripts == nullptr) {
        return;
    }
    Notification::Emit({ .message = "Loading mods...", .remainingTime = 3.0f, .mute = true });
    for (const auto& error : CompileScripts(nullptr, nullptr)) {
        Notification::Emit({ .message = "Failed to build " + error,
                             .messageColor = ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                             .remainingTime = 8.0f,
                             .mute = true });
    }
    try {
        scripts->LoadAll();
        ReportLoad(scripts);
        Notification::Emit({ .message = "Finished loading mods", .remainingTime = 4.0f, .mute = true });
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to load mods: {}", e.what());
        Notification::Emit({ .message = "Failed to load some mods, check the logs for details.",
                             .messageColor = ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                             .remainingTime = 8.0f,
                             .mute = true });
    }
#endif
}

void GameEngine::FinishInit() {
    spdlog::set_pattern("[%H:%M:%S.%e] [%s:%#] [%l] %v");
    SPDLOG_INFO("Starting {} version {} (Branch: {} | Commit: {})", PORT_NAME, (char*)gBuildVersion, (char*)gGitBranch,
                (char*)gGitCommitHash);

    auto audio = std::make_shared<Ship::Audio>(
        Ship::AudioSettings{ .SampleRate = 32000, .SampleLength = 512, .DesiredBuffered = 1100 },
        ShipCompat::GetConfig());
    context->GetChildren().Add(audio);
    AudioSetAudioComponent(audio);
    audio->Init();

    gsFast3dWindow->SetTargetFps(60);
    gsFast3dWindow->SetMaximumFrameLatency(1);
    gsFast3dWindow->SetRendererUCode(ucode_f3dex2_ac);
    gsFast3dWindow->SetTextureFilter((Fast::FilteringMode)CVarGetInteger(CVAR_TEXTURE_FILTER, Fast::FILTER_LINEAR));
    GameEngine_GetInterpreter()->SetNativeDimensions(NATIVE_SCREEN_WIDTH, NATIVE_SCREEN_HEIGHT);
    gAC_PrerenderFb = gfx_create_framebuffer(NATIVE_SCREEN_WIDTH, NATIVE_SCREEN_HEIGHT, NATIVE_SCREEN_WIDTH,
                                             NATIVE_SCREEN_HEIGHT, 1, false);

    auto loader = ShipCompat::GetResourceManager()->GetResourceLoader();
    auto blobFactory = std::make_shared<Ship::ResourceFactoryBinaryBlobV0>();

    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(Fast::ResourceType::Texture), 1);
    loader->RegisterResourceFactory(std::make_shared<AC::ResourceFactoryBinaryRawTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "RawTexture", static_cast<uint32_t>(AC::ResourceType::RawTexture), 0);
    loader->RegisterResourceFactory(std::make_shared<AC::ResourceFactoryBinaryRawTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "RawTexture", static_cast<uint32_t>(AC::ResourceType::RawTexture), 1);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY,
                                    "Vertex", static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLVertexV0>(), RESOURCE_FORMAT_XML, "Vertex",
                                    static_cast<uint32_t>(Fast::ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV0>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryDisplayListV1>(),
                                    RESOURCE_FORMAT_BINARY, "DisplayList",
                                    static_cast<uint32_t>(Fast::ResourceType::DisplayList), 1);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryXMLDisplayListV0>(), RESOURCE_FORMAT_XML,
                                    "DisplayList", static_cast<uint32_t>(Fast::ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY,
                                    "Matrix", static_cast<uint32_t>(Fast::ResourceType::Matrix), 0);
    loader->RegisterResourceFactory(std::make_shared<Fast::ResourceFactoryBinaryLightV0>(), RESOURCE_FORMAT_BINARY,
                                    "Light", static_cast<uint32_t>(Fast::ResourceType::Light), 0);
    loader->RegisterResourceFactory(std::make_shared<AC::ResourceFactoryBinaryAssetArrayV0>(), RESOURCE_FORMAT_BINARY,
                                    "AssetArray", static_cast<uint32_t>(AC::ResourceType::AssetArray), 0);
    loader->RegisterResourceFactory(std::make_shared<AC::ResourceFactoryBinaryGenericArrayV0>(), RESOURCE_FORMAT_BINARY,
                                    "GenericArray", static_cast<uint32_t>(AC::ResourceType::GenericArray), 0);
    loader->RegisterResourceFactory(std::make_shared<AC::ResourceFactoryBinaryKeyFrameSkelV0>(), RESOURCE_FORMAT_BINARY,
                                    "KeyFrameSkel", static_cast<uint32_t>(AC::ResourceType::KeyFrameSkel), 0);
    loader->RegisterResourceFactory(std::make_shared<AC::ResourceFactoryBinaryKeyFrameAnimV0>(), RESOURCE_FORMAT_BINARY,
                                    "KeyFrameAnim", static_cast<uint32_t>(AC::ResourceType::KeyFrameAnim), 0);

    loader->RegisterResourceFactory(blobFactory, RESOURCE_FORMAT_BINARY, "Blob",
                                    static_cast<uint32_t>(Ship::ResourceType::Blob), 0);
#ifdef AC_HAS_IMPORTERS
    AC_RegisterResourceImporters();
#endif
    prevAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    ShipCompat::GetResourceManager()->SetAltAssetsEnabled(prevAltAssets);

    Instance->AudioInit();
    AC_AudioInit();
    PortGui::SetupGuiElements();
    DevConsole_Init();
    ShipInit::InitAll();
#ifdef ENABLE_SCRIPTING
    if (PortGui::mPortMenu != nullptr) {
        PortGui::mPortMenu->AddModMenu();
    }
    if (auto scripts = ShipCompat::GetScriptLoader()) {
        try {
            scripts->LoadAll();
            ReportLoad(scripts);
        } catch (const std::exception& e) { SPDLOG_ERROR("Failed to load mods: {}", e.what()); }
    }
#endif
    CALL_EVENT(EngineReady);

    if (!sGameArchiveLoaded) {
        SPDLOG_WARN("{} is not loaded; running the port shell without game assets", GAME_ARCHIVE);
        Notification::Emit({ .prefix = GAME_ARCHIVE,
                             .message = "not loaded.",
                             .suffix = "Running without game assets.",
                             .remainingTime = 10.0f });
    }
}

void GameEngine::RunExtract(int argc, char* argv[]) {
    bool extractDone = false;
    std::atomic<bool> compileDone = false;
    std::atomic<size_t> compileCount{ 0 };
    std::mutex compileMutex;
    std::string compileName;
    std::string compileError;
    bool compileErrorDismissed = false;
    ExtractSteps extractStep = ES_PORT_ARCHIVE;
    WindowsSteps windowsStep = WS_TEMP;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ShipCompat::GetWindow());
    auto gui = wnd->GetGui();
    bool menuWasVisible = false;
    if (gui->GetMenu()->IsVisible()) {
        menuWasVisible = true;
        gui->GetMenu()->Hide();
    }

    // runtime, because the archive is held open (and can't be removed on Windows).
    // Honor it here on the next launch, before the archive is read or mounted.
    {
        std::error_code ec;
        const std::string marker = Ship::Context::GetPathRelativeToAppDirectory("regenerate_o2r", PORT_NAME);
        if (std::filesystem::exists(marker, ec)) {
            const std::string o2r = Ship::Context::LocateFileAcrossAppDirs(GAME_ARCHIVE, PORT_NAME);
            if (!o2r.empty()) {
                std::filesystem::remove(o2r, ec);
            }
            std::filesystem::remove(marker, ec);
        }
    }

    OTRVersion gameArchiveVersion = DetectOTRVersion(GAME_ARCHIVE);

    const std::string gameO2RPath = Ship::Context::LocateFileAcrossAppDirs(GAME_ARCHIVE, PORT_NAME);
    bool found = std::filesystem::exists(gameO2RPath);
    bool shouldRegen =
        !VerifyArchiveVersion(gameArchiveVersion) && gameArchiveVersion.major != INT16_MAX &&
        !(gameArchiveVersion.major == 0 && gameArchiveVersion.minor == 0 && gameArchiveVersion.patch == 0);
    if (found && IsArchiveGameVersionUnknown(gameO2RPath)) {
        shouldRegen = true;
    }

    std::filesystem::path ownPath;
    std::vector<std::string> args;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            args.push_back(argv[i]);
        }
    }
    GameExtractor extract;
    PromptSteps promptStep = PS_FILE_CHECK;
    std::atomic<bool> extracting = false;
    std::vector<std::pair<std::string, std::string>> discChoices;
    bool showDiscPicker = false;
    bool pickFolder = false;
    std::atomic<size_t> extractCount{ 0 }, totalExtract{ 0 };
    std::string installPath = Ship::Context::GetAppBundlePath();
    std::string file;
    std::shared_ptr<BS::thread_pool> threadPool = std::make_shared<BS::thread_pool>(1);
#ifdef USE_NETWORKING
    bool satellaStarted = false;
    std::atomic<bool> satellaDone = false;
#endif

    auto startExtraction = [&]() {
        if (!GameExtractor::IsDiscInputAvailable()) {
            PortGui::RegisterPopup("Disc Extraction Not Implemented Yet",
                                   "This build's Torch has no GameCube disc input yet, so " +
                                       std::string(GAME_ARCHIVE) +
                                       " cannot be generated.\nThe port will start without game assets.",
                                   "OK", "", [&]() { extractStep = GS_LOAD; });
            promptStep = PS_WAIT;
            return;
        }
        extracting = true;
        threadPool->submit_task([&]() -> void {
            if (extract.Parse(totalExtract, PORT_NAME)) {
                extract.GenerateOTR(extractCount, PORT_NAME);
            }
#ifdef __EMSCRIPTEN__
            std::error_code ec;
            std::filesystem::remove_all(kWebDiscDir, ec);
#endif
            extracting = false;
            extractCount = totalExtract = 0;
            args.clear();
            extractStep = ES_VERIFY;
        });
    };

    if (!found && !ExtractorDataExists(installPath)) {
        PortGui::RegisterPopup("Game Assets Not Found",
                               std::string(GAME_ARCHIVE) +
                                   " was not found and the extractor data (config.yml, "
                                   "assets/ymls) is missing.\nThe port will start without game assets.",
                               "OK", "", [&]() { extractStep = GS_LOAD; });
        promptStep = PS_WAIT;
    } else if (shouldRegen) {
        PortGui::RegisterPopup("Outdated Game Archive",
                               "Your " + std::string(GAME_ARCHIVE) + " was created with an incompatible version of " +
                                   PORT_NAME + ".\nYou will now be redirected to re-extract it.");
        std::error_code ec;
        std::filesystem::remove(gameO2RPath, ec);
    }
    while (true) {
        if (extractDone && sGameArchiveFailed) {
            // The archive is present but unreadable. Loading it again every frame only leaks, so set it
            // aside and go back to the setup prompt instead.
            if (PortGui::PopupsQueued() == 0) {
                PortGui::RegisterPopup(
                    "Game Archive Error",
                    "The saved game archive could not be opened.\nIt will be set aside so you can generate it again "
                    "from your disc.\n\nCheck the logs for details.",
                    "OK", "", [&]() {
                        const std::string o2r = Ship::Context::LocateFileAcrossAppDirs(GAME_ARCHIVE, PORT_NAME);
                        std::error_code ec;
                        std::filesystem::rename(o2r, o2r + ".unreadable", ec);
                        sGameArchiveFailed = false;
                        extractDone = false;
                        extractStep = ES_EXTRACT;
                        promptStep = PS_FILE_CHECK;
                    });
            }
            goto render;
        }
        if (extractDone && (compileError.empty() || compileErrorDismissed)) {
            break;
        }
        if (PortGui::PopupsQueued() > 0 || extracting || showDiscPicker) {
            goto render;
        }
        switch (extractStep) {
            case ES_PORT_ARCHIVE: {
#if defined(_WIN32) && !defined(__WINRT__)
                extractStep = ES_WINDOWS;
#else
                extractStep = args.size() > 0 ? ES_EXTRACT_ARGS : ES_EXTRACT;
#endif
                continue;
            }
            case ES_WINDOWS: {
                switch (windowsStep) {
                    case WS_TEMP: {
#ifdef _WIN32
                        char* tempVar = getenv("TEMP");
                        std::filesystem::path tempPath;
                        try {
                            tempPath = std::filesystem::canonical(tempVar);
                        } catch (std::filesystem::filesystem_error const& ex) {
                            std::string userPath = getenv("USERPROFILE");
                            userPath.append("\\AppData\\Local\\Temp");
                            tempPath = std::filesystem::canonical(userPath);
                        }
                        wchar_t buffer[MAX_PATH];
                        GetModuleFileName(NULL, buffer, _countof(buffer));
                        ownPath = std::filesystem::canonical(buffer).parent_path();
                        if (IsSubpath(ownPath, tempPath)) {
                            PortGui::RegisterPopup(std::string(PORT_NAME) + " Path Error",
                                                   std::string(PORT_NAME) +
                                                       " is running in a temp folder.\nExtract the .zip and run again.",
                                                   "OK", "", [&]() {
                                                       threadPool = nullptr;
                                                       gsFast3dWindow = nullptr;
                                                       context = nullptr;
                                                       exit(0);
                                                   });
                        } else {
                            windowsStep = WS_PERMS;
                        }
#endif
                        continue;
                    }
                    case WS_PERMS: {
                        FILE* tfile = fopen("./text.txt", "w");
                        std::filesystem::path tfolder = std::filesystem::path("./test/");
                        bool error = false;
                        try {
                            create_directories(tfolder);
                        } catch (std::filesystem::filesystem_error const& ex) { error = true; }
                        if (tfile == NULL || error) {
                            PortGui::RegisterPopup(std::string(PORT_NAME) + " Permissions Error",
                                                   std::string(PORT_NAME) +
                                                       " does not have proper file permissions.\nPlease move it to a "
                                                       "folder that does and run again.",
                                                   "OK", "", [&]() {
                                                       fclose(tfile);
                                                       PathTestCleanup(tfile);
                                                       threadPool = nullptr;
                                                       gsFast3dWindow = nullptr;
                                                       context = nullptr;
                                                       exit(0);
                                                   });
                        } else {
                            fclose(tfile);
                            if (!PathTestCleanup(tfile)) {
                                PortGui::RegisterPopup(std::string(PORT_NAME) + " Permissions Error",
                                                       std::string(PORT_NAME) +
                                                           " does not have proper file permissions.\nPlease move it "
                                                           "to a folder that does and run again.",
                                                       "OK", "", [&]() {
                                                           threadPool = nullptr;
                                                           gsFast3dWindow = nullptr;
                                                           context = nullptr;
                                                           exit(0);
                                                       });
                            }
                            windowsStep = WS_ONEDRIVE;
                        }
                        continue;
                    }
                    case WS_ONEDRIVE: {
                        if (ownPath.string().find("OneDrive") != std::string::npos) {
                            PortGui::RegisterPopup(
                                std::string(PORT_NAME) + " Path Error",
                                std::string(PORT_NAME) +
                                    " appears to be in a OneDrive folder, which will cause "
                                    "issues.\nPlease move it to a folder outside of OneDrive, like the root of a\n"
                                    "drive (e.g. \"C:\\Games\\" +
                                    PORT_NAME + "\").",
                                "OK", "", [&]() {
                                    threadPool = nullptr;
                                    gsFast3dWindow = nullptr;
                                    context = nullptr;
                                    exit(0);
                                });
                        } else {
                            windowsStep = WS_DONE;
                            if (args.size() > 0) {
                                extractStep = ES_EXTRACT_ARGS;
                            } else {
                                extractStep = ES_EXTRACT;
                            }
                        }
                        continue;
                    }
                    default:
                        continue;
                }
                break;
            }
            case ES_EXTRACT_ARGS: {
                if (args.size() == 0) {
                    extractStep =
                        std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs(GAME_ARCHIVE, PORT_NAME))
                            ? ES_VERIFY
                            : ES_EXTRACT;
                    continue;
                }
                file = args.at(0);
                args.erase(args.begin());
                extract = GameExtractor();
                if (extract.RunStandalone(file)) {
                    if (std::filesystem::exists(Ship::Context::GetAppDirectoryPath(PORT_NAME) + "/" + GAME_ARCHIVE)) {
                        std::string msg =
                            "Archive for this disc, " + std::string(GAME_ARCHIVE) + ", already exists.\nExtract again?";
                        PortGui::RegisterPopup(
                            "Confirm Re-extract", msg.c_str(), "Yes", "No", [&]() { startExtraction(); },
                            [&]() {
                                args.clear();
                                extractStep = ES_VERIFY;
                            });
                    } else {
                        startExtraction();
                    }
                } else {
                    std::string msg = "File\n" + std::string(file) + "\nis not a supported Animal Crossing disc.\n" +
                                      GameExtractor::sLastError;
                    PortGui::RegisterPopup("Disc Error", msg.c_str());
                }
                break;
            }
            case ES_EXTRACT: {
                switch (promptStep) {
                    case PS_FILE_CHECK: {
                        const bool gameO2RExists =
                            std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs(GAME_ARCHIVE, PORT_NAME));

                        if (gameO2RExists) {
                            extractStep = ES_VERIFY;
                            continue;
                        }
                        promptStep = PS_WAIT;
                        PortGui::RegisterPopup(
                            "No Game Archive",
                            std::string(GAME_ARCHIVE) + " was not found. Generate it now from your Animal Crossing "
                                                        "disc?",
                            "Yes", "No", [&]() { promptStep = PS_LOCAL; }, [&]() { extractStep = GS_LOAD; });
                        continue;
                    }
                    case PS_LOCAL: {
                        extract = GameExtractor();
                        discChoices =
                            extract.FindSupportedDiscs({ installPath, Ship::Context::GetAppDirectoryPath(PORT_NAME) });
                        if (discChoices.size() > 1) {
                            promptStep = PS_WAIT;
                            showDiscPicker = true;
                        } else if (discChoices.size() == 1) {
                            promptStep = PS_WAIT;
                            std::string msg = "Found " + discChoices[0].second + ".\nGenerate game files from it?";
                            PortGui::RegisterPopup(
                                "Disc found", msg.c_str(), "Yes", "No",
                                [&]() {
                                    file = discChoices[0].first;
                                    extract.RunStandalone(file);
                                    startExtraction();
                                },
                                [&]() { promptStep = PS_PICK; });
                        } else {
                            promptStep = PS_PICK;
                        }
                        continue;
                    }
                    case PS_PICK: {
#if defined(__EMSCRIPTEN__)
                        pickFolder = false;
                        promptStep = PS_FIRST;
                        continue;
#elif defined(__WINRT__)
                        promptStep = PS_WAIT;
                        PortGui::RegisterPopup(
                            "Add Your Disc",
                            "Upload " + std::string(GAME_ARCHIVE) +
                                ", or your Animal Crossing disc image\n(.iso, .gcm, .ciso, .gcz), to this app's "
                                "LocalState folder\nusing the Xbox Device Portal, then choose Retry.",
                            "Retry", "Skip", [&]() { promptStep = PS_LOCAL; }, [&]() { extractStep = GS_LOAD; });
                        continue;
#elif defined(__IOS__) || defined(__ANDROID__)
                        promptStep = PS_WAIT;
#ifdef __IOS__
                        const std::string discFolder = "Files > On My iPhone > " + std::string(PORT_NAME);
#else
                        const std::string discFolder = Ship::Context::GetAppDirectoryPath(PORT_NAME);
#endif
                        PortGui::RegisterPopup(
                            "Add Your Disc",
                            "Copy your Animal Crossing disc image (.iso, .gcm, .ciso, .gcz) into\n" + discFolder +
                                "\nand choose Retry.",
                            "Retry", "Skip", [&]() { promptStep = PS_LOCAL; }, [&]() { extractStep = GS_LOAD; });
                        continue;
#endif
                        promptStep = PS_WAIT;
                        PortGui::RegisterPopup(
                            "Select Game Disc",
                            "Choose a GameCube disc image (.iso, .gcm, .ciso, .gcz)\nor a folder extracted from the "
                            "disc (containing sys/ and files/).",
                            "Disc Image", "Disc Folder",
                            [&]() {
                                pickFolder = false;
                                promptStep = PS_FIRST;
                            },
                            [&]() {
                                pickFolder = true;
                                promptStep = PS_FIRST;
                            });
                        continue;
                    }
                    case PS_FIRST: {
#ifdef __EMSCRIPTEN__
                        {
                            std::string webError;
                            std::error_code ec;
                            const std::string archivePath =
                                Ship::Context::GetPathRelativeToAppDirectory(GAME_ARCHIVE, PORT_NAME);
                            const auto picked = WebFilePicker_PickDisc(kWebDiscDir, archivePath, webError);
                            if (picked == WebPickResult::Archive) {
                                extractStep = ES_VERIFY;
                            } else if (picked == WebPickResult::Disc && extract.RunStandalone(kWebDiscDir)) {
                                file = kWebDiscDir;
                                startExtraction();
                            } else if (picked == WebPickResult::Cancelled) {
                                promptStep = PS_FILE_CHECK;
                            } else {
                                std::filesystem::remove_all(kWebDiscDir, ec);
                                promptStep = PS_WAIT;
                                PortGui::RegisterPopup("Disc Error",
                                                       webError.empty() ? GameExtractor::sLastError : webError, "OK",
                                                       "", [&]() { promptStep = PS_FILE_CHECK; });
                            }
                        }
                        continue;
#endif
                        if (!extract.SelectGameFromUI(pickFolder)) {
                            promptStep = PS_WAIT;
                            if (!GameExtractor::sLastError.empty()) {
                                PortGui::RegisterPopup("Disc Error", GameExtractor::sLastError, "OK", "",
                                                       [&]() { promptStep = PS_FILE_CHECK; });
                            } else {
                                promptStep = PS_FILE_CHECK;
                            }
                            continue;
                        }
                        file = extract.GetDiscPath();
                        startExtraction();
                        continue;
                    }
                    default:
                        break;
                }
                break;
            }
            case ES_VERIFY: {
                const bool gameO2RExists =
                    std::filesystem::exists(Ship::Context::LocateFileAcrossAppDirs(GAME_ARCHIVE, PORT_NAME));
#ifdef __EMSCRIPTEN__
                if (gameO2RExists) {
                    WebCache_Save();
                }
#endif

                if (!gameO2RExists) {
                    if (PortGui::PopupsQueued() == 0) {
                        std::string errorMsg;
                        if (!GameExtractor::sLastError.empty()) {
                            // Word-wrap the raw Torch error so the popup stays readable.
                            std::string wrapped = GameExtractor::sLastError;
                            const size_t wrapCol = 80;
                            size_t pos = 0;
                            while (pos + wrapCol < wrapped.size()) {
                                size_t breakAt = wrapped.rfind(' ', pos + wrapCol);
                                if (breakAt == std::string::npos || breakAt <= pos) {
                                    breakAt = pos + wrapCol;
                                }
                                wrapped.insert(breakAt, "\n");
                                pos = breakAt + 1;
                            }
                            errorMsg = "Disc extraction failed:\n\n" + wrapped + "\n\nCheck the logs for full details.";
                        } else {
                            errorMsg = "No game archive was generated.";
                        }
                        errorMsg += "\nThe port will start without game assets.";
                        PortGui::RegisterPopup("Extraction Error", errorMsg.c_str(), "OK", "",
                                               [&]() { extractStep = GS_LOAD; });
                    }
                    continue;
                }

                extractStep = GS_LOAD;
                continue;
            }
            case GS_LOAD: {
#ifdef USE_NETWORKING
                if (!satellaStarted && CVarGetInteger(CVAR_DEVELOPER_TOOLS("Satella"), 1)) {
                    satellaStarted = true;
                    threadPool->submit_task([&]() -> void {
                        Satella::Client::Instance().Execute();
                        satellaDone = true;
                    });
                }
                if (satellaStarted && !satellaDone) {
                    break;
                }
#endif
                LoadResourceFiles();
#ifdef ENABLE_SCRIPTING
                if (totalScripts > 0) {
                    threadPool->submit_task([&]() -> void {
                        const auto errors = CompileScripts(
                            [&](const std::string& name) {
                                std::lock_guard<std::mutex> lock(compileMutex);
                                compileName = name;
                            },
                            [&]() { compileCount++; });
                        {
                            std::lock_guard<std::mutex> lock(compileMutex);
                            for (const auto& error : errors) {
                                compileError += (compileError.empty() ? "" : "\n\n") + error;
                            }
                        }
                        compileDone = true;
                    });
                    extractStep = GS_COMPILE;
                    continue;
                }
#endif
                extractDone = true;
                continue;
            }
            case GS_COMPILE: {
                if (compileDone) {
                    extractDone = true;
                }
                break;
            }
            default:
                break;
        }

    render:
        if (!WindowIsRunning()) {
            threadPool = nullptr;
            gsFast3dWindow = nullptr;
            context = nullptr;
            exit(0);
        }
        // Process window events for resize, mouse, keyboard events
        wnd->HandleEvents();
        UIWidgets::Colors themeColor =
            static_cast<UIWidgets::Colors>(CVarGetInteger(CVAR_SETTING("Menu.Theme"), UIWidgets::Colors::LightBlue));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, UIWidgets::ColorValues.at(themeColor));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray));

        // Skip dropped frames
        if (!wnd->IsFrameReady()) {
            ImGui::PopStyleColor(2);
            continue;
        }
        gui->StartDraw();
        gsFast3dWindow->StartFrame();
        gsFast3dWindow->RunGuiOnly();

        if (showDiscPicker && !ImGui::IsPopupOpen("Select Game Disc##picker")) {
            ImGui::OpenPopup("Select Game Disc##picker");
        }
        if (showDiscPicker) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
            auto pickerColor = UIWidgets::ColorValues.at(THEME_COLOR);
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, pickerColor);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(pickerColor.x, pickerColor.y, pickerColor.z, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
            if (ImGui::BeginPopupModal("Select Game Disc##picker", NULL,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::Text("Multiple game discs were found.");
                ImGui::TextDisabled("Only one %s is generated, so pick the disc to use:", GAME_ARCHIVE);
                ImGui::Spacing();
                for (size_t i = 0; i < discChoices.size(); i++) {
                    const auto& choice = discChoices[i];
                    std::string label =
                        choice.second + "##disc" + std::to_string(i); // version name; path shown as tooltip
                    if (ImGui::Button(label.c_str(), ImVec2(420.0f, 0.0f))) {
                        file = choice.first;
                        extract.RunStandalone(file);
                        showDiscPicker = false;
                        startExtraction();
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", choice.first.c_str());
                    }
                }
                ImGui::Spacing();
                if (ImGui::Button("Browse for a different disc...", ImVec2(420.0f, 0.0f))) {
                    showDiscPicker = false;
                    promptStep = PS_PICK;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        if (extracting && !ImGui::IsPopupOpen("Disc Extraction")) {
            ImGui::OpenPopup("Disc Extraction");
        }
        if (extracting) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
            auto color = UIWidgets::ColorValues.at(THEME_COLOR);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(color.x, color.y, color.z, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
            if (ImGui::BeginPopupModal("Disc Extraction", NULL,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                float progress = (totalExtract > 0 ? (float)extractCount / (float)totalExtract : 0.0f) * 100.0f;
                if (progress > 100.0f) {
                    progress = 100.0f;
                }
                const bool counting = totalExtract > 0 && extractCount > 0;
                const bool done = counting && roundf(progress) == 100.0f;
                auto filename = std::filesystem::path(file).filename().string();

                // Status line
                if (done) {
                    ImGui::Text("Done! Finishing up...");
                } else if (counting) {
                    ImGui::Text("Extracting %s...", filename.c_str());
                    std::string assetName = GameExtractor::GetCurrentAssetName();
                    if (!assetName.empty()) {
                        const float maxWidth = 600.0f - ImGui::GetStyle().WindowPadding.x * 2.0f;
                        if (ImGui::CalcTextSize(assetName.c_str()).x > maxWidth) {
                            const std::string ellipsis = "...";
                            const float ellipsisWidth = ImGui::CalcTextSize(ellipsis.c_str()).x;
                            while (assetName.size() > 3 &&
                                   ImGui::CalcTextSize(assetName.c_str()).x > maxWidth - ellipsisWidth) {
                                assetName.pop_back();
                            }
                            assetName += ellipsis;
                        }
                        ImGui::TextDisabled("%s", assetName.c_str());
                    } else {
                        ImGui::TextDisabled(" ");
                    }
                } else {
                    ImGui::Text("Reading %s...", filename.c_str());
                    ImGui::TextDisabled("Please wait, this can take a moment.");
                }

                // Progress bar
                std::string overlay =
                    counting ? fmt::format("{:.0f}%", progress) : (std::string) "Reading disc, please wait...";
                ImGui::ProgressBar(counting ? progress / 100.0f : -1.0f * (float)ImGui::GetTime(),
                                   ImVec2(600.0f, 50.0f), overlay.c_str());
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        std::string modName;
        std::string modError;
        {
            std::lock_guard<std::mutex> lock(compileMutex);
            modName = compileName;
            modError = compileError;
        }
#ifdef USE_NETWORKING
        const auto satellaPhase = Satella::Client::Instance().GetPhase();
        const bool satellaPopup =
            satellaPhase == Satella::Phase::Connecting || satellaPhase == Satella::Phase::FetchingKeys;
        if (satellaPopup && !ImGui::IsPopupOpen("Satella")) {
            ImGui::OpenPopup("Satella");
        }
        if (ImGui::BeginPopupModal("Satella", NULL,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            if (!satellaPopup) {
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::Text("%s", satellaPhase == Satella::Phase::Connecting ? "Connecting to Satella..."
                                                                             : "Getting mod signing keys...");
                ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(600.0f, 20.0f), "");
            }
            ImGui::EndPopup();
        }
#endif
        const bool modsPopup =
            extractStep == GS_COMPILE && (!extractDone || (!modError.empty() && !compileErrorDismissed));
        if (modsPopup && !ImGui::IsPopupOpen("Loading Mods")) {
            ImGui::OpenPopup("Loading Mods");
        }
        if (modsPopup) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
            auto color = UIWidgets::ColorValues.at(THEME_COLOR);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(color.x, color.y, color.z, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
            if (ImGui::BeginPopupModal("Loading Mods", NULL,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoSavedSettings)) {
                const size_t built = compileCount;
                const float progress = totalScripts > 0 ? std::min(1.0f, (float)built / (float)totalScripts) : 0.0f;
                if (modError.empty()) {
                    ImGui::Text("Building %s...", modName.empty() ? "mods" : modName.c_str());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Some mods failed to build:");
                }
                const std::string overlay = fmt::format("{}/{}", built, totalScripts);
                ImGui::ProgressBar(progress, ImVec2(600.0f, 50.0f), overlay.c_str());
                if (!modError.empty()) {
                    ImGui::Spacing();
                    ImGui::BeginChild("##modError", ImVec2(600.0f, 110.0f), ImGuiChildFlags_Borders);
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(modError.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    if (extractDone && ImGui::Button("Continue", ImVec2(600.0f, 0.0f))) {
                        compileErrorDismissed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        gui->EndDraw();
        gsFast3dWindow->EndFrame();
        ImGui::PopStyleColor(2);
#ifdef __EMSCRIPTEN__
        WaitForPresentSlot(60);
#endif
    }
    threadPool = nullptr;

    CheckAndCreateModFolder();
    if (menuWasVisible) {
        gui->GetMenu()->Show();
    }
}

ImFont* GameEngine::CreateFontWithSize(float size, std::string fontPath) {
    auto mImGuiIo = &ImGui::GetIO();
    ImFont* font;
    // Rasterize the glyph atlas at higher density so menu text stays sharp on HiDPI displays
    // (Retina, and browsers on such displays). Bake at backingScale * maxMenuScale so the runtime
    // ImGui Menu Scaling setting (FontGlobalScale) only ever downsamples the atlas rather than
    // stretching it blurry. lus reports 1 on standard-DPI displays, which keeps this a no-op there.
    float rasterDensity = 1.0f;
    constexpr float kMaxMenuScale = 2.0f; // keep in sync with imguiScaleOptionToValue's max
    const float backingScale = ShipCompat::GetWindow()->GetGui()->GetDpiScale();
    if (backingScale > 1.0f) {
        rasterDensity = backingScale * kMaxMenuScale;
    }
    if (fontPath == "") {
        ImFontConfig fontCfg = ImFontConfig();
        fontCfg.OversampleH = fontCfg.OversampleV = 1;
        fontCfg.PixelSnapH = true;
        fontCfg.SizePixels = size;
        fontCfg.RasterizerDensity = rasterDensity;
        font = mImGuiIo->Fonts->AddFontDefault(&fontCfg);
    } else {
        auto initData = std::make_shared<Ship::ResourceInitData>();
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.RasterizerDensity = rasterDensity;

        initData->Format = RESOURCE_FORMAT_BINARY;
        initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
        initData->ResourceVersion = 0;
        initData->Path = fontPath;
        std::shared_ptr<Ship::Font> fontData = std::static_pointer_cast<Ship::Font>(
            ShipCompat::GetResourceManager()->LoadResource(fontPath, false, initData));
        font = mImGuiIo->Fonts->AddFontFromMemoryTTF(fontData->Data, fontData->DataSize, size, &config);
    }
    // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly
    float iconFontSize = size * 2.0f / 3.0f;
    static const ImWchar sIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = iconFontSize;
    iconsConfig.RasterizerDensity = rasterDensity;
    mImGuiIo->Fonts->AddFontFromMemoryCompressedBase85TTF(fontawesome_compressed_data_base85, iconFontSize,
                                                          &iconsConfig, sIconsRanges);

    return font;
}

void GameEngine::ScaleImGui() {
    int32_t imGuiScaleIndex = CVarGetInteger("gSettings.ImGuiScale", defaultImGuiScale);
    if (imGuiScaleIndex == previousImGuiScaleIndex) {
        return;
    }

    float scale = imguiScaleOptionToValue[imGuiScaleIndex];
    float newScale = scale / previousImGuiScale;
    ImGui::GetStyle().ScaleAllSizes(newScale);
    ImGui::GetIO().FontGlobalScale = scale;
    previousImGuiScale = scale;
    previousImGuiScaleIndex = imGuiScaleIndex;
}

void GameEngine::Create(int argc, char* argv[]) {
    const auto instance = Instance = new GameEngine();
    instance->RunExtract(argc, argv);
    instance->FinishInit();
}

void GameEngine::Destroy() {
    PortGui::Destroy();
    // Persist the window state (fullscreen, size, position) here. GameEngine::Instance is never deleted, so the
    // Ship::Context it holds is never destroyed and the save in Context::~Context never runs on quit.
    if (auto window = ShipCompat::GetWindow()) {
        window->SaveWindowToConfig();
    }
    if (auto config = ShipCompat::GetConfig()) {
        config->Save();
    }
    gsFast3dWindow = nullptr;
    AudioExit();
    auto& pool = GameEngine::Instance->memoryPool;

    for (auto& entry : pool) {
        if (entry.addr != nullptr) {
            std::free(entry.addr);
        }
    }

    pool.clear();
}

void GameEngine::StartFrame() const {
    using Ship::KbScancode;
#ifdef __EMSCRIPTEN__
    static std::string sWebFile;
    std::string webFile;
    while (WebFiles_Take(webFile)) {
        if (auto drop = ShipCompat::GetFileDropMgr()) {
            sWebFile = webFile;
            drop->SetDroppedFile(sWebFile.data());
            drop->CallHandlers();
        }
    }
#endif
    const int32_t dwScancode = ShipCompat::GetWindow()->GetLastScancode();
    ShipCompat::GetWindow()->SetLastScancode(-1);

    if (dwScancode != -1) {
        CALL_EVENT(KeyboardInput, (uint32_t)dwScancode);
    }

    switch (dwScancode) {
        case KbScancode::LUS_KB_TAB: {
            // Toggle HD Assets
            CVarSetInteger("gEnhancements.Mods.AlternateAssets",
                           !CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0));
            break;
        }
        default:
            break;
    }
}

int32_t GameEngine::GetFrameRateMode() {
    int32_t mode = CVarGetInteger(CVAR_SETTING("FrameRateMode"), FRAME_RATE_60);

    if (mode < FRAME_RATE_ORIGINAL || mode > FRAME_RATE_CUSTOM) {
        return FRAME_RATE_60;
    }
    return mode;
}

uint32_t GameEngine::GetOriginalFPS() {
    return 60 / std::max<uint32_t>(sFrameDivisor, 1);
}

uint32_t GameEngine::GetInterpolationFPS() {
    auto window = ShipCompat::GetWindow();
    uint32_t refreshRate = window->GetCurrentRefreshRate();
    uint32_t fps;

    switch (GetFrameRateMode()) {
        case FRAME_RATE_ORIGINAL:
            return GetOriginalFPS();
        case FRAME_RATE_MATCH_REFRESH_RATE:
        case FRAME_RATE_UNCAPPED:
            return refreshRate;
        case FRAME_RATE_120:
            fps = 120;
            break;
        case FRAME_RATE_144:
            fps = 144;
            break;
        case FRAME_RATE_CUSTOM:
            if (CVarGetInteger(CVAR_SETTING("MatchRefreshRate"), 0)) {
                return refreshRate;
            }
            fps = CVarGetInteger(CVAR_SETTING("InterpolationFPS"), 60);
            break;
        case FRAME_RATE_60:
        default:
            fps = 60;
            break;
    }
    if (CVarGetInteger(CVAR_VSYNC_ENABLED, 1) || !window->CanDisableVerticalSync()) {
        return std::min<uint32_t>(refreshRate, fps);
    }
    return fps;
}

bool FrameInterpolation_IsActive(void) {
    return GameEngine::GetFrameRateMode() != FRAME_RATE_ORIGINAL;
}

namespace {

using FrameClock = std::chrono::steady_clock;

bool PrecisePacingEnabled() {
#ifdef __EMSCRIPTEN__
    return true;
#else
    return !CVarGetInteger(CVAR_VSYNC_ENABLED, 1) && ShipCompat::GetWindow()->CanDisableVerticalSync();
#endif
}

void WaitForPresentSlot(int fps) {
    static FrameClock::time_point sDeadline;
    static bool sValid = false;
    const auto interval = std::chrono::duration_cast<FrameClock::duration>(std::chrono::duration<double>(1.0 / fps));
    auto now = FrameClock::now();

    if (!sValid || now - sDeadline > interval * 4 || sDeadline - now > interval * 2) {
        sDeadline = now;
        sValid = true;
    }
#ifdef __EMSCRIPTEN__
    WebFrame_Wait(now < sDeadline ? std::chrono::duration<double, std::milli>(sDeadline - now).count() : 0.0);
    sWebYielded = true;
#else
    if (now < sDeadline) {
        if (sDeadline - now > std::chrono::milliseconds(2)) {
            std::this_thread::sleep_until(sDeadline - std::chrono::milliseconds(1));
        }
        while (FrameClock::now() < sDeadline) {
            std::this_thread::yield();
        }
    }
#endif
    sDeadline += interval;
}

} // namespace

// Audio

void GameEngine::HandleAudioThread() {
    while (audio.running) {
        {
            std::unique_lock<std::mutex> Lock(audio.mutex);
            while (!audio.processing && audio.running) {
                audio.cv_to_thread.wait(Lock);
            }

            if (!audio.running) {
                break;
            }
        }
        std::unique_lock<std::mutex> Lock(audio.mutex);

        audio.processing = false;
        audio.cv_from_thread.notify_one();
    }
}

void GameEngine::EndWebFrame() {
#ifdef __EMSCRIPTEN__
    if (!sWebYielded) {
        WebFrame_Wait(0.0);
    }
    sWebYielded = false;
#endif
}

void GameEngine::StartAudioFrame() {
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        audio.processing = true;
    }

    audio.cv_to_thread.notify_one();
}

void GameEngine::EndAudioFrame() {
    {
        std::unique_lock<std::mutex> Lock(audio.mutex);
        while (audio.processing) {
            audio.cv_from_thread.wait(Lock);
        }
    }
}

extern "C" void GameEngine_LockAudioThread() {
    audio.mutex.lock();
}

extern "C" void GameEngine_UnlockAudioThread() {
    audio.mutex.unlock();
}

void GameEngine::AudioInit() {
    if (!audio.running) {
        audio.running = true;
        audio.thread = std::thread(HandleAudioThread);
    }
}

void GameEngine::AudioExit() {
    {
        std::unique_lock lock(audio.mutex);
        audio.running = false;
    }
    audio.cv_to_thread.notify_all();

    // Wait until the audio thread quit
    if (audio.thread.joinable()) {
        audio.thread.join();
    }
}

static uint32_t ResolveGameVersion() {
    const auto versions = ShipCompat::GetResourceManager()->GetArchiveManager()->GetGameVersions();
    if (versions.empty()) {
        return 0;
    }
    const uint32_t raw = versions[0];
    if (raw == GAME_VERSION_GAFE01_00) {
        return raw;
    }
    return BSWAP32(raw);
}

uint32_t GameEngine::GetGameVersion() {
    return ResolveGameVersion();
}

bool GameEngine::IsGameArchiveLoaded() {
    return sGameArchiveLoaded;
}

void GameEngine::RunCommands(Gfx* Commands, const std::vector<FrameInterpolationResult>& replacements) {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ShipCompat::GetWindow());

    if (wnd == nullptr) {
        return;
    }

    auto interpreter = wnd->GetInterpreterWeak().lock().get();
    const bool precisePacing = PrecisePacingEnabled();
    const int targetFps = wnd->GetTargetFps();

    interpreter->mInterpolationIndex = 0;
    for (const auto& r : replacements) {
        // Process window events for resize, mouse, keyboard events
        wnd->HandleEvents();
        if (precisePacing && targetFps > 0) {
            WaitForPresentSlot(targetFps);
        }
        bool drawn = wnd->DrawAndRunGraphicsCommands(Commands, r.mtx, r.dl);
        interpreter->mInterpolationIndex++;
        if (!drawn) {
            break;
        }
    }

    bool curAltAssets = CVarGetInteger("gEnhancements.Mods.AlternateAssets", 0);
    if (prevAltAssets != curAltAssets) {
        prevAltAssets = curAltAssets;
        ShipCompat::GetResourceManager()->SetAltAssetsEnabled(curAltAssets);
        // Vanilla and HD are cached under distinct explicit paths (see AcquireDrawTexture), so
        // toggling needs no resource-cache purge — purging here freed/dirtied audio/soundfont
        // resources the engine holds raw pointers into and crashed. Just drop the GPU texture
        // cache + the interpreter's texture futures (via gfx_texture_cache_clear ->
        // TextureCacheClear) so the next frame re-resolves which path to draw.
        gfx_texture_cache_clear();
    }
}

void GameEngine::RunCommandsUncapped(Gfx* Commands, uint32_t originalFps) {
    static FrameClock::time_point sPeriodStart;
    static bool sPeriodValid = false;
    static double sFrameSeconds = 1.0 / 240.0;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ShipCompat::GetWindow());
    const double period = 1.0 / std::max<uint32_t>(originalFps, 1);
    const std::unordered_map<Mtx*, MtxF> noMtx;
    const std::unordered_map<Gfx*, Gfx*> noDl;

    if (wnd == nullptr) {
        return;
    }

    auto interpreter = wnd->GetInterpreterWeak().lock().get();
    auto now = FrameClock::now();
    if (!sPeriodValid || std::chrono::duration<double>(now - sPeriodStart).count() > period * 2.0) {
        sPeriodStart = now;
        sPeriodValid = true;
    }

    interpreter->mInterpolationIndex = 0;
    bool presented = false;
    while (true) {
        auto before = FrameClock::now();
        double t = std::chrono::duration<double>(before - sPeriodStart).count() + sFrameSeconds;
        if (presented && t >= period) {
            break;
        }

        float step = (float)(t / period);
        wnd->HandleEvents();
        bool interpolated = step < 0.999f;
        if (interpolated) {
            FrameInterpolationResult r = FrameInterpolation_Interpolate(std::max(step, 0.0f));
            wnd->DrawAndRunGraphicsCommands(Commands, r.mtx, r.dl);
        } else {
            wnd->DrawAndRunGraphicsCommands(Commands, noMtx, noDl);
        }
        interpreter->mInterpolationIndex++;
        presented = true;
#ifdef __EMSCRIPTEN__
        WebFrame_Wait(0.0);
        sWebYielded = true;
#endif

        auto after = FrameClock::now();
        double frameSeconds = std::chrono::duration<double>(after - before).count();
        sFrameSeconds = sFrameSeconds * 0.8 + std::min(frameSeconds, period) * 0.2;
        if (!interpolated) {
            break;
        }
    }

    sPeriodStart += std::chrono::duration_cast<FrameClock::duration>(std::chrono::duration<double>(period));
}

void GameEngine::ProcessGfxCommands(Gfx* commands) {
    static int sLastFps = 0;
    static int sLastOriginalFps = 0;
    static int sTime = 0;
    std::vector<FrameInterpolationResult> mtx_replacements;
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ShipCompat::GetWindow());
    const int originalFps = (int)GetOriginalFPS();
    const int mode = GetFrameRateMode();
    int fps = originalFps;

    if (mode == FRAME_RATE_UNCAPPED && !GfxDebuggerIsDebugging()) {
        if (wnd != nullptr) {
            wnd->SetTargetFps(1000);
            wnd->SetMaximumFrameLatency(1);
        }
        sLastFps = 0;
        RunCommandsUncapped(commands, originalFps);
        return;
    }

    if (mode != FRAME_RATE_ORIGINAL) {
        fps = std::max<int>((int)GetInterpolationFPS(), originalFps);
    }

    if (sLastFps != fps || sLastOriginalFps != originalFps) {
        sTime = 0;
    }

    // time_base = fps * original_fps (one second)
    int nextOriginalFrame = fps;

    while (sTime + originalFps <= nextOriginalFrame) {
        sTime += originalFps;
        if (sTime != nextOriginalFrame) {
            mtx_replacements.push_back(FrameInterpolation_Interpolate((float)sTime / nextOriginalFrame));
        } else {
            mtx_replacements.emplace_back();
        }
    }

    sTime -= fps;
    sLastFps = fps;
    sLastOriginalFps = originalFps;

    int threshold = CVarGetInteger(CVAR_SETTING("ExtraLatencyThreshold"), 80);

    if (wnd != nullptr) {
        wnd->SetTargetFps(fps);
        wnd->SetMaximumFrameLatency(threshold > 0 && fps >= threshold ? 2 : 1);
    }

    // When the gfx debugger is active, only run with the final mtx
    if (GfxDebuggerIsDebugging()) {
        mtx_replacements.clear();
        mtx_replacements.emplace_back();
    }

    RunCommands(commands, mtx_replacements);
}

bool GameEngine::IsAltAssetsEnabled() {
    return prevAltAssets;
}

extern "C" void GameEngine_ProcessGfxCommands(Gfx* commands) {
    GameEngine::ProcessGfxCommands(commands);
}

extern "C" void GameEngine_SetFrameDivisor(uint32_t divisor) {
    sFrameDivisor = divisor;
}

extern "C" uint32_t GameEngine_GetInterpolatedFPS() {
    return GameEngine::GetInterpolationFPS();
}

extern "C" uint32_t GameEngine_GetSampleRate() {
    auto player = ShipCompat::GetAudio()->GetAudioPlayer();
    if (player == nullptr) {
        return 0;
    }

    if (!player->IsInitialized()) {
        return 0;
    }

    return player->GetSampleRate();
}

extern "C" uint32_t GameEngine_GetSamplesPerFrame() {
    return SAMPLES_PER_FRAME;
}

// End

Fast::Interpreter* GameEngine_GetInterpreter() {
    return static_pointer_cast<Fast::Fast3dWindow>(ShipCompat::GetWindow())->GetInterpreterWeak().lock().get();
}

extern "C" float GameEngine_GetAspectRatio() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.aspect_ratio;
}

extern "C" float AC_WideScale(void) {
    auto interpreter = GameEngine_GetInterpreter();
    float scale = (OTRGetDimensionFromRightEdge(0.0f) - OTRGetDimensionFromLeftEdge(0.0f)) /
                  (float)interpreter->mNativeDimensions.width;
    return scale > 1.0f ? scale : 1.0f;
}

extern "C" uint32_t AC_RenderSizeKey(void) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mCurDimensions.width << 16) ^ interpreter->mCurDimensions.height;
}

extern "C" int AC_WideCount(float* carry) {
    float total = *carry + AC_WideScale();
    int count = (int)total;
    *carry = total - (float)count;
    return count;
}

extern "C" uint32_t GameEngine_GetGameVersion() {
    return ResolveGameVersion();
}

extern "C" bool GameEngine_IsGameArchiveLoaded() {
    return sGameArchiveLoaded;
}

extern "C" bool GameEngine_OTRSigCheck(const char* data) {
    return ShipCompat::GetResourceManager()->OtrSignatureCheck(data);
}

// Gets the width of the main ImGui window
extern "C" uint32_t OTRGetCurrentWidth() {
    return ShipCompat::GetWindow()->GetWidth();
}

// Gets the height of the main ImGui window
extern "C" uint32_t OTRGetCurrentHeight() {
    return ShipCompat::GetWindow()->GetHeight();
}

extern "C" float OTRGetHUDAspectRatio() {
    if (CVarGetInteger("gHUDAspectRatio.Enabled", 0) == 0 || CVarGetInteger("gHUDAspectRatio.X", 0) == 0 ||
        CVarGetInteger("gHUDAspectRatio.Y", 0) == 0) {
        return GameEngine_GetAspectRatio();
    }
    return ((float)CVarGetInteger("gHUDAspectRatio.X", 1) / (float)CVarGetInteger("gHUDAspectRatio.Y", 1));
}

extern "C" float OTRGetDimensionFromLeftEdge(float v) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 -
            interpreter->mNativeDimensions.height / 2 * interpreter->mCurDimensions.aspect_ratio + (v));
}

extern "C" float OTRGetDimensionFromRightEdge(float v) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 +
            interpreter->mNativeDimensions.height / 2 * interpreter->mCurDimensions.aspect_ratio - (v));
}

extern "C" float OTRGetDimensionFromLeftEdgeForcedAspect(float v, float aspectRatio) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 -
            interpreter->mNativeDimensions.height / 2 *
                (aspectRatio > 0 ? aspectRatio : interpreter->mCurDimensions.aspect_ratio) +
            (v));
}

extern "C" float OTRGetDimensionFromRightEdgeForcedAspect(float v, float aspectRatio) {
    auto interpreter = GameEngine_GetInterpreter();
    return (interpreter->mNativeDimensions.width / 2 +
            interpreter->mNativeDimensions.height / 2 *
                (aspectRatio > 0 ? aspectRatio : interpreter->mCurDimensions.aspect_ratio) -
            (v));
}

extern "C" float OTRGetDimensionFromLeftEdgeOverride(float v) {
    return OTRGetDimensionFromLeftEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" float OTRGetDimensionFromRightEdgeOverride(float v) {
    return OTRGetDimensionFromRightEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

// Gets the width of the current render target area
extern "C" uint32_t OTRGetGameRenderWidth() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.width;
}

// Gets the height of the current render target area
extern "C" uint32_t OTRGetGameRenderHeight() {
    auto interpreter = GameEngine_GetInterpreter();
    return interpreter->mCurDimensions.height;
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdge(float v) {
    return ((int)floorf(OTRGetDimensionFromLeftEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdge(float v) {
    return ((int)ceilf(OTRGetDimensionFromRightEdge(v)));
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdgeForcedAspect(float v, float aspectRatio) {
    return ((int)floorf(OTRGetDimensionFromLeftEdgeForcedAspect(v, aspectRatio)));
}

extern "C" int16_t OTRGetRectDimensionFromRightEdgeForcedAspect(float v, float aspectRatio) {
    return ((int)ceilf(OTRGetDimensionFromRightEdgeForcedAspect(v, aspectRatio)));
}

extern "C" int16_t OTRGetRectDimensionFromLeftEdgeOverride(float v) {
    return OTRGetRectDimensionFromLeftEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" int16_t OTRGetRectDimensionFromRightEdgeOverride(float v) {
    return OTRGetRectDimensionFromRightEdgeForcedAspect(v, OTRGetHUDAspectRatio());
}

extern "C" int32_t OTRConvertHUDXToScreenX(int32_t v) {
    auto interpreter = GameEngine_GetInterpreter();
    float gameAspectRatio = interpreter->mCurDimensions.aspect_ratio;
    int32_t gameHeight = interpreter->mCurDimensions.height;
    int32_t gameWidth = interpreter->mCurDimensions.width;
    float hudAspectRatio = 4.0f / 3.0f;
    int32_t hudHeight = gameHeight;
    int32_t hudWidth = hudHeight * hudAspectRatio;
    float hudScreenRatio = (hudWidth / (float)NATIVE_SCREEN_WIDTH);
    float hudCoord = v * hudScreenRatio;
    float gameOffset = (gameWidth - hudWidth) / 2;
    float gameCoord = hudCoord + gameOffset;
    float gameScreenRatio = ((float)NATIVE_SCREEN_WIDTH / gameWidth);
    float screenScaledCoord = gameCoord * gameScreenRatio;
    int32_t screenScaledCoordInt = screenScaledCoord;
    return screenScaledCoordInt;
}

extern "C" void* GameEngine_GetExactDataByName(const char* path) {
    auto asset = ShipCompat::GetResourceManager()->LoadResourceProcess(path, true);
    return asset ? static_cast<void*>(asset->GetRawPointer()) : nullptr;
}

extern "C" void* GameEngine_Malloc(size_t size) {
    auto& pool = GameEngine::Instance->memoryPool;

    uint8_t* ptr = static_cast<uint8_t*>(std::malloc(size));

    if (ptr) {
        pool.push_back({ ptr, size });
    }

    return ptr;
}

extern "C" void GameEngine_Free(void* ptr) {
    if (!ptr) {
        return;
    }

    auto& pool = GameEngine::Instance->memoryPool;

    for (size_t i = 0; i < pool.size(); ++i) {
        if (pool[i].addr == ptr) {
            std::free(pool[i].addr);

            if (i != pool.size() - 1) {
                std::swap(pool[i], pool.back());
            }

            pool.pop_back();
            return;
        }
    }
}
