#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../source/assistant_ui.cpp"

static std::vector<MCPTitleListType> fakeTitles;
static int fakeMcpHandle = 1;
static int fakeMcpCount = -2;
static int fakeMcpReportedCount = -1;
static int fakeMcpListError = 0;
static int allocations = 0;
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
        std::ifstream stream("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", std::ios::binary);
        font.assign(std::istreambuf_iterator<char>(stream), {});
    }
    assert(!font.empty());
    *data = font.data();
    *size = static_cast<uint32_t>(font.size());
}
extern "C" void *OSAllocFromSystem(size_t bytes, int alignment)
{
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
extern "C" MCPError MCP_TitleCount(int) { return fakeMcpCount == -2 ? static_cast<int>(fakeTitles.size()) : fakeMcpCount; }
extern "C" MCPError MCP_TitleList(int, uint32_t *count, MCPTitleListType *titles, uint32_t bytes)
{
    if (fakeMcpListError)
        return fakeMcpListError;
    assert(bytes >= fakeTitles.size() * sizeof(MCPTitleListType));
    std::copy(fakeTitles.begin(), fakeTitles.end(), titles);
    *count = fakeMcpReportedCount < 0 ? static_cast<uint32_t>(fakeTitles.size()) : fakeMcpReportedCount;
    return 0;
}

void reset_navigation()
{
    installActive = false;
    simpleUi = false;
    dashboardOpen = false;
    startupActive = false;
    contentFocused = false;
    installConfirmOpen = false;
    launchConfirmOpen = false;
    autoInstallConfirmOpen = false;
    touchHeld = false;
    touchArmed = false;
    cachedInstallEnabled = true;
    cachedInstallGloballyEnabled = true;
    cachedAutoInstallEnabled = false;
    selectedPage = PackagesPageIndex;
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
    assert(autoInstallConfirmOpen);
    handle_navigation_buttons(VPAD_BUTTON_A);
    assert(assistant_ui_take_action(&action));
    assert(action.type == ASSISTANT_UI_ACTION_SET_AUTO_INSTALL && !action.enabled);

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

void touch_tests()
{
    for (auto size : {std::pair<int, int>{854, 480}, {1280, 720}, {1920, 1080}, {640, 480}})
    {
        reset_navigation();
        SDL_SetWindowSize(window, size.first, size.second);
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
    SDL_SetWindowSize(window, 854, 480);
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
        {"/vol/storage_slc/sys/title/00050000/12345678", 0x0005000012345678ULL}})
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

    fakeMcpHandle = -1;
    assert(!assistant_visit_title_catalog(collect_title, &locations, nullptr));
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
    fakeMcpListError = -1;
    assert(!assistant_visit_title_catalog(collect_title, &locations, nullptr) && allocations == 0);
    fakeMcpListError = 0;
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
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, outputWidth, outputHeight, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    assert(SDL_RenderReadPixels(renderer, nullptr, surface->format->format, surface->pixels, surface->pitch) == 0);
    auto *pixels = static_cast<uint32_t *>(surface->pixels);
    bool varied = false;
    for (int i = 100; i < outputWidth * outputHeight; i += 101)
        varied |= pixels[i] != pixels[0];
    assert(varied);
    assert(SDL_SaveBMP(surface, path.string().c_str()) == 0);
    SDL_FreeSurface(surface);
}

void render_tests(const char *output)
{
    std::filesystem::path fixtures = "/tmp/uif-assistant-ui-fixtures";
    std::filesystem::create_directories(fixtures / "install" / "Game One");
    for (const char *file : {"title.tik", "title.tmd", "title.cert", "00000000.app"})
        std::ofstream(fixtures / "install" / "Game One" / file) << "fixture";
    std::ofstream(fixtures / "install" / "not-a-package.txt") << "not a directory";
    AssistantUiModel model{};
    std::snprintf(model.sd_root, sizeof(model.sd_root), "%s", fixtures.string().c_str());
    std::snprintf(model.ip_address, sizeof(model.ip_address), "192.168.68.107");
    std::snprintf(model.connection_state, sizeof(model.connection_state), "Ready");
    std::snprintf(model.status, sizeof(model.status), "Receiver ready.");
    std::snprintf(model.component_version, sizeof(model.component_version), "1.0.0-alpha");
    std::snprintf(model.build_id, sizeof(model.build_id), "interaction-tests");
    model.http_port = 8573;
    model.pairing_code = 123456;
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
        SDL_SetWindowSize(window, size.first, size.second);
        for (int page = 0; page < PageCount; ++page)
        {
            select_page(page);
            assistant_ui_render(&model);
            assert(textCacheBytes <= TextCacheLimit);
            for (const auto &target : touchTargets)
                assert(target.rect.x >= 0 && target.rect.y >= 0
                    && target.rect.x + target.rect.w <= DesignWidth
                    && target.rect.y + target.rect.h <= DesignHeight);
            if (page == PackagesPageIndex || page == InstalledTitlesPageIndex || page == ConfigPageIndex || page == 0)
                save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-page-" + std::to_string(page) + ".bmp"));
        }
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

        model.simple_ui = true;
        assistant_ui_render(&model);
        save_frame(std::filesystem::path(output) / (std::to_string(size.first) + "-simple.bmp"));
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
        std::snprintf(model.connection_state, sizeof(model.connection_state), "Ready");
        std::snprintf(model.status, sizeof(model.status), "Receiver ready.");
    }
    assert(allocations == 0);
    std::filesystem::remove_all(fixtures);
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
    touch_tests();
    input_adapter_tests();
    catalog_tests();
    render_tests(argc > 1 ? argv[1] : "build-host/screenshots");
    assistant_ui_shutdown();
    std::cout << "Assistant navigation, touch, install lock, storage fallback, cache, and 60-page render checks passed." << std::endl;
}
