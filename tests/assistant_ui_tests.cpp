#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "assistant_test_temp.h"
#include "../source/assistant_ui.cpp"

static std::vector<MCPTitleListType> fakeTitles;
static int fakeMcpHandle = 1;
static int fakeMcpCount = -2;
static int fakeMcpReportedCount = -1;
static int fakeMcpListError = 0;
static std::vector<int> fakeMcpCounts;
static bool fakeMcpSkipWrite = false;
static int allocations = 0;
static bool failSystemAllocation = false;
static VPADStatus fakeVpad{};
static KPADStatus fakeKpad{};
static bool hasKpad = false;

extern "C" int VPADRead(VPADChan, VPADStatus *status, uint32_t, VPADReadError *error)
{
    *status = fakeVpad;
    *error = VPAD_READ_SUCCESS;
    fakeVpad.trigger = 0;
    return 1;
}
extern "C" int KPADReadEx(KPADChan channel, KPADStatus *status, uint32_t, KPADError *error)
{
    *error = KPAD_ERROR_OK;
    if (channel != 0 || !hasKpad)
        return 0;
    *status = fakeKpad;
    hasKpad = false;
    return 1;
}
extern "C" void VPADGetTPCalibratedPointEx(VPADChan, int resolution, VPADTouchData *result, const VPADTouchData *raw)
{
    assert(resolution == VPAD_TP_854X480);
    *result = *raw;
}
extern "C" void OSGetSharedData(int, int, void **data, uint32_t *size)
{
    static std::vector<char> font;
    if (font.empty())
    {
        const char *configured = std::getenv("UIF_ASSISTANT_TEST_FONT");
        std::ifstream stream(configured && *configured ? configured : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", std::ios::binary);
        font.assign(std::istreambuf_iterator<char>(stream), {});
    }
    assert(!font.empty());
    *data = font.data();
    *size = static_cast<uint32_t>(font.size());
}
extern "C" void *OSAllocFromSystem(size_t bytes, int alignment)
{
    if (failSystemAllocation) return nullptr;
    void *result = nullptr;
    if (posix_memalign(&result, alignment, bytes) != 0)
        return nullptr;
    ++allocations;
    return result;
}
extern "C" void OSFreeToSystem(void *value)
{
    if (value)
        --allocations;
    std::free(value);
}
extern "C" MCPError MCP_Open() { return fakeMcpHandle; }
extern "C" MCPError MCP_Close(int) { return 0; }
extern "C" MCPError MCP_TitleCount(int)
{
    if (!fakeMcpCounts.empty()) {
        int count = fakeMcpCounts.front();
        fakeMcpCounts.erase(fakeMcpCounts.begin());
        return count;
    }
    return fakeMcpCount == -2 ? static_cast<int>(fakeTitles.size()) : fakeMcpCount;
}
extern "C" MCPError MCP_TitleList(int, uint32_t *count, MCPTitleListType *titles, uint32_t bytes)
{
    if (fakeMcpListError)
        return fakeMcpListError;
    assert(bytes >= fakeTitles.size() * sizeof(MCPTitleListType));
    if (!fakeMcpSkipWrite) std::copy(fakeTitles.begin(), fakeTitles.end(), titles);
    *count = fakeMcpReportedCount < 0 ? static_cast<uint32_t>(fakeTitles.size()) : fakeMcpReportedCount;
    return 0;
}

void reset_navigation()
{
    installActive = false;
    simpleUi = false;
    dashboardOpen = false;
    receiverOpen = false;
    settingsSaved = assistant_settings_defaults();
    settingsDraft = settingsSaved;
    settingsReady = true;
    settingsPending = settingsBusy = settingsSaveConfirm = false;
    settingsRevision = 0;
    settingsTab = settingsFocus = 0;
    portEditing = -1;
    startupActive = false;
    contentFocused = false;
    installConfirmOpen = false;
    launchConfirmOpen = false;
    autoInstallConfirmOpen = false;
    forgetDevicesConfirmOpen = false;
    pairingChangesAllowed = true;
    touchHeld = false;
    touchArmed = false;
    cachedInstallEnabled = true;
    cachedInstallGloballyEnabled = true;
    cachedAutoInstallEnabled = false;
    selectedPage = PackagesPageIndex;
    diagnosticTab = 0;
    diagnosticFocused = false;
    selectedPackageIndex = 0;
    selectedInstalledTitleIndex = 0;
    selectedInstallTarget = 1;
    cachedInstallPackages = {"Game One", "Game Two"};
    cachedInstalledTitles = {{"0005000012345678", "Game One", "NAND / MLC", "fs:/vol/storage_mlc01/usr/title/00050000/12345678"}};
    systemMemoryOnlyPackages.clear();
    pendingAction = {};
    storageRefreshRequested = false;
}

void navigation_tests()
{
    static_assert(PageCount == 5);
    reset_navigation();
    select_page(0);
    handle_navigation_buttons(VPAD_BUTTON_UP);
    assert(selectedPage == DiagnosticsPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(diagnosticFocused && !forgetDevicesConfirmOpen);
    handle_navigation_buttons(VPAD_BUTTON_RIGHT);
    assert(diagnosticTab == 1 && selectedPage == DiagnosticsPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_RIGHT);
    assert(diagnosticTab == 2 && assistant_ui_needs_log_tail());
    handle_navigation_buttons(VPAD_BUTTON_B);
    assert(!diagnosticFocused);
    handle_navigation_buttons(VPAD_BUTTON_DOWN);
    assert(selectedPage == 0 && !assistant_ui_needs_log_tail());
    reset_navigation();
    handle_navigation_buttons(VPAD_BUTTON_DOWN);
    assert(selectedPage == PackagesPageIndex + 1 && !contentFocused);
    select_page(PackagesPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(contentFocused && !installConfirmOpen);
    handle_navigation_buttons(VPAD_BUTTON_DOWN);
    assert(selectedPackageIndex == 1 && selectedPage == PackagesPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_B);
    assert(!contentFocused && selectedPage == PackagesPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_UP);
    assert(selectedPage == PackagesPageIndex - 1);

    reset_navigation();
    cachedInstallPackages.clear();
    handle_navigation_buttons(VPAD_BUTTON_DOWN);
    assert(selectedPage == PackagesPageIndex + 1);
    select_page(PackagesPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(!contentFocused && pendingAction.type == ASSISTANT_UI_ACTION_NONE);

    reset_navigation();
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(installConfirmOpen && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
    handle_navigation_buttons(VPAD_BUTTON_B);
    assert(!installConfirmOpen && contentFocused);
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_A);
    AssistantUiAction action{};
    assert(assistant_ui_take_action(&action));
    assert(action.type == ASSISTANT_UI_ACTION_INSTALL_TO_USB);
    assert(std::string(action.package_name) == "Game One");
    assert(!assistant_ui_take_action(&action));

    reset_navigation();
    assistant_ui_require_system_memory("Game One");
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_RIGHT);
    handle_touch_command({{}, TouchCommand::Usb, 0});
    assert(selectedInstallTarget == 0);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(assistant_ui_take_action(&action) && action.type == ASSISTANT_UI_ACTION_INSTALL_TO_NAND);

    reset_navigation();
    cachedInstallEnabled = false;
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(!installConfirmOpen && !assistant_ui_take_action(&action));

    reset_navigation();
    select_page(InstalledTitlesPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(contentFocused && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(launchConfirmOpen && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
    handle_navigation_buttons(VPAD_BUTTON_B);
    assert(contentFocused && !launchConfirmOpen);
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(assistant_ui_take_action(&action) && action.type == ASSISTANT_UI_ACTION_LAUNCH_TITLE);

    reset_navigation();
    assistant_ui_set_install_active(true);
    handle_navigation_buttons(VPAD_BUTTON_DOWN | VPAD_BUTTON_A);
    handle_touch_command({{}, TouchCommand::Page, 0});
    assert(selectedPage == PackagesPageIndex && !contentFocused && !assistant_ui_take_action(&action));
    assistant_ui_set_install_active(false);
    handle_navigation_buttons(VPAD_BUTTON_DOWN);
    assert(selectedPage == PackagesPageIndex + 1);
    assert(!assistant_ui_take_storage_refresh_request());
    handle_navigation_buttons(VPAD_BUTTON_X);
    assert(assistant_ui_take_storage_refresh_request());
    assert(!assistant_ui_take_storage_refresh_request());

    reset_navigation();
    simpleUi = true;
    handle_navigation_buttons(VPAD_BUTTON_X);
    assert(autoInstallConfirmOpen && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(assistant_ui_take_action(&action));
    assert(action.type == ASSISTANT_UI_ACTION_SET_AUTO_INSTALL && action.enabled);

    reset_navigation();
    cachedAutoInstallEnabled = true;
    select_page(ConfigPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(contentFocused && !autoInstallConfirmOpen);
    assert(!assistant_ui_take_action(&action));

    reset_navigation();
    simpleUi = true;
    cachedInstallGloballyEnabled = false;
    handle_navigation_buttons(VPAD_BUTTON_X);
    assert(!autoInstallConfirmOpen && !assistant_ui_take_action(&action));

    reset_navigation();
    simpleUi = true;
    handle_navigation_buttons(VPAD_BUTTON_Y);
    assert(dashboardOpen && !simpleUi && selectedPage == PackagesPageIndex);
    assert(!contentFocused && !assistant_ui_take_action(&action));
    handle_touch_command({{}, TouchCommand::Receiver, 0});
    assert(!dashboardOpen && simpleUi);
    handle_touch_command({{}, TouchCommand::Dashboard, 0});
    assert(dashboardOpen && !simpleUi);
    installActive = true;
    handle_navigation_buttons(VPAD_BUTTON_Y);
    handle_touch_command({{}, TouchCommand::Receiver, 0});
    assert(dashboardOpen && !assistant_ui_take_action(&action));
    installActive = false;
    handle_navigation_buttons(VPAD_BUTTON_Y);
    startupActive = true;
    handle_touch_command({{}, TouchCommand::Dashboard, 0});
    handle_navigation_buttons(VPAD_BUTTON_Y);
    assert(simpleUi && !dashboardOpen && !assistant_ui_take_action(&action));
    startupActive = false;
}

// Expected dimensions come from the authored test case, not cached SUT metrics.
static int fixtureOutputWidth = 0;
static int fixtureOutputHeight = 0;

static void resize_fixture(int width, int height)
{
    assert(width > 0 && height > 0);
    SDL_SetWindowSize(window, width, height);
    int actualWidth = 0;
    int actualHeight = 0;
    for (unsigned int attempt = 0; attempt < 1000; ++attempt)
    {
        // Window resize delivery can be asynchronous on a host backend.
        SDL_PumpEvents();
        // Service deferred software-renderer resize before checking its output.
        assert(SDL_RenderClear(renderer) == 0);
        SDL_RenderPresent(renderer);
        if (SDL_GetRendererOutputSize(renderer, &actualWidth, &actualHeight) == 0
            && actualWidth == width && actualHeight == height)
        {
            fixtureOutputWidth = width;
            fixtureOutputHeight = height;
            return;
        }
        SDL_Delay(1);
    }
    std::fprintf(stderr, "Renderer did not reach requested fixture resolution %dx%d; actual %dx%d: %s\n",
        width, height, actualWidth, actualHeight, SDL_GetError());
    std::abort();
}

void touch_tests()
{
    for (auto size : {std::pair<int, int>{854, 480}, {1280, 720}, {1920, 1080}, {640, 480}})
    {
        reset_navigation();
        resize_fixture(size.first, size.second);
        for (float safeScale : {1.0f, 0.8f})
        {
            update_render_metrics(safeScale);
            touchTargets.clear();
            add_touch_target(440, 320, 1300, 84, TouchCommand::Package, 1);
            float x = (renderOriginX + 600 * renderScale) * 854 / outputWidth;
            float y = (renderOriginY + 350 * renderScale) * 480 / outputHeight;
            assert(touch_target_at(x, y));
            handle_touch_sample(true, true, x, y);
            handle_touch_sample(true, true, x, y);
            assert(!contentFocused);
            handle_touch_sample(false, true, x, y);
            assert(contentFocused && selectedPackageIndex == 1 && !installConfirmOpen);
            handle_touch_sample(false, true, x, y);
            assert(!installConfirmOpen);

            contentFocused = false;
            handle_touch_sample(true, true, x, y);
            handle_touch_sample(true, true, 0, 0);
            handle_touch_sample(false, true, x, y);
            assert(!contentFocused);
            assert(!touch_target_at(-1, y) && !touch_target_at(854, y));
            if (size.first == 640)
                assert(!touch_target_at(400, 0));
        }
    }
}

void input_adapter_tests()
{
    for (int extension : std::array<int, 4>{0, WPAD_EXT_CLASSIC, WPAD_EXT_MPLUS_CLASSIC, WPAD_EXT_PRO_CONTROLLER})
    {
        reset_navigation();
        select_page(0);
        fakeVpad = {};
        fakeKpad = {};
        fakeKpad.extensionType = extension;
        if (extension == WPAD_EXT_PRO_CONTROLLER)
            fakeKpad.pro.trigger = WPAD_PRO_BUTTON_DOWN;
        else if (extension == WPAD_EXT_CLASSIC || extension == WPAD_EXT_MPLUS_CLASSIC)
            fakeKpad.classic.trigger = WPAD_CLASSIC_BUTTON_DOWN;
        else
            fakeKpad.trigger = WPAD_BUTTON_DOWN;
        hasKpad = true;
        assert(assistant_ui_poll_input());
        assert(selectedPage == 1 && !contentFocused);
        simpleUi = true;
        if (extension == WPAD_EXT_PRO_CONTROLLER)
            fakeKpad.pro.trigger = WPAD_PRO_BUTTON_Y;
        else if (extension == WPAD_EXT_CLASSIC || extension == WPAD_EXT_MPLUS_CLASSIC)
            fakeKpad.classic.trigger = WPAD_CLASSIC_BUTTON_Y;
        else
            fakeKpad.trigger = WPAD_BUTTON_2;
        hasKpad = true;
        assert(assistant_ui_poll_input());
        assert(dashboardOpen && !simpleUi && selectedPage == PackagesPageIndex && !contentFocused);
    }

    reset_navigation();
    fakeVpad.trigger = VPAD_BUTTON_DOWN;
    assert(assistant_ui_poll_input());
    assert(selectedPage == PackagesPageIndex + 1);
    select_page(PackagesPageIndex);
    resize_fixture(854, 480);
    update_render_metrics(1.0f);
    touchTargets.clear();
    add_touch_target(440, 320, 1300, 84, TouchCommand::Package, 1);
    fakeVpad.tpNormal = {300, 158, 1, VPAD_VALID};
    assistant_ui_poll_input();
    assert(!contentFocused);
    fakeVpad.tpNormal.touched = 0;
    assistant_ui_poll_input();
    assert(contentFocused && selectedPackageIndex == 1 && !installConfirmOpen);
    fakeVpad = {};
}

bool collect_title(const AssistantTitleLocation *title, void *context)
{
    auto *locations = static_cast<std::vector<AssistantTitleLocation> *>(context);
    locations->push_back(*title);
    return true;
}

void catalog_tests()
{
    fakeTitles.clear();
    for (auto value : {std::pair<const char *, uint64_t>{"/vol/storage_mlc01/usr/title/00050000/12345678", 0x0005000012345678ULL},
        {"/vol/storage_usb01/usr/title/00050002/11223344", 0x0005000211223344ULL},
        {"/vol/storage_mlc01/usr/title/00050010/12345678", 0x0005001012345678ULL},
        {"/vol/storage_odd01/usr/title/00050000/12345678", 0x0005000012345678ULL}})
    {
        MCPTitleListType title{};
        title.titleId = value.second;
        std::snprintf(title.path, sizeof(title.path), "%s", value.first);
        fakeTitles.push_back(title);
    }
    std::vector<AssistantTitleLocation> locations;
    assert(assistant_visit_title_catalog(collect_title, &locations, nullptr));
    assert(locations.size() == 2 && allocations == 0);
    assert(!locations[0].usb && locations[1].usb);
    assert(std::string(locations[0].title_id) == "0005000012345678");
    assert(std::string(locations[1].path) == "fs:/vol/storage_usb01/usr/title/00050002/11223344");
    auto diagnostic = assistant_catalog_diagnostics();
    assert(diagnostic.expected_count == 4 && diagnostic.returned_count == 4 && diagnostic.observed_count == 4);
    assert(diagnostic.capacity_bytes == 4 * sizeof(MCPTitleListType) && diagnostic.attempts == 1);
    failSystemAllocation = true;
    locations.clear();
    assert(assistant_visit_title_catalog(collect_title, &locations, nullptr));
    assert(locations.size() == 2 && allocations == 0);
    failSystemAllocation = false;

    fakeMcpHandle = -1;
    assert(!assistant_visit_title_catalog(collect_title, &locations, nullptr));
    assert(std::string(assistant_catalog_stage()) == "open" && assistant_catalog_error() == -1);
    fakeMcpHandle = 0;
    locations.clear();
    assert(assistant_visit_title_catalog(collect_title, &locations, nullptr));
    assert(locations.size() == 2);
    fakeMcpHandle = 1;
    fakeMcpCount = 5000;
    assert(!assistant_visit_title_catalog(collect_title, &locations, nullptr) && allocations == 0);
    fakeMcpCount = -2;

    fakeMcpReportedCount = static_cast<int>(fakeTitles.size()) + 1;
    assert(!assistant_visit_title_catalog(collect_title, &locations, nullptr) && allocations == 0);
    fakeMcpReportedCount = -1;
    fakeMcpReportedCount = static_cast<int>(fakeTitles.size()) - 1;
    assert(!assistant_visit_title_catalog(collect_title, &locations, nullptr) && allocations == 0);
    assert(std::string(assistant_catalog_stage()) == "count-changed");
    diagnostic = assistant_catalog_diagnostics();
    assert(diagnostic.expected_count == 4 && diagnostic.returned_count == 3 && diagnostic.observed_count == 4 && diagnostic.attempts == 3);
    fakeMcpReportedCount = -1;
    bool mismatch = false;
    locations.clear();
    fakeMcpCounts = {4, 5, 4, 4};
    assert(assistant_visit_title_catalog(collect_title, &locations, &mismatch) && !mismatch);
    assert(locations.size() == 2 && assistant_catalog_diagnostics().attempts == 2 && allocations == 0);
    locations.clear();
    fakeMcpCounts = {4, 5, 4, 5, 4, 5};
    assert(!assistant_visit_title_catalog(collect_title, &locations, &mismatch) && mismatch && locations.empty());
    assert(assistant_catalog_diagnostics().attempts == 3 && allocations == 0);
    fakeMcpCounts = {0, 4, 4, 4};
    assert(assistant_visit_title_catalog(collect_title, &locations, &mismatch) && !mismatch && locations.size() == 2);
    assert(assistant_catalog_diagnostics().attempts == 2 && allocations == 0);
    locations.clear();
    fakeMcpCounts = {0, 0};
    assert(assistant_visit_title_catalog(collect_title, &locations, &mismatch) && locations.empty() && !mismatch);
    assert(assistant_catalog_diagnostics().returned_count == 0 && allocations == 0);
    fakeMcpCounts = {4, -1};
    assert(!assistant_visit_title_catalog(collect_title, &locations, &mismatch) && mismatch && locations.empty());
    assert(std::string(assistant_catalog_stage()) == "count-after" && assistant_catalog_error() == -1 && allocations == 0);
    fakeMcpSkipWrite = true;
    assert(!assistant_visit_title_catalog(collect_title, &locations, &mismatch) && mismatch && locations.empty());
    assert(std::string(assistant_catalog_stage()) == "invalid-record" && allocations == 0);
    fakeMcpSkipWrite = false;
    fakeMcpListError = -1;
    assert(!assistant_visit_title_catalog(collect_title, &locations, nullptr) && allocations == 0);
    fakeMcpListError = 0;
    std::snprintf(fakeTitles.back().path, sizeof(fakeTitles.back().path), "%s", "/vol/storage_slc/sys/title/00050000/12345678");
    bool partial = false;
    assert(!assistant_visit_title_catalog(collect_title, &locations, &partial) && partial);
    assert(std::string(assistant_catalog_stage()) == "unrecognized-location");
    fakeTitles.pop_back();
    AssistantTitleLocation found{};
    assert(assistant_find_catalog_title("0005000211223344", &found) && found.usb);
    assert(!assistant_find_catalog_title("0005001012345678", &found));
    assert(!assistant_find_catalog_title("not-a-title", &found));
    bool truncated = false;
    assert(assistant_visit_title_catalog([](const AssistantTitleLocation *, void *) { return false; }, nullptr, &truncated));
    assert(truncated && allocations == 0);

    installedTitlesMlcReadable = false;
    installedTitlesUsbReadable = false;
    cachedInstalledTitles.clear();
    refresh_installed_titles();
    assert(titleCatalogAvailable && cachedInstalledTitles.size() == 2);
}

void save_frame(const std::filesystem::path &path)
{
    int actualWidth = 0;
    int actualHeight = 0;
    assert(SDL_GetRendererOutputSize(renderer, &actualWidth, &actualHeight) == 0);
    assert(actualWidth > 0 && actualHeight > 0);
    assert(actualWidth == fixtureOutputWidth && actualHeight == fixtureOutputHeight);
    // Capture must agree with the production layout's dimensions, rather than
    // silently validating a cropped image drawn with stale metrics.
    assert(outputWidth == actualWidth && outputHeight == actualHeight);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, actualWidth, actualHeight, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface && surface->pixels && surface->format);
    assert(surface->w == actualWidth && surface->h == actualHeight);
    assert(static_cast<size_t>(surface->format->BytesPerPixel) == sizeof(uint32_t));
    assert(surface->pitch > 0 && static_cast<size_t>(surface->pitch)
        >= static_cast<size_t>(surface->w) * sizeof(uint32_t));
    // An explicit allocation-bounded rectangle prevents a later backend resize
    // from expanding a nullptr/full-output read beyond this surface's buffer.
    SDL_Rect bounds{0, 0, surface->w, surface->h};
    assert(SDL_RenderReadPixels(renderer, &bounds, surface->format->format, surface->pixels, surface->pitch) == 0);
    int capturedWidth = 0;
    int capturedHeight = 0;
    assert(SDL_GetRendererOutputSize(renderer, &capturedWidth, &capturedHeight) == 0);
    assert(capturedWidth == actualWidth && capturedHeight == actualHeight);
    const auto *pixels = static_cast<const unsigned char *>(surface->pixels);
    uint32_t firstPixel = 0;
    std::memcpy(&firstPixel, pixels, sizeof(firstPixel));
    bool varied = false;
    size_t pixelCount = static_cast<size_t>(surface->w) * static_cast<size_t>(surface->h);
    for (size_t i = 100; i < pixelCount; i += 101)
    {
        size_t row = i / static_cast<size_t>(surface->w);
        size_t column = i % static_cast<size_t>(surface->w);
        uint32_t pixel = 0;
        std::memcpy(&pixel, pixels + row * static_cast<size_t>(surface->pitch)
            + column * sizeof(pixel), sizeof(pixel));
        varied |= pixel != firstPixel;
    }
    assert(varied);
    assert(SDL_SaveBMP(surface, path.string().c_str()) == 0);
    SDL_FreeSurface(surface);
}

void pairing_navigation_tests()
{
    reset_navigation();
    select_page(DiagnosticsPageIndex);
    handle_touch_command({{}, TouchCommand::DiagnosticTab, 3});
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(forgetDevicesConfirmOpen && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
    handle_navigation_buttons(VPAD_BUTTON_B);
    assert(!forgetDevicesConfirmOpen && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
    handle_touch_command({{}, TouchCommand::ForgetDevices, 0});
    assert(forgetDevicesConfirmOpen);
    handle_navigation_buttons(VPAD_BUTTON_A);
    AssistantUiAction action{};
    assert(assistant_ui_take_action(&action) && action.type == ASSISTANT_UI_ACTION_FORGET_DEVICES);
    reset_navigation();
    pairingChangesAllowed = false;
    select_page(DiagnosticsPageIndex);
    handle_touch_command({{}, TouchCommand::DiagnosticTab, 3});
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_touch_command({{}, TouchCommand::ForgetDevices, 0});
    assert(!forgetDevicesConfirmOpen && !assistant_ui_take_action(&action));
    reset_navigation();
}

void settings_tests(const char *output)
{
    reset_navigation();
    AssistantUiModel model{};
    model.settings = assistant_settings_defaults();
    model.ui_scale = 1.0f;
    model.install_enabled = model.manual_install_enabled = true;
    model.simple_ui = true;
    // Model paths have a smaller public bound than the C receiver fixtures.
    char root[sizeof(model.sd_root)];
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-settings-ui"));
    int root_length = std::snprintf(model.sd_root, sizeof(model.sd_root), "%s", root);
    assert(root_length >= 0 && static_cast<size_t>(root_length) < sizeof(model.sd_root));
    std::snprintf(model.connection_state, sizeof(model.connection_state), "Ready");
    std::snprintf(model.component_version, sizeof(model.component_version), "1.0.6-dev");
    std::snprintf(model.build_id, sizeof(model.build_id), "settings-candidate");
    model.http_port = 8573;
    assistant_ui_render(&model);
    assert(std::any_of(touchTargets.begin(), touchTargets.end(), [](const auto &t) { return t.command == TouchCommand::Settings; }));
    handle_touch_command({{}, TouchCommand::Settings, 0});
    assert(dashboardOpen && selectedPage == ConfigPageIndex && contentFocused);
    handle_touch_command({{}, TouchCommand::Setting, -1});
    assert(!settingsDraft.simple_ui && settings_dirty());
    handle_touch_command({{}, TouchCommand::Setting, 2});
    handle_touch_command({{}, TouchCommand::SettingDecrease, 3});
    assert(settingsDraft.allow_ui_scale && settingsDraft.ui_scale_percent == 99);
    settingsDraft.ui_scale_percent = 45;
    handle_touch_command({{}, TouchCommand::SettingDecrease, 3});
    assert(settingsDraft.ui_scale_percent == 45);
    settingsDraft.ui_scale_percent = 100;
    handle_touch_command({{}, TouchCommand::SettingIncrease, 3});
    assert(settingsDraft.ui_scale_percent == 100);
    handle_touch_command({{}, TouchCommand::SettingsTab, 1});
    handle_touch_command({{}, TouchCommand::Setting, 3});
    assert(portEditing == 3);
    for (int key : {11, 5, 4, 4, 2, 4, 13}) port_key(key); // 65535
    assert(portEditing == -1 && settingsDraft.http_port == 65535);
    handle_touch_command({{}, TouchCommand::Setting, 4});
    for (int key : {11, 5, 4, 4, 2, 5, 13}) port_key(key); // 65536 rejected
    assert(portEditing == 4 && settingsDraft.broadcast_port == 8573);
    port_key(11); port_key(10); port_key(13);
    assert(portEditing == 4 && !entered_port());
    port_key(9); port_key(0); port_key(13);
    assert(portEditing == -1 && settingsDraft.broadcast_port == 1);
    handle_touch_command({{}, TouchCommand::Setting, 3});
    port_key(11);
    handle_navigation_buttons(VPAD_BUTTON_B);
    assert(portEditing == -1 && settingsDraft.http_port == 65535);
    handle_touch_command({{}, TouchCommand::Setting, 1});
    assert(!settingsDraft.broadcast_enabled);
    handle_touch_command({{}, TouchCommand::Setting, 2});
    assert(!settingsDraft.passive_broadcast_enabled); // subordinate preference not toggled when disabled
    handle_touch_command({{}, TouchCommand::Setting, 1});
    handle_touch_command({{}, TouchCommand::Setting, 2});
    assert(settingsDraft.passive_broadcast_enabled);

    select_page(0);
    assistant_ui_render(&model);
    assert(settings_dirty() && settingsDraft.http_port == 65535); // navigation preserves unsaved edits
    handle_touch_command({{}, TouchCommand::Settings, 0});
    handle_touch_command({{}, TouchCommand::SettingsSave, 0});
    AssistantUiAction action{};
    assert(assistant_ui_take_action(&action) && action.type == ASSISTANT_UI_ACTION_SAVE_SETTINGS);
    assert(action.settings.http_port == 65535 && settingsPending);
    handle_touch_command({{}, TouchCommand::SettingsSave, 0});
    assert(!assistant_ui_take_action(&action));
    model.settings_revision++;
    model.settings_save_ok = false;
    std::snprintf(model.settings_message, sizeof(model.settings_message), "Cannot write settings to SD. No changes applied.");
    assistant_ui_render(&model);
    assert(!settingsPending && settings_dirty() && settingsDraft.http_port == 65535);
    handle_touch_command({{}, TouchCommand::SettingsSave, 0});
    assert(assistant_ui_take_action(&action));
    model.settings = action.settings;
    model.settings_revision++;
    model.settings_save_ok = true;
    model.settings_restart_required = true;
    model.settings_message[0] = 0;
    assistant_ui_render(&model);
    assert(!settingsPending && !settings_dirty() && model.http_port == 8573);

    handle_touch_command({{}, TouchCommand::SettingsTab, 2});
    handle_touch_command({{}, TouchCommand::Setting, 3});
    handle_touch_command({{}, TouchCommand::SettingsSave, 0});
    assert(autoInstallConfirmOpen && settingsSaveConfirm && !assistant_ui_take_action(&action));
    handle_navigation_buttons(VPAD_BUTTON_B);
    assert(!autoInstallConfirmOpen && settings_dirty());
    handle_touch_command({{}, TouchCommand::SettingsSave, 0});
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(assistant_ui_take_action(&action) && action.type == ASSISTANT_UI_ACTION_SAVE_SETTINGS && action.settings.auto_install_enabled);
    model.settings = action.settings; model.settings_revision++;
    assistant_ui_render(&model);
    assert(!settings_dirty());
    handle_touch_command({{}, TouchCommand::Setting, 1});
    handle_touch_command({{}, TouchCommand::Setting, 2});
    assert(!settingsDraft.install_enabled && settingsDraft.manual_install_enabled);
    handle_touch_command({{}, TouchCommand::SettingsCancel, 0});
    assert(!settings_dirty());
    model.settings_busy = true;
    assistant_ui_render(&model);
    handle_touch_command({{}, TouchCommand::Setting, 1});
    handle_touch_command({{}, TouchCommand::SettingsDefaults, 0});
    handle_touch_command({{}, TouchCommand::SettingsSave, 0});
    assert(!settings_dirty() && !assistant_ui_take_action(&action));
    model.settings_busy = false;
    assistant_ui_render(&model);
    handle_touch_command({{}, TouchCommand::SettingsDefaults, 0});
    assert(settings_dirty() && !assistant_ui_take_action(&action));
    handle_touch_command({{}, TouchCommand::SettingsCancel, 0});
    assert(!settings_dirty());

    // The same controls are reachable with a D-pad, even with installation disabled.
    model.settings.install_enabled = false;
    assistant_ui_render(&model);
    select_page(ConfigPageIndex);
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_DOWN);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(settingsDraft.install_enabled);
    handle_touch_command({{}, TouchCommand::SettingsCancel, 0});
    model.simple_ui = false;
    handle_touch_command({{}, TouchCommand::Receiver, 0});
    assistant_ui_render(&model);
    assert(simpleUi); // temporary receiver view also works with Dashboard startup

    std::filesystem::create_directories(output);
    for (auto size : {std::pair<int,int>{854,480}, {1280,720}, {1920,1080}, {640,480}})
    {
        resize_fixture(size.first, size.second);
        handle_touch_command({{}, TouchCommand::Settings, 0});
        for (int tab = 0; tab < 3; tab++)
        {
            handle_touch_command({{}, TouchCommand::SettingsTab, tab});
            assistant_ui_render(&model);
            for (const auto &t : touchTargets)
                assert(t.rect.x >= 0 && t.rect.y >= 0 && t.rect.x + t.rect.w <= DesignWidth && t.rect.y + t.rect.h <= DesignHeight);
            save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-settings-" + std::to_string(tab) + ".bmp"));
        }
        handle_touch_command({{}, TouchCommand::SettingsTab, 1});
        handle_touch_command({{}, TouchCommand::Setting, 3});
        assistant_ui_render(&model);
        assert(touchTargets.size() == 14);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-port-editor.bmp"));
        handle_navigation_buttons(VPAD_BUTTON_B);
    }
    reset_navigation();
    assert(std::filesystem::remove_all(std::filesystem::path(root)) >= 1);
}

void render_tests(const char *output)
{
    char root[sizeof(AssistantUiModel{}.sd_root)];
    assert(assistant_test_make_temp_directory(root, sizeof(root), "uif-assistant-ui-fixtures"));
    std::filesystem::path fixtures(root);
    std::filesystem::create_directories(fixtures / "install" / "Game One");
    for (const char *file : {"title.tik", "title.tmd", "title.cert", "00000000.app"})
        std::ofstream(fixtures / "install" / "Game One" / file) << "fixture";
    std::ofstream(fixtures / "install" / "not-a-package.txt") << "not a directory";
    AssistantUiModel model{};
    model.settings = assistant_settings_defaults();
    int root_length = std::snprintf(model.sd_root, sizeof(model.sd_root), "%s", root);
    assert(root_length >= 0 && static_cast<size_t>(root_length) < sizeof(model.sd_root));
    std::snprintf(model.ip_address, sizeof(model.ip_address), "192.168.68.107");
    std::snprintf(model.connection_state, sizeof(model.connection_state), "Ready");
    std::snprintf(model.status, sizeof(model.status), "Receiver ready.");
    std::snprintf(model.component_version, sizeof(model.component_version), "1.0.0-alpha");
    std::snprintf(model.build_id, sizeof(model.build_id), "interaction-tests");
    model.http_port = 8573;
    model.pairing_code = 123456;
    model.pairing_code_persisted = true;
    model.manual_install_enabled = true;
    model.install_enabled = true;
    model.ui_scale = 1.0f;
    model.sd_space_known = true;
    model.sd_free_bytes = 32LL * 1024 * 1024 * 1024;
    model.sd_total_bytes = 64LL * 1024 * 1024 * 1024;
    std::filesystem::create_directories(output);

    reset_navigation();
    select_page(PackagesPageIndex);
    assistant_ui_render(&model);
    assert(cachedInstallPackages.size() == 1);
    assert(cachedPackageValidation.at("Game One").rfind("Ready:", 0) == 0);
    auto fonts = fontCache.size();
    auto textures = textCache.size();
    assistant_ui_render(&model);
    assert(fontCache.size() == fonts && textCache.size() == textures);
    model.install_enabled = false;
    assistant_ui_render(&model);
    handle_navigation_buttons(VPAD_BUTTON_A);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(!installConfirmOpen && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
    model.install_enabled = true;
    std::filesystem::remove(fixtures / "install" / "Game One" / "title.cert");
    assistant_ui_render(&model);
    assert(cachedPackageValidation.at("Game One").rfind("Ready:", 0) == 0);
    assistant_ui_invalidate_storage();
    assistant_ui_render(&model);
    assert(cachedPackageValidation.at("Game One").find("title.cert") != std::string::npos);
    std::ofstream(fixtures / "install" / "Game One" / "title.cert") << "fixture";
    assistant_ui_invalidate_storage();

    const auto packageRoot = (fixtures / "install" / "Game One").string();
    assert(assistant_policy_write(packageRoot.c_str(), true));
    assistant_ui_render(&model);
    assert(cachedPackagePolicies.at("Game One") == ASSISTANT_POLICY_NAND);
    installConfirmOpen = true;
    selectedInstallTarget = 1;
    assistant_ui_render(&model);
    assert(selectedInstallTarget == 0);
    installConfirmOpen = false;
    std::ofstream(fixtures / "install" / "Game One" / "title.tmd", std::ios::app) << "replacement";
    assistant_ui_invalidate_storage();
    assistant_ui_render(&model);
    assert(cachedPackagePolicies.at("Game One") == ASSISTANT_POLICY_INVALID);
    installConfirmOpen = true;
    pendingAction.type = ASSISTANT_UI_ACTION_NONE;
    assistant_ui_render(&model);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(pendingAction.type == ASSISTANT_UI_ACTION_NONE && installConfirmOpen);
    installConfirmOpen = false;
    assert(assistant_policy_write(packageRoot.c_str(), false));
    assistant_ui_invalidate_storage();

    for (auto size : {std::pair<int, int>{854, 480}, {1280, 720}, {1920, 1080}, {640, 480}})
    {
        resize_fixture(size.first, size.second);
        for (int page = 0; page < PageCount; ++page)
        {
            select_page(page);
            assistant_ui_render(&model);
            assert(textCacheBytes <= TextCacheLimit);
            for (const auto &target : touchTargets)
                assert(target.rect.x >= 0 && target.rect.y >= 0
                    && target.rect.x + target.rect.w <= DesignWidth
                    && target.rect.y + target.rect.h <= DesignHeight);
            save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-page-" + std::to_string(page) + ".bmp"));
        }
        select_page(DiagnosticsPageIndex);
        for (int tab = 0; tab < DiagnosticTabCount; ++tab) {
            handle_touch_command({{}, TouchCommand::DiagnosticTab, tab});
            assistant_ui_render(&model);
            assert(assistant_ui_needs_log_tail() == (tab == 2));
            save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-diagnostics-" + std::to_string(tab) + ".bmp"));
        }
        assistant_ui_render(&model);
        handle_navigation_buttons(VPAD_BUTTON_A);
        assistant_ui_render(&model);
        assert(forgetDevicesConfirmOpen && touchTargets.size() == 2);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-forget-confirm.bmp"));
        model.transfer_active = true;
        assistant_ui_render(&model);
        assert(!forgetDevicesConfirmOpen);
        model.transfer_active = false;
        select_page(PackagesPageIndex);
        assistant_ui_render(&model);
        handle_navigation_buttons(VPAD_BUTTON_A);
        handle_navigation_buttons(VPAD_BUTTON_A);
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-confirm.bmp"));
        model.install_active = true;
        model.install_done_bytes = 1024;
        model.install_total_bytes = 2048;
        std::snprintf(model.status, sizeof(model.status), "Installing Game One to USB: 50%%.");
        assistant_ui_render(&model);
        assert(touchTargets.empty());
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-install.bmp"));
        model.install_active = false;
        assistant_ui_set_install_active(false);

        std::snprintf(model.status, sizeof(model.status), "Receiver ready.");
        model.simple_ui = true;
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-simple.bmp"));
        model.last_transfer_has_result = true;
        model.last_transfer_success = true;
        std::snprintf(model.last_transfer_package_display_name, sizeof(model.last_transfer_package_display_name), "Game One");
        std::snprintf(model.last_transfer_message, sizeof(model.last_transfer_message), "Installed to USB");
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-last-transfer-success.bmp"));
        model.last_transfer_success = false;
        std::snprintf(model.last_transfer_message, sizeof(model.last_transfer_message), "Installation failed. The package is still on SD.");
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-last-transfer-failed.bmp"));
        model.last_transfer_has_result = false;
        model.transfer_active = model.current_file_active = true;
        std::snprintf(model.connection_state, sizeof(model.connection_state), "Transfer Active");
        std::snprintf(model.status, sizeof(model.status), "Receiving Game One.");
        model.file_count = 30;
        model.uploaded_files = 3;
        model.total_bytes = 4503412137LL;
        model.uploaded_bytes = model.current_file_bytes = 1565720576LL;
        model.current_file_total_bytes = 2929393664LL;
        model.current_file_bytes_per_second = 635543;
        std::snprintf(model.package_display_name, sizeof(model.package_display_name), "UIF106 NSMB Original");
        std::snprintf(model.current_file_path, sizeof(model.current_file_path), "install/UIF106 NSMB Original/00000001.app");
        assert(progress_percent(model.current_file_bytes, model.current_file_total_bytes) == 53);
        assert(progress_percent(-1, 10) == 0 && progress_percent(11, 10) == 100 && progress_percent(0, 0) == 0);
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-receiving.bmp"));
        model.install_active = true;
        assistant_ui_render(&model);
        assert(touchTargets.empty());
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-install-receiving.bmp"));
        model.install_active = false;
        assistant_ui_set_install_active(false);
        model.current_file_idle_seconds = 12;
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-waiting-data.bmp"));
        model.verifying_files = true;
        std::snprintf(model.status, sizeof(model.status), "Verifying SD files.");
        model.uploaded_bytes = model.total_bytes;
        model.verified_bytes = model.current_file_bytes;
        model.current_file_idle_seconds = 0;
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-verifying.bmp"));
        model.simple_ui = false;
        receiverOpen = false;
        select_page(0);
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-receiver-verifying.bmp"));
        model.transfer_active = model.current_file_active = model.verifying_files = false;
        std::snprintf(model.connection_state, sizeof(model.connection_state), "Ready");
        std::snprintf(model.status, sizeof(model.status), "Receiver ready.");
        model.simple_ui = true;
        model.total_bytes = model.uploaded_bytes = 0;
        model.file_count = model.uploaded_files = 0;
        assistant_ui_render(&model);
        model.pairing_code_persisted = false;
        char code[32];
        format_pairing_code(&model, code, sizeof(code));
        assert(!strcmp(code, "Unavailable"));
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-pairing-unavailable.bmp"));
        model.pairing_code_persisted = true;
        model.pairing_code = 42;
        format_pairing_code(&model, code, sizeof(code));
        assert(!strcmp(code, "000042"));
        model.pairing_code = 123456;
        assert(std::any_of(touchTargets.begin(), touchTargets.end(), [](const auto &target) {
            return target.command == TouchCommand::Dashboard;
        }));
        handle_touch_command({{}, TouchCommand::Dashboard, 0});
        assistant_ui_render(&model);
        assert(dashboardOpen && !simpleUi && selectedPage == PackagesPageIndex);
        assert(!contentFocused && !installConfirmOpen && pendingAction.type == ASSISTANT_UI_ACTION_NONE);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-dashboard.bmp"));
        handle_navigation_buttons(VPAD_BUTTON_Y);
        assistant_ui_render(&model);
        assert(simpleUi && !dashboardOpen);
        handle_navigation_buttons(VPAD_BUTTON_X);
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-auto-install-confirm.bmp"));
        autoInstallConfirmOpen = false;

        model.startup_active = true;
        model.install_enabled = false;
        std::snprintf(model.connection_state, sizeof(model.connection_state), "Starting");
        std::snprintf(model.status, sizeof(model.status), "Connecting network.");
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-startup.bmp"));
        assert(touchTargets.empty());
        model.startup_active = false;
        model.install_enabled = true;
        model.simple_ui = false;
        receiverOpen = false;
        std::snprintf(model.connection_state, sizeof(model.connection_state), "Ready");
        std::snprintf(model.status, sizeof(model.status), "Receiver ready.");
    }
    assert(allocations == 0);
    assert(std::filesystem::remove_all(fixtures) >= 1);
}

int main(int argc, char **argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    assert(TTF_Init() == 0);
    OSGetSharedData(0, 0, &systemFontData, &systemFontSize);
    window = SDL_CreateWindow("UI test", 0, 0, 854, 480, SDL_WINDOW_HIDDEN);
    assert(window);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    assert(renderer);
    initialized = true;
    navigation_tests();
    pairing_navigation_tests();
    touch_tests();
    input_adapter_tests();
    catalog_tests();
    settings_tests(argc > 1 ? argv[1] : "build-host/screenshots");
    render_tests(argc > 1 ? argv[1] : "build-host/screenshots");
    assistant_ui_shutdown();
    std::cout << "Assistant navigation, touch, install lock, storage fallback, cache, and multi-resolution transfer/verification render checks passed." << std::endl;
}
