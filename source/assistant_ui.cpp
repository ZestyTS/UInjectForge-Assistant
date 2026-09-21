#include "assistant_ui.h"
#include "assistant_storage.h"
#include "assistant_policy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <sys/stat.h>

#ifdef UIF_ASSISTANT_HOST_TEST
#include "assistant_test_platform.h"
#else
#include <coreinit/memory.h>
#include <coreinit/systeminfo.h>
#include <padscore/kpad.h>
#include <vpad/input.h>
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

namespace
{
constexpr int DesignWidth = 1920;
constexpr int DesignHeight = 1080;
constexpr int WiiUWindowWidth = 1280;
constexpr int WiiUWindowHeight = 720;
constexpr float DefaultUiSafeScale = 1.00f;

SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
void *systemFontData = nullptr;
uint32_t systemFontSize = 0;
bool initialized = false;
bool refreshRequested = true;
bool storageRefreshRequested = false;
int selectedPage = 0;
int selectedPackageIndex = 0;
int selectedInstalledTitleIndex = 0;
int selectedInstallTarget = 1;
bool installConfirmOpen = false;
bool launchConfirmOpen = false;
bool autoInstallConfirmOpen = false;
bool contentFocused = false;
bool installActive = false;
bool simpleUi = false;
bool dashboardOpen = false;
bool startupActive = false;
bool touchHeld = false;
bool cachedInstallEnabled = false;
bool cachedInstallGloballyEnabled = false;
bool cachedAutoInstallEnabled = false;
bool packagesLoaded = false;
bool setupLoaded = false;
bool titleCatalogAvailable = false;
bool installedTitlesLoaded = false;
bool installedTitlesMlcReadable = false;
bool installedTitlesUsbReadable = false;
AssistantUiAction pendingAction{};
std::vector<std::string> cachedInstallPackages;
std::map<std::string, std::string> cachedPackageValidation;
std::map<std::string, AssistantPackagePolicy> cachedPackagePolicies;
std::vector<std::string> systemMemoryOnlyPackages;
std::string cachedSdRoot;
std::string lastCompletedTransfer;
std::vector<std::pair<std::string, bool>> cachedSetupRows;

struct InstalledTitleEntry
{
    std::string titleId;
    std::string name;
    std::string storage;
    std::string path;
};

std::vector<InstalledTitleEntry> cachedInstalledTitles;
int outputWidth = DesignWidth;
int outputHeight = DesignHeight;
float renderScale = DefaultUiSafeScale;
int renderOriginX = 0;
int renderOriginY = 0;

struct TextTexture
{
    std::string text;
    int size;
    int maxWidth;
    uint32_t color;
    SDL_Texture *texture;
    int width;
    int height;
    uint64_t used;
};

std::map<int, TTF_Font *> fontCache;
std::vector<TextTexture> textCache;
size_t textCacheBytes = 0;
uint64_t textCacheClock = 0;
constexpr size_t TextCacheLimit = 8 * 1024 * 1024;

void clear_text_cache()
{
    for (const auto &entry : textCache)
        SDL_DestroyTexture(entry.texture);
    textCache.clear();
    textCacheBytes = 0;
}

SDL_Color White{244, 246, 255, 255};
SDL_Color Muted{188, 192, 210, 255};
SDL_Color Dim{132, 138, 158, 255};
SDL_Color Accent{123, 74, 216, 255};
SDL_Color AccentLight{141, 99, 224, 255};
SDL_Color AccentDark{87, 50, 163, 255};
SDL_Color AppBackground{9, 10, 15, 255};
SDL_Color TopBarBackground{21, 21, 32, 255};
SDL_Color PanelBackground{23, 23, 34, 255};
SDL_Color FieldBackground{38, 38, 58, 255};
SDL_Color Border{60, 52, 82, 255};
SDL_Color Ready{68, 210, 122, 255};
SDL_Color Warn{246, 194, 83, 255};
SDL_Color Danger{255, 107, 129, 255};

const char *PageNames[] = {
    "Overview",
    "Receiver",
    "Pairing",
    "Transfer",
    "Results",
    "Activity",
    "Packages",
    "Log",
    "Title Storage",
    "Network",
    "Health",
    "Setup Check",
    "Diagnostics",
    "Controls",
    "Config",
};

constexpr int PageCount = sizeof(PageNames) / sizeof(PageNames[0]);
constexpr int PackagesPageIndex = 6;
constexpr int InstalledTitlesPageIndex = 8;
constexpr int ConfigPageIndex = 14;
constexpr int SidebarVisibleCount = 8;
int sidebarStart = 0;

enum class TouchCommand { Page, SidebarUp, SidebarDown, Enter, Back, Refresh, Package, Title, Previous, Next, Confirm, Nand, Usb, AutoInstall, Dashboard, Receiver };
struct TouchTarget { SDL_Rect rect; TouchCommand command; int value; };
std::vector<TouchTarget> touchTargets;
TouchTarget pressedTouch{};
bool touchArmed = false;

void add_touch_target(int x, int y, int width, int height, TouchCommand command, int value = 0)
{
    touchTargets.push_back({{x, y, width, height}, command, value});
}

float normalize_ui_scale(float value)
{
    if (value < 0.45f || value > 1.00f)
        return DefaultUiSafeScale;

    return value;
}

void update_render_metrics(float requestedSafeScale)
{
    int width = 0;
    int height = 0;
    if (!renderer || SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || width <= 0 || height <= 0)
    {
        width = DesignWidth;
        height = DesignHeight;
    }

    outputWidth = width;
    outputHeight = height;

    float fitScale = std::min(
        static_cast<float>(outputWidth) / static_cast<float>(DesignWidth),
        static_cast<float>(outputHeight) / static_cast<float>(DesignHeight));
    renderScale = std::max(0.1f, fitScale * normalize_ui_scale(requestedSafeScale));
    renderOriginX = static_cast<int>(std::lround((outputWidth - (DesignWidth * renderScale)) * 0.5f));
    renderOriginY = static_cast<int>(std::lround((outputHeight - (DesignHeight * renderScale)) * 0.5f));
}

int scale_coord(int value)
{
    return static_cast<int>(std::lround(value * renderScale));
}

int scale_x(int value)
{
    return renderOriginX + scale_coord(value);
}

int scale_y(int value)
{
    return renderOriginY + scale_coord(value);
}

int scale_size(int value)
{
    return std::max(1, scale_coord(value));
}

int scale_font(int size)
{
    return std::max(12, scale_coord(size));
}

void draw_rect(int x, int y, int w, int h, SDL_Color color)
{
    SDL_Rect rect{scale_x(x), scale_y(y), scale_size(w), scale_size(h)};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void draw_panel(int x, int y, int w, int h, SDL_Color border)
{
    draw_rect(x, y, w, h, border);
    draw_rect(x + 3, y + 3, w - 6, h - 6, PanelBackground);
}

void draw_brand_mark(int x, int y)
{
    draw_rect(x + 4, y + 5, 7, 29, AccentLight);
    draw_rect(x + 27, y + 5, 7, 29, AccentLight);
    draw_rect(x + 11, y + 27, 16, 7, AccentLight);
    draw_rect(x, y + 12, 4, 5, AccentDark);
    draw_rect(x, y + 23, 4, 5, AccentDark);
    draw_rect(x + 34, y + 12, 4, 5, AccentDark);
    draw_rect(x + 34, y + 23, 4, 5, AccentDark);
    draw_rect(x + 35, y + 1, 5, 5, Warn);
    draw_rect(x + 41, y + 7, 4, 4, Warn);
    draw_rect(x + 29, y + 7, 4, 4, Warn);
}

TTF_Font *open_font(int size)
{
    auto found = fontCache.find(size);
    if (found != fontCache.end())
        return found->second;
    if (!systemFontData || systemFontSize == 0)
        return nullptr;

    SDL_RWops *rw = SDL_RWFromMem(systemFontData, static_cast<int>(systemFontSize));
    if (!rw)
        return nullptr;

    TTF_Font *font = TTF_OpenFontRW(rw, 1, size);
    if (font)
        fontCache.emplace(size, font);
    return font;
}

std::string fit_text(TTF_Font *font, const char *text, int maxWidth)
{
    if (!text)
        return {};

    std::string value(text);
    if (maxWidth <= 0 || !font)
        return value;

    int width = 0;
    int height = 0;
    if (TTF_SizeUTF8(font, value.c_str(), &width, &height) != 0 || width <= maxWidth)
        return value;

    while (value.size() > 4)
    {
        size_t end = value.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
            --end;
        value.resize(end);
        std::string candidate = value + "...";
        if (TTF_SizeUTF8(font, candidate.c_str(), &width, &height) == 0 && width <= maxWidth)
            return candidate;
    }

    return "...";
}

void draw_text(const char *text, int x, int y, int size, SDL_Color color = White, int maxWidth = 0)
{
    if (!text || !text[0])
        return;
    int scaledSize = scale_font(size);
    int scaledMaxWidth = maxWidth > 0 ? scale_size(maxWidth) : 0;
    uint32_t packedColor = (uint32_t(color.r) << 24) | (uint32_t(color.g) << 16)
        | (uint32_t(color.b) << 8) | color.a;
    for (auto &entry : textCache)
    {
        if (entry.size == scaledSize && entry.maxWidth == scaledMaxWidth
            && entry.color == packedColor && entry.text == text)
        {
            entry.used = ++textCacheClock;
            SDL_Rect target{scale_x(x), scale_y(y), entry.width, entry.height};
            SDL_RenderCopy(renderer, entry.texture, nullptr, &target);
            return;
        }
    }
    TTF_Font *font = open_font(scaledSize);
    if (!font)
        return;

    std::string fitted = fit_text(font, text, scaledMaxWidth);
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, fitted.c_str(), color);
    if (!surface)
        return;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture)
    {
        SDL_Rect target{scale_x(x), scale_y(y), surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, nullptr, &target);
        size_t bytes = size_t(surface->w) * size_t(surface->h) * 4;
        while (!textCache.empty() && (textCacheBytes + bytes > TextCacheLimit || textCache.size() >= 192))
        {
            auto oldest = std::min_element(textCache.begin(), textCache.end(), [](const TextTexture &a, const TextTexture &b) { return a.used < b.used; });
            textCacheBytes -= size_t(oldest->width) * size_t(oldest->height) * 4;
            SDL_DestroyTexture(oldest->texture);
            textCache.erase(oldest);
        }
        if (bytes <= TextCacheLimit)
        {
            textCache.push_back({text, scaledSize, scaledMaxWidth, packedColor, texture, surface->w, surface->h, ++textCacheClock});
            textCacheBytes += bytes;
        }
        else
            SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
}

void draw_button(const char *label, int x, int y, int width, TouchCommand command, int value = 0, bool selected = false, bool enabled = true)
{
    draw_rect(x, y, width, 64, selected ? AccentDark : FieldBackground);
    draw_text(label, x + 18, y + 18, 26, enabled ? White : Dim, width - 36);
    if (enabled)
        add_touch_target(x, y, width, 64, command, value);
}

void draw_label_value(const char *label, const char *value, int x, int y, int w)
{
    draw_text(label, x, y, 28, Muted, w);
    draw_text(value && value[0] ? value : "not set", x, y + 38, 40, White, w);
}

void draw_metric(const char *label, const char *value, int x, int y, int w)
{
    draw_rect(x, y, w, 94, FieldBackground);
    draw_text(label, x + 18, y + 12, 24, Dim, w - 36);
    draw_text(value, x + 18, y + 42, 34, White, w - 36);
}

void draw_compact_metric(const char *label, const char *value, int x, int y, int w)
{
    draw_rect(x, y, w, 72, FieldBackground);
    draw_text(label, x + 18, y + 10, 22, Dim, w - 36);
    draw_text(value, x + 18, y + 36, 28, White, w - 36);
}

void draw_status_pill(const char *text, SDL_Color color, int x, int y, int w)
{
    draw_rect(x, y, w, 56, color);
    draw_text(text, x + 22, y + 12, 29, SDL_Color{8, 12, 14, 255}, w - 44);
}

void draw_status_row(const char *label, const char *value, bool ok, int x, int y, int w)
{
    draw_rect(x, y, w, 48, FieldBackground);
    draw_rect(x, y, 12, 48, ok ? Ready : Warn);
    draw_text(label, x + 28, y + 10, 24, Muted, 360);
    draw_text(value, x + 420, y + 8, 28, ok ? White : Warn, w - 448);
}

void draw_activity_lines(const AssistantUiModel *model, int x, int y, int lineHeight, unsigned int maxLines, int maxWidth)
{
    unsigned int visible = std::min(model->activity_count, maxLines);
    unsigned int start = model->activity_count > visible ? model->activity_count - visible : 0;
    for (unsigned int i = 0; i < visible; i++)
        draw_text(model->activity_log[start + i], x, y + static_cast<int>(i) * lineHeight, 25, i + 1 == visible ? White : Muted, maxWidth);
}

void draw_topbar_connection(const AssistantUiModel *model)
{
    char endpoint[160];
    std::snprintf(endpoint, sizeof(endpoint), "%s:%d", model->ip_address[0] ? model->ip_address : "waiting", model->http_port);

    char pairing[96];
    std::snprintf(pairing, sizeof(pairing), "Code %06u", model->pairing_code);

    draw_text(endpoint, 980, 24, 30, White, 420);
    draw_text(model->paired ? "Paired" : pairing, 980, 66, 32, model->paired ? Ready : AccentLight, 420);
}

void draw_page_sidebar()
{
    draw_panel(54, 150, 330, 826, contentFocused ? Border : Accent);
    draw_text("Pages", 84, 178, 40, White, 250);

    if (selectedPage < sidebarStart)
        sidebarStart = selectedPage;
    if (selectedPage >= sidebarStart + SidebarVisibleCount)
        sidebarStart = selectedPage - SidebarVisibleCount + 1;
    for (int row = 0; row < SidebarVisibleCount; row++)
    {
        int i = sidebarStart + row;
        if (i >= PageCount)
            break;
        int y = 224 + row * 82;
        bool selected = i == selectedPage;
        draw_rect(82, y, 274, 72, selected ? AccentDark : FieldBackground);
        draw_text(PageNames[i], 104, y + 23, 26, White, 230);
        add_touch_target(82, y, 274, 72, TouchCommand::Page, i);
    }
    draw_button("Up", 82, 898, 126, TouchCommand::SidebarUp, 0, false, sidebarStart > 0);
    draw_button("Down", 220, 898, 136, TouchCommand::SidebarDown, 0, false, sidebarStart + SidebarVisibleCount < PageCount);
}

const char *safe(const char *value)
{
    return value && value[0] ? value : "-";
}

void size_text(long long bytes, char *buffer, size_t bufferSize)
{
    const char *unit = "B";
    double amount = static_cast<double>(bytes);
    if (bytes >= 1024ll * 1024ll * 1024ll)
    {
        amount /= 1024.0 * 1024.0 * 1024.0;
        unit = "GiB";
    }
    else if (bytes >= 1024ll * 1024ll)
    {
        amount /= 1024.0 * 1024.0;
        unit = "MiB";
    }
    else if (bytes >= 1024ll)
    {
        amount /= 1024.0;
        unit = "KiB";
    }

    std::snprintf(buffer, bufferSize, "%.1f %s", amount, unit);
}

constexpr int QrVersion = 4;
constexpr int QrSize = 33;
constexpr int QrDataCodewords = 80;
constexpr int QrEccCodewords = 20;

struct QrMatrix
{
    bool modules[QrSize][QrSize]{};
    bool reserved[QrSize][QrSize]{};
};

int gfExp[512]{};
int gfLog[256]{};
bool gfReady = false;

void init_gf()
{
    if (gfReady)
        return;

    int value = 1;
    for (int i = 0; i < 255; i++)
    {
        gfExp[i] = value;
        gfLog[value] = i;
        value <<= 1;
        if ((value & 0x100) != 0)
            value ^= 0x11D;
    }

    for (int i = 255; i < 512; i++)
        gfExp[i] = gfExp[i - 255];

    gfReady = true;
}

uint8_t gf_mul(uint8_t left, uint8_t right)
{
    if (left == 0 || right == 0)
        return 0;

    init_gf();
    return static_cast<uint8_t>(gfExp[gfLog[left] + gfLog[right]]);
}

void qr_set(QrMatrix &qr, int x, int y, bool dark, bool reserve)
{
    if (x < 0 || y < 0 || x >= QrSize || y >= QrSize)
        return;

    qr.modules[y][x] = dark;
    if (reserve)
        qr.reserved[y][x] = true;
}

void qr_draw_finder(QrMatrix &qr, int left, int top)
{
    for (int dy = -1; dy <= 7; dy++)
    {
        for (int dx = -1; dx <= 7; dx++)
        {
            int x = left + dx;
            int y = top + dy;
            if (x < 0 || y < 0 || x >= QrSize || y >= QrSize)
                continue;

            bool inPattern = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;
            bool dark = inPattern
                && (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
            qr_set(qr, x, y, dark, true);
        }
    }
}

void qr_draw_alignment(QrMatrix &qr, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            int distance = std::max(std::abs(dx), std::abs(dy));
            qr_set(qr, cx + dx, cy + dy, distance == 2 || distance == 0, true);
        }
    }
}

void qr_reserve_format(QrMatrix &qr)
{
    for (int i = 0; i <= 8; i++)
    {
        if (i != 6)
        {
            qr_set(qr, 8, i, false, true);
            qr_set(qr, i, 8, false, true);
        }
    }

    for (int i = 0; i < 8; i++)
        qr_set(qr, QrSize - 1 - i, 8, false, true);
    for (int i = 8; i < 15; i++)
        qr_set(qr, 8, QrSize - 15 + i, false, true);
}

void qr_draw_function_modules(QrMatrix &qr)
{
    qr_draw_finder(qr, 0, 0);
    qr_draw_finder(qr, QrSize - 7, 0);
    qr_draw_finder(qr, 0, QrSize - 7);

    for (int i = 8; i < QrSize - 8; i++)
    {
        bool dark = (i % 2) == 0;
        qr_set(qr, i, 6, dark, true);
        qr_set(qr, 6, i, dark, true);
    }

    qr_draw_alignment(qr, 26, 26);
    qr_set(qr, 8, (4 * QrVersion) + 9, true, true);
    qr_reserve_format(qr);
}

void qr_place_format(QrMatrix &qr)
{
    constexpr int format = 0b111011111000100; // ECC level L, mask 0.
    auto bit = [](int value, int index) { return ((value >> index) & 1) != 0; };

    for (int i = 0; i <= 5; i++)
        qr_set(qr, 8, i, bit(format, i), true);
    qr_set(qr, 8, 7, bit(format, 6), true);
    qr_set(qr, 8, 8, bit(format, 7), true);
    qr_set(qr, 7, 8, bit(format, 8), true);
    for (int i = 9; i < 15; i++)
        qr_set(qr, 14 - i, 8, bit(format, i), true);

    for (int i = 0; i < 8; i++)
        qr_set(qr, QrSize - 1 - i, 8, bit(format, i), true);
    for (int i = 8; i < 15; i++)
        qr_set(qr, 8, QrSize - 15 + i, bit(format, i), true);
}

std::array<uint8_t, QrEccCodewords + 1> qr_generator()
{
    std::array<uint8_t, QrEccCodewords + 1> generator{};
    generator[0] = 1;
    int degree = 0;
    for (int i = 0; i < QrEccCodewords; i++)
    {
        uint8_t coefficient = static_cast<uint8_t>(gfExp[i]);
        for (int j = degree; j >= 0; j--)
            generator[j + 1] ^= gf_mul(generator[j], coefficient);
        degree++;
    }

    return generator;
}

bool qr_create_data(const char *payload, std::array<uint8_t, QrDataCodewords> &data)
{
    std::vector<int> bits;
    auto appendBits = [&bits](unsigned int value, int count) {
        for (int i = count - 1; i >= 0; i--)
            bits.push_back((value >> i) & 1);
    };

    size_t length = payload ? std::strlen(payload) : 0;
    if (length > 255)
        return false;

    appendBits(0x4, 4);
    appendBits(static_cast<unsigned int>(length), 8);
    for (size_t i = 0; i < length; i++)
        appendBits(static_cast<unsigned char>(payload[i]), 8);

    int capacityBits = QrDataCodewords * 8;
    if (static_cast<int>(bits.size()) > capacityBits)
        return false;

    int terminator = std::min(4, capacityBits - static_cast<int>(bits.size()));
    for (int i = 0; i < terminator; i++)
        bits.push_back(0);
    while ((bits.size() % 8) != 0)
        bits.push_back(0);

    size_t codeword = 0;
    for (size_t i = 0; i < bits.size(); i += 8)
    {
        uint8_t value = 0;
        for (int b = 0; b < 8; b++)
            value = static_cast<uint8_t>((value << 1) | bits[i + b]);
        data[codeword++] = value;
    }

    uint8_t pads[] = {0xEC, 0x11};
    int padIndex = 0;
    while (codeword < data.size())
        data[codeword++] = pads[(padIndex++) & 1];

    return true;
}

std::array<uint8_t, QrEccCodewords> qr_ecc(const std::array<uint8_t, QrDataCodewords> &data)
{
    init_gf();
    auto generator = qr_generator();
    std::array<uint8_t, QrEccCodewords> ecc{};

    for (uint8_t value : data)
    {
        uint8_t factor = value ^ ecc[0];
        for (int i = 0; i < QrEccCodewords - 1; i++)
            ecc[i] = ecc[i + 1];
        ecc[QrEccCodewords - 1] = 0;

        for (int i = 0; i < QrEccCodewords; i++)
            ecc[i] ^= gf_mul(generator[i + 1], factor);
    }

    return ecc;
}

bool qr_build(const char *payload, QrMatrix &qr)
{
    std::array<uint8_t, QrDataCodewords> data{};
    if (!qr_create_data(payload, data))
        return false;

    qr = QrMatrix{};
    qr_draw_function_modules(qr);

    auto ecc = qr_ecc(data);
    std::vector<uint8_t> codewords;
    codewords.reserve(QrDataCodewords + QrEccCodewords);
    codewords.insert(codewords.end(), data.begin(), data.end());
    codewords.insert(codewords.end(), ecc.begin(), ecc.end());

    int bitIndex = 0;
    bool upward = true;
    for (int right = QrSize - 1; right > 0; right -= 2)
    {
        if (right == 6)
            right--;

        for (int vertical = 0; vertical < QrSize; vertical++)
        {
            int y = upward ? QrSize - 1 - vertical : vertical;
            for (int dx = 0; dx < 2; dx++)
            {
                int x = right - dx;
                if (qr.reserved[y][x])
                    continue;

                bool dark = false;
                if (bitIndex < static_cast<int>(codewords.size() * 8))
                    dark = ((codewords[bitIndex / 8] >> (7 - (bitIndex % 8))) & 1) != 0;
                bitIndex++;

                bool mask = ((x + y) % 2) == 0;
                qr_set(qr, x, y, dark ^ mask, false);
            }
        }

        upward = !upward;
    }

    qr_place_format(qr);
    return true;
}

void draw_qr_code(const char *payload, int x, int y, int scale)
{
    QrMatrix qr;
    if (!qr_build(payload, qr))
    {
        draw_text("QR unavailable", x, y, 28, Warn, 260);
        return;
    }

    int quiet = 4;
    int size = (QrSize + quiet * 2) * scale;
    draw_rect(x, y, size, size, SDL_Color{250, 250, 250, 255});
    SDL_Color dark{8, 12, 14, 255};
    for (int row = 0; row < QrSize; row++)
    {
        for (int col = 0; col < QrSize; col++)
        {
            if (qr.modules[row][col])
                draw_rect(x + (col + quiet) * scale, y + (row + quiet) * scale, scale, scale, dark);
        }
    }
}

void draw_progress(int x, int y, int w, int h, long long current, long long total)
{
    draw_rect(x, y, w, h, FieldBackground);
    int fill = 0;
    if (total > 0 && current > 0)
        fill = std::max(4, static_cast<int>((static_cast<double>(current) / static_cast<double>(total)) * w));
    fill = std::min(fill, w);
    if (fill > 0)
        draw_rect(x, y, fill, h, Accent);
}

void join_path(char *destination, size_t destinationSize, const char *root, const char *relative)
{
    if (!destination || destinationSize == 0)
        return;

    const char *safeRoot = root && root[0] ? root : "";
    const char *safeRelative = relative && relative[0] ? relative : "";
    const char *separator = "";
    size_t rootLength = std::strlen(safeRoot);
    if (rootLength > 0 && safeRoot[rootLength - 1] != '/' && safeRelative[0])
        separator = "/";

    std::snprintf(destination, destinationSize, "%s%s%s", safeRoot, separator, safeRelative);
}

std::vector<std::string> list_directories(const char *path, int maxItems)
{
    std::vector<std::string> items;
    DIR *dir = opendir(path);
    if (!dir)
        return items;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_name[0] == '.')
            continue;

        if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN)
            continue;
        if (entry->d_type == DT_UNKNOWN)
        {
            char child[512];
            join_path(child, sizeof(child), path, entry->d_name);
            struct stat info;
            if (stat(child, &info) != 0 || !S_ISDIR(info.st_mode))
                continue;
        }

        items.emplace_back(entry->d_name);
        if (static_cast<int>(items.size()) >= maxItems)
            break;
    }

    closedir(dir);
    std::sort(items.begin(), items.end());
    return items;
}

std::vector<std::string> list_sd_install_packages(const AssistantUiModel *model)
{
    char installPath[512];
    join_path(installPath, sizeof(installPath), model->sd_root, "install");
    return list_directories(installPath, 128);
}

bool string_ends_with_ignore_case(const std::string &value, const char *suffix)
{
    if (!suffix)
        return false;

    size_t suffixLength = std::strlen(suffix);
    if (value.size() < suffixLength)
        return false;

    return std::equal(
        suffix,
        suffix + suffixLength,
        value.end() - static_cast<std::ptrdiff_t>(suffixLength),
        value.end(),
        [](char left, char right) {
            return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
        });
}

bool file_exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

bool directory_exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool root_file_exists(const AssistantUiModel *model, const char *relative)
{
    if (!model || !model->sd_root[0])
        return false;

    char path[512];
    join_path(path, sizeof(path), model->sd_root, relative);
    return file_exists(path);
}

constexpr const char *PpssppRequiredAssetPaths[] = {
    "flash0/font/ltn0.pgf",
    "flash0/font/jpn0.pgf",
    "flash0/font/kr0.pgf",
    "flash0/font/ltn1.pgf",
    "flash0/font/ltn2.pgf",
    "flash0/font/ltn3.pgf",
    "flash0/font/ltn4.pgf",
    "flash0/font/ltn5.pgf",
    "flash0/font/ltn6.pgf",
    "flash0/font/ltn7.pgf",
    "flash0/font/ltn8.pgf",
    "flash0/font/ltn9.pgf",
    "flash0/font/ltn10.pgf",
    "flash0/font/ltn11.pgf",
    "flash0/font/ltn12.pgf",
    "flash0/font/ltn13.pgf",
    "flash0/font/ltn14.pgf",
    "flash0/font/ltn15.pgf",
    "ui_atlas.zim",
    "ui_atlas.meta",
    "ppge_atlas.zim",
    "ppge_atlas.meta",
    "Roboto-Condensed.ttf",
    "compat.ini",
    "langregion.ini",
    "lang/en_US.ini",
    "shaders/defaultshaders.ini",
};

bool root_ppsspp_assets_complete(const AssistantUiModel *model, const char *assetsRoot)
{
    for (const char *asset : PpssppRequiredAssetPaths)
    {
        char relative[512];
        join_path(relative, sizeof(relative), assetsRoot, asset);
        if (!root_file_exists(model, relative))
            return false;
    }

    return true;
}
bool root_directory_exists(const AssistantUiModel *model, const char *relative)
{
    if (!model || !model->sd_root[0])
        return false;

    char path[512];
    join_path(path, sizeof(path), model->sd_root, relative);
    return directory_exists(path);
}

bool root_directory_has_ini(const AssistantUiModel *model, const char *relative)
{
    if (!model || !model->sd_root[0])
        return false;

    char path[512];
    join_path(path, sizeof(path), model->sd_root, relative);
    DIR *dir = opendir(path);
    if (!dir)
        return false;

    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_name[0] != '.' && string_ends_with_ignore_case(entry->d_name, ".ini"))
        {
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

int count_files_with_suffix(const char *path, const char *suffix)
{
    int count = 0;
    DIR *dir = opendir(path);
    if (!dir)
        return count;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_name[0] != '.' && string_ends_with_ignore_case(entry->d_name, suffix))
            count++;
    }

    closedir(dir);
    return count;
}

std::string build_install_package_validation(const char *packagePath)
{
    char ticketPath[512];
    char tmdPath[512];
    char certPath[512];
    join_path(ticketPath, sizeof(ticketPath), packagePath, "title.tik");
    join_path(tmdPath, sizeof(tmdPath), packagePath, "title.tmd");
    join_path(certPath, sizeof(certPath), packagePath, "title.cert");

    bool hasTicket = file_exists(ticketPath);
    bool hasTmd = file_exists(tmdPath);
    bool hasCert = file_exists(certPath);
    int appCount = count_files_with_suffix(packagePath, ".app");
    int h3Count = count_files_with_suffix(packagePath, ".h3");
    if (hasTicket && hasTmd && hasCert && appCount > 0)
    {
        char summary[128];
        std::snprintf(summary, sizeof(summary), "Ready: %d app, %d h3", appCount, h3Count);
        return summary;
    }

    std::vector<std::string> missing;
    if (!hasTicket)
        missing.emplace_back("title.tik");
    if (!hasTmd)
        missing.emplace_back("title.tmd");
    if (!hasCert)
        missing.emplace_back("title.cert");
    if (appCount == 0)
        missing.emplace_back(".app files");

    std::string summary = "Missing ";
    for (size_t i = 0; i < missing.size(); i++)
    {
        if (i > 0)
            summary += ", ";
        summary += missing[i];
    }

    return summary;
}

void load_package_snapshot(const AssistantUiModel *model)
{
    if (packagesLoaded)
        return;
    std::string selected = selectedPackageIndex >= 0 && selectedPackageIndex < static_cast<int>(cachedInstallPackages.size())
        ? cachedInstallPackages[selectedPackageIndex] : "";
    cachedInstallPackages = list_sd_install_packages(model);
    cachedPackageValidation.clear();
    cachedPackagePolicies.clear();
    systemMemoryOnlyPackages.clear();
    auto found = std::find(cachedInstallPackages.begin(), cachedInstallPackages.end(), selected);
    selectedPackageIndex = found == cachedInstallPackages.end() ? 0 : static_cast<int>(found - cachedInstallPackages.begin());
    packagesLoaded = true;
}

const std::string &package_validation(const AssistantUiModel *model, const std::string &package)
{
    auto found = cachedPackageValidation.find(package);
    if (found != cachedPackageValidation.end())
        return found->second;
    char installPath[512];
    char packagePath[512];
    join_path(installPath, sizeof(installPath), model->sd_root, "install");
    join_path(packagePath, sizeof(packagePath), installPath, package.c_str());
    cachedPackagePolicies[package] = assistant_policy_read(packagePath);
    return cachedPackageValidation.emplace(package, build_install_package_validation(packagePath)).first->second;
}

std::string decode_xml_text(std::string value)
{
    const std::array<std::pair<const char *, const char *>, 5> entities = {{
        {"&amp;", "&"},
        {"&quot;", "\""},
        {"&apos;", "'"},
        {"&lt;", "<"},
        {"&gt;", ">"},
    }};

    for (const auto &entity : entities)
    {
        size_t position = 0;
        while ((position = value.find(entity.first, position)) != std::string::npos)
        {
            value.replace(position, std::strlen(entity.first), entity.second);
            position += std::strlen(entity.second);
        }
    }

    for (char &character : value)
    {
        if (character == '\r' || character == '\n' || character == '\t')
            character = ' ';
    }

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::string extract_xml_text(const std::string &xml, const char *tag)
{
    std::string openTag = std::string("<") + tag + ">";
    std::string closeTag = std::string("</") + tag + ">";
    size_t start = xml.find(openTag);
    if (start == std::string::npos)
        return {};
    start += openTag.size();
    size_t end = xml.find(closeTag, start);
    if (end == std::string::npos)
        return {};
    return decode_xml_text(xml.substr(start, end - start));
}

std::string read_installed_title_name(const char *titlePath)
{
    char metaDirectory[512];
    char metaPath[512];
    join_path(metaDirectory, sizeof(metaDirectory), titlePath, "meta");
    join_path(metaPath, sizeof(metaPath), metaDirectory, "meta.xml");
    FILE *file = std::fopen(metaPath, "rb");
    if (!file)
        return "(name unavailable)";

    std::vector<char> buffer(65536, 0);
    size_t read = std::fread(buffer.data(), 1, buffer.size() - 1, file);
    std::fclose(file);
    std::string xml(buffer.data(), read);
    for (const char *tag : {"longname_en", "shortname_en", "longname_ja", "shortname_ja"})
    {
        std::string name = extract_xml_text(xml, tag);
        if (!name.empty())
            return name;
    }

    return "(name unavailable)";
}

bool is_hex_component_8(const char *value)
{
    if (!value || std::strlen(value) != 8)
        return false;

    for (size_t i = 0; i < 8; i++)
    {
        if (!std::isxdigit(static_cast<unsigned char>(value[i])))
            return false;
    }

    return true;
}

bool append_installed_titles_at(const char *rootPath, const char *storage)
{
    DIR *root = opendir(rootPath);
    if (!root)
        return false;

    struct dirent *high;
    while ((high = readdir(root)) != nullptr)
    {
        if (!is_hex_component_8(high->d_name))
            continue;

        char highPath[512];
        join_path(highPath, sizeof(highPath), rootPath, high->d_name);
        if (!directory_exists(highPath))
            continue;

        DIR *lowDir = opendir(highPath);
        if (!lowDir)
            continue;

        struct dirent *low;
        while ((low = readdir(lowDir)) != nullptr)
        {
            if (cachedInstalledTitles.size() >= 512)
                break;
            if (!is_hex_component_8(low->d_name))
                continue;

            char titlePath[512];
            join_path(titlePath, sizeof(titlePath), highPath, low->d_name);
            if (!directory_exists(titlePath))
                continue;

            cachedInstalledTitles.push_back({
                std::string(high->d_name) + low->d_name,
                "",
                storage,
                titlePath,
            });
        }

        closedir(lowDir);
    }

    closedir(root);
    return true;
}

bool append_catalog_title(const AssistantTitleLocation *title, void *)
{
    if (title->usb ? installedTitlesUsbReadable : installedTitlesMlcReadable)
        return true;
    if (cachedInstalledTitles.size() >= 512)
        return false;
    cachedInstalledTitles.push_back({title->title_id, title->title_id, title->usb ? "USB" : "NAND / MLC", title->path});
    return true;
}

void refresh_installed_titles()
{
    cachedInstalledTitles.clear();
    installedTitlesMlcReadable = append_installed_titles_at("fs:/vol/storage_mlc01/usr/title", "NAND / MLC");
    installedTitlesUsbReadable = append_installed_titles_at("fs:/vol/storage_usb01/usr/title", "USB");
    titleCatalogAvailable = false;
    if (!installedTitlesMlcReadable || !installedTitlesUsbReadable)
        titleCatalogAvailable = assistant_visit_title_catalog(append_catalog_title, nullptr, nullptr);
    std::sort(cachedInstalledTitles.begin(), cachedInstalledTitles.end(), [](const InstalledTitleEntry &left, const InstalledTitleEntry &right) {
        if (left.storage != right.storage)
            return left.storage < right.storage;
        if (left.name != right.name)
            return left.name < right.name;
        return left.titleId < right.titleId;
    });
    selectedInstalledTitleIndex = std::clamp(selectedInstalledTitleIndex, 0, std::max(0, static_cast<int>(cachedInstalledTitles.size()) - 1));
    installedTitlesLoaded = true;
}
void draw_setup_check_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 702, Border);
    draw_text("Setup Check", 444, 178, 42, White, 620);
    draw_text("Read-only SD and homebrew readiness checks.", 444, 238, 28, Muted, 900);

    if (!setupLoaded)
    {
        bool sdReady = model->sd_root[0] != '\0' && directory_exists(model->sd_root);
        bool assistantReady =
            root_file_exists(model, "wiiu/apps/uinjectforge-assistant/uinjectforge-assistant.rpx") ||
            root_file_exists(model, "wiiu/apps/uinjectforge-assistant/uinjectforge-assistant.wuhb");
        bool bridgeReady = root_file_exists(model, "wiiu/environments/aroma/plugins/uinjectforge-aroma-bridge.wps");
        bool ftpReady =
            root_file_exists(model, "wiiu/environments/aroma/plugins/ftpiiu_plugin.wps") ||
            root_file_exists(model, "wiiu/environments/aroma/plugins/ftpiiu.wps") ||
            root_file_exists(model, "wiiu/environments/aroma/plugins/ftpiiu_everywhere.wps");
        bool nintendontReady =
            root_file_exists(model, "apps/nintendont/boot.dol") ||
            root_file_exists(model, "apps/Nintendont/boot.dol");
        bool noodsReady =
            root_file_exists(model, "wiiu/apps/noods/noods.rpx") ||
            root_file_exists(model, "wiiu/apps/noods/noods.wuhb") ||
            root_file_exists(model, "wiiu/apps/NooDS/noods.rpx") ||
            root_file_exists(model, "wiiu/apps/NooDS/noods.wuhb");
        bool ppssppRuntimeReady =
            root_file_exists(model, "wiiu/apps/ppsspp/PPSSPP.rpx") ||
            root_file_exists(model, "wiiu/apps/ppsspp/ppsspp.rpx") ||
            root_file_exists(model, "ppsspp/PPSSPP.rpx") ||
            root_file_exists(model, "ppsspp/ppsspp.rpx");
        bool ppssppAssetsReady =
            root_ppsspp_assets_complete(model, "wiiu/apps/ppsspp/assets") ||
            root_ppsspp_assets_complete(model, "ppsspp/assets");
        bool controllerReady =
            root_file_exists(model, "controller.ini") ||
            root_file_exists(model, "apps/nintendont/controller.ini") ||
            root_file_exists(model, "apps/Nintendont/controller.ini") ||
            root_directory_has_ini(model, "controllers");
        bool loadiineReady = root_directory_exists(model, "wiiu/games");
        bool installRootReady = root_directory_exists(model, "install");
        load_package_snapshot(model);
        int validPackages = 0;
        int invalidPackages = 0;
        for (const auto &package : cachedInstallPackages)
        {
            const auto &validation = package_validation(model, package);
            if (validation.rfind("Ready:", 0) == 0)
                validPackages++;
            else
                invalidPackages++;
        }

        char sdStatus[160];
        if (sdReady && model->sd_space_known)
        {
            char freeSpace[64];
            size_text(model->sd_free_bytes, freeSpace, sizeof(freeSpace));
            std::snprintf(sdStatus, sizeof(sdStatus), "%s free", freeSpace);
        }
        else
        {
            std::snprintf(sdStatus, sizeof(sdStatus), "%s", sdReady ? "mounted, free space unknown" : "not mounted");
        }

        char ppssppStatus[160];
        std::snprintf(
            ppssppStatus,
            sizeof(ppssppStatus),
            "%s",
            ppssppRuntimeReady && ppssppAssetsReady ? "runtime and asset files found" : ppssppRuntimeReady ? "runtime found, assets incomplete" : "optional runtime missing");
        char packageStatus[160];
        std::snprintf(
            packageStatus,
            sizeof(packageStatus),
            "%s; %d valid, %d incomplete",
            installRootReady ? "install folder ready" : "install folder missing",
            validPackages,
            invalidPackages);

        cachedSetupRows = {
            {sdStatus, sdReady},
            {assistantReady ? "installed on SD" : "missing app files", assistantReady},
            {bridgeReady ? "plugin found" : "optional plugin missing", true},
            {ftpReady ? "plugin found" : "optional FTP plugin missing", true},
            {nintendontReady ? "boot.dol found" : "needed for GCN tests", nintendontReady},
            {noodsReady ? "runtime found" : "optional runtime missing", true},
            {ppssppStatus, !ppssppRuntimeReady || ppssppAssetsReady},
            {controllerReady ? "custom config found" : "optional config missing", true},
            {loadiineReady ? "wiiu/games ready" : "optional folder missing", true},
            {packageStatus, installRootReady && invalidPackages == 0}
        };
        setupLoaded = true;
    }
    const char *labels[] = {"SD", "Assistant", "Aroma Bridge", "FTPiiU", "Nintendont", "NooDS", "PPSSPP", "Controller INI", "Loadiine", "WUP Packages"};
    for (size_t i = 0; i < cachedSetupRows.size(); ++i)
        draw_status_row(labels[i], cachedSetupRows[i].first.c_str(), cachedSetupRows[i].second, 444, 286 + int(i) * 52, 1280);
}

void draw_overview_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 232, AccentDark);
    draw_text("Overview", 444, 178, 42, White, 440);
    char endpoint[160];
    std::snprintf(
        endpoint,
        sizeof(endpoint),
        "%s:%d",
        model->ip_address[0] ? model->ip_address : "waiting",
        model->http_port);
    char pair[32];
    std::snprintf(pair, sizeof(pair), "%06u", model->pairing_code);
    draw_label_value("Receiver", safe(model->connection_state), 448, 248, 240);
    draw_label_value("Address", endpoint, 728, 248, 430);
    draw_label_value(model->paired ? "Pairing" : "Code", model->paired ? "Paired" : pair, 1196, 248, 230);
    draw_label_value("SD Root", safe(model->sd_root), 1454, 248, 360);

    draw_panel(414, 418, 706, 286, model->transfer_active ? Accent : Border);
    draw_text("Transfer", 444, 446, 36, White, 320);
    draw_text(model->transfer_active ? safe(model->package_display_name) : "No active transfer", 444, 500, 30, White, 610);
    char fileProgress[64];
    std::snprintf(fileProgress, sizeof(fileProgress), "%d / %d files", model->uploaded_files, model->file_count);
    char uploaded[32];
    char total[32];
    size_text(model->uploaded_bytes, uploaded, sizeof(uploaded));
    size_text(model->total_bytes, total, sizeof(total));
    draw_metric("Files", fileProgress, 444, 566, 240);
    draw_metric("Bytes", uploaded, 714, 566, 210);
    draw_progress(948, 600, 126, 28, model->uploaded_bytes, model->total_bytes);

    draw_panel(1160, 418, 706, 286, Border);
    draw_text("Diagnostics", 1190, 446, 36, White, 360);
    char http[96];
    std::snprintf(http, sizeof(http), "%u requests, %u errors", model->http_request_count, model->http_error_count);
    char udp[128];
    std::snprintf(udp, sizeof(udp), "%u probes, %u replies", model->discovery_probe_count, model->discovery_reply_count);
    draw_metric("HTTP", http, 1190, 510, 300);
    draw_metric("UDP", udp, 1520, 510, 280);
    draw_text(model->last_http_error[0] ? model->last_http_error : "No HTTP errors.", 1190, 636, 26, model->last_http_error[0] ? Danger : Muted, 620);

    draw_panel(414, 740, 1452, 236, Border);
    draw_text("Activity", 444, 764, 34, White, 260);
    draw_activity_lines(model, 444, 818, 30, 5, 1340);
}

void draw_pairing_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, AccentDark);
    draw_text("Pairing", 444, 178, 42, White, 420);
    draw_text(model->paired ? "Ready for transfers from UIF." : "Enter this code in UIF.", 444, 238, 30, Muted, 720);
    char pair[32];
    std::snprintf(pair, sizeof(pair), "%06u", model->pairing_code);
    draw_text(pair, 444, 306, 104, AccentLight, 500);
    draw_metric("State", model->paired ? "Paired" : "Not paired", 940, 276, 310);
    char attempts[96];
    std::snprintf(attempts, sizeof(attempts), "%u attempts", model->pairing_attempt_count);
    char failures[96];
    std::snprintf(failures, sizeof(failures), "%u failures", model->pairing_failure_count);
    draw_metric("Attempts", attempts, 1282, 276, 250);
    draw_metric("Failures", failures, 1564, 276, 240);
    draw_text("If pairing fails, run Check Status.", 444, 468, 28, White, 720);
    draw_text("Last HTTP error", 444, 560, 30, Muted, 360);
    draw_text(model->last_http_error[0] ? model->last_http_error : "None", 444, 604, 28, model->last_http_error[0] ? Danger : White, 840);

    char pairingUrl[160];
    std::snprintf(
        pairingUrl,
        sizeof(pairingUrl),
        "http://%s:%d/?c=%06u",
        model->ip_address[0] ? model->ip_address : "0.0.0.0",
        model->http_port,
        model->pairing_code);
    draw_qr_code(pairingUrl, 1460, 452, 4);
    draw_text("Status QR", 1450, 632, 22, Muted, 260);
    draw_text(pairingUrl, 1260, 676, 20, White, 560);
}

void draw_activity_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, Border);
    draw_text("Activity Log", 444, 178, 42, White, 520);
    draw_text("Newest events; also saved to SD.", 444, 238, 30, Muted, 760);
    draw_text(safe(model->log_path), 444, 292, 28, White, 1200);
    draw_rect(444, 358, 1350, 2, Accent);
    draw_activity_lines(model, 444, 394, 44, ASSISTANT_UI_ACTIVITY_LINES, 1320);
}

void draw_log_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 702, Border);
    draw_text("On-SD Log", 444, 178, 42, White, 520);
    draw_text(safe(model->log_path), 444, 238, 28, Muted, 1200);
    draw_rect(444, 296, 1350, 2, Accent);

    if (model->log_tail_count == 0)
    {
        draw_text("No SD log lines yet.", 444, 344, 34, Warn, 900);
        draw_text("Status, pairing, transfers, and errors are saved here.", 444, 398, 28, Muted, 1000);
        return;
    }

    unsigned int visible = std::min(model->log_tail_count, static_cast<unsigned int>(ASSISTANT_UI_LOG_LINES));
    for (unsigned int i = 0; i < visible; i++)
        draw_text(model->log_tail[i], 444, 334 + static_cast<int>(i) * 45, 23, i + 1 == visible ? White : Muted, 1320);
}

void draw_network_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, Border);
    draw_text("Network", 444, 178, 42, White, 480);
    char endpoint[160];
    std::snprintf(endpoint, sizeof(endpoint), "%s:%d", model->ip_address[0] ? model->ip_address : "waiting", model->http_port);
    char httpPort[64];
    char broadcastPort[64];
    std::snprintf(httpPort, sizeof(httpPort), "%d", model->http_port);
    std::snprintf(broadcastPort, sizeof(broadcastPort), "%d", model->broadcast_port);
    draw_metric("Address", endpoint, 444, 262, 500);
    draw_metric("HTTP", httpPort, 980, 262, 220);
    draw_metric("UDP", broadcastPort, 1236, 262, 220);
    draw_metric("UDP socket", model->broadcast_socket_ready ? "ready" : "off", 1492, 262, 260);
    draw_status_row("Discovery", model->broadcast_enabled ? "UDP replies on" : "off", model->broadcast_enabled, 444, 414, 1280);
    draw_status_row("Passive broadcast", model->passive_broadcast_enabled ? "enabled" : "off", !model->passive_broadcast_enabled, 444, 478, 1280);
    char counters[160];
    std::snprintf(counters, sizeof(counters), "%u probes, %u replies, %u broadcasts", model->discovery_probe_count, model->discovery_reply_count, model->discovery_broadcast_count);
    draw_status_row("Counters", counters, true, 444, 542, 1280);
    draw_status_row("Last peer", safe(model->last_discovery_peer), model->last_discovery_peer[0] != '\0', 444, 606, 1280);
}

void draw_health_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, Border);
    draw_text("Health", 444, 178, 42, White, 420);
    draw_status_row("SD", safe(model->sd_root), model->sd_root[0] != '\0', 444, 262, 1280);
    char space[128];
    char freeBytes[32];
    char totalBytes[32];
    size_text(model->sd_free_bytes, freeBytes, sizeof(freeBytes));
    size_text(model->sd_total_bytes, totalBytes, sizeof(totalBytes));
    std::snprintf(space, sizeof(space), model->sd_space_known ? "%s free of %s" : "unknown", freeBytes, totalBytes);
    draw_status_row("Free space", space, model->sd_space_known, 444, 326, 1280);
    draw_status_row("HTTP", model->http_port > 0 ? "ready" : "not ready", model->http_port > 0, 444, 390, 1280);
    draw_status_row("Pairing", model->paired ? "paired" : "waiting", model->paired, 444, 454, 1280);
    draw_status_row("Transfer", model->transfer_active ? safe(model->package_display_name) : "idle", true, 444, 518, 1280);
    char fit[128];
    char required[32];
    size_text(model->transfer_required_bytes, required, sizeof(required));
    std::snprintf(fit, sizeof(fit), model->transfer_active ? "%s required" : "no active transfer", required);
    draw_status_row("Capacity", fit, model->transfer_fits_sd, 444, 582, 1280);
    char heartbeat[96];
    std::snprintf(heartbeat, sizeof(heartbeat), "%u loop heartbeat(s)", model->loop_heartbeat_count);
    draw_status_row("Loop", heartbeat, true, 444, 646, 1280);
    draw_text("Read-only diagnostics.", 444, 740, 28, Muted, 600);
}

void draw_controls_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, Border);
    draw_text("Controls", 444, 178, 42, White, 480);
    draw_status_row("HOME", "open HOME Menu", true, 444, 262, 1280);
    draw_status_row("D-pad", "Choose page or list item", true, 444, 326, 1280);
    draw_status_row("A / B", "Open or confirm / Back", true, 444, 390, 1280);
    draw_status_row("Touch", "Tap a page, item, or action", true, 444, 454, 1280);
    draw_text("Detected controllers", 444, 508, 32, Muted, 460);
    draw_text(safe(model->controller_summary), 444, 558, 30, White, 1260);
    draw_text("Check Status includes this.", 444, 660, 28, Muted, 700);
}

void draw_receiver_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 356, AccentDark);
    draw_text("Connect", 444, 178, 42, White, 500);
    char endpoint[160];
    std::snprintf(endpoint, sizeof(endpoint), "%s:%d", model->ip_address[0] ? model->ip_address : "waiting", model->http_port);
    draw_label_value("Address", endpoint, 448, 250, 500);
    draw_text("Pairing code", 448, 358, 30, Muted, 420);
    char pair[32];
    std::snprintf(pair, sizeof(pair), "%06u", model->pairing_code);
    draw_text(pair, 448, 392, 76, AccentLight, 420);

    draw_label_value("SD root", safe(model->sd_root), 1040, 250, 540);
    draw_label_value("Discovery", model->broadcast_enabled ? "UDP replies on" : "off", 1040, 358, 280);
    draw_label_value("Passive UDP", model->passive_broadcast_enabled ? "on" : "off", 1364, 358, 280);

    draw_panel(414, 542, 1452, 244, Border);
    draw_text("Status", 444, 570, 40, White, 360);
    draw_text(safe(model->status), 444, 626, 34, White, 1330);
    draw_text("Open the UIF Assistant panel.", 444, 704, 28, Muted, 700);
    draw_text("HOME opens the Wii U Menu.", 1260, 704, 28, Muted, 500);
}

void draw_transfer_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 416, model->transfer_active ? Accent : Border);
    draw_text("Transfer", 444, 178, 42, White, 500);
    draw_text(model->transfer_active ? safe(model->package_display_name) : "No active transfer", 444, 242, 38, White, 960);
    draw_label_value("Kind", safe(model->package_kind), 444, 330, 320);
    draw_label_value("Transfer ID", safe(model->transfer_id), 444, 432, 360);
    draw_label_value("Target", safe(model->target_root), 840, 432, 520);
    draw_label_value("After", safe(model->post_transfer_action), 1400, 432, 340);

    char fileProgress[64];
    std::snprintf(fileProgress, sizeof(fileProgress), "%d / %d files", model->uploaded_files, model->file_count);
    char uploaded[32];
    char total[32];
    size_text(model->uploaded_bytes, uploaded, sizeof(uploaded));
    size_text(model->total_bytes, total, sizeof(total));
    char byteProgress[96];
    std::snprintf(byteProgress, sizeof(byteProgress), "%s / %s", uploaded, total);
    draw_metric("Files", fileProgress, 840, 324, 260);
    draw_metric("Bytes", byteProgress, 1130, 324, 340);
    draw_progress(1500, 354, 260, 30, model->uploaded_bytes, model->total_bytes);

    draw_panel(414, 602, 1452, 244, Border);
    draw_text("Receiver Activity", 444, 630, 40, White, 420);
    draw_activity_lines(model, 444, 686, 30, 5, 1320);
}

void draw_results_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, model->last_transfer_success ? Ready : Border);
    draw_text("Last Transfer", 444, 178, 42, White, 620);
    if (!model->last_transfer_has_result)
    {
        draw_text("No completed transfer yet.", 444, 252, 34, Muted, 900);
        draw_text("Run a transfer from UIF, then return here.", 444, 312, 28, Muted, 900);
        return;
    }

    SDL_Color statusColor = model->last_transfer_success ? Ready : model->last_transfer_canceled ? Warn : Danger;
    draw_text(safe(model->last_transfer_status), 444, 250, 46, statusColor, 420);
    draw_text(safe(model->last_transfer_message), 720, 260, 30, White, 1040);
    draw_label_value("Package", safe(model->last_transfer_package_display_name), 444, 354, 580);
    draw_label_value("Kind", safe(model->last_transfer_package_kind), 1080, 354, 260);
    draw_label_value("Action", safe(model->last_transfer_post_transfer_action), 1380, 354, 360);
    draw_label_value("Target", safe(model->last_transfer_target_root), 444, 468, 650);
    draw_label_value("Transfer ID", safe(model->last_transfer_id), 1140, 468, 420);

    char files[96];
    std::snprintf(files, sizeof(files), "%d / %d files", model->last_transfer_uploaded_files, model->last_transfer_file_count);
    char uploaded[32];
    char total[32];
    size_text(model->last_transfer_uploaded_bytes, uploaded, sizeof(uploaded));
    size_text(model->last_transfer_total_bytes, total, sizeof(total));
    char bytes[96];
    std::snprintf(bytes, sizeof(bytes), "%s / %s", uploaded, total);
    draw_metric("Files", files, 444, 596, 300);
    draw_metric("Bytes", bytes, 790, 596, 360);
    if (model->last_transfer_failed_file[0])
        draw_metric("Failed file", safe(model->last_transfer_failed_file), 1196, 596, 520);
}

int list_start(int selected, int count)
{
    return std::clamp(selected - 2, 0, std::max(0, count - 5));
}

bool selected_package_requires_nand()
{
    return selectedPackageIndex >= 0 && selectedPackageIndex < static_cast<int>(cachedInstallPackages.size())
        && (cachedPackagePolicies[cachedInstallPackages[selectedPackageIndex]] == ASSISTANT_POLICY_NAND
            || std::find(systemMemoryOnlyPackages.begin(), systemMemoryOnlyPackages.end(),
                cachedInstallPackages[selectedPackageIndex]) != systemMemoryOnlyPackages.end());
}

void draw_list_actions(bool hasItems)
{
    draw_button(contentFocused ? "Back" : "Open List", 1400, 174, 200,
        contentFocused ? TouchCommand::Back : TouchCommand::Enter, 0, contentFocused, hasItems || contentFocused);
    draw_button("Refresh", 1620, 174, 210, TouchCommand::Refresh);
}

void draw_sd_packages_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 826, contentFocused ? Accent : Border);
    draw_text("Packages on SD", 444, 178, 42, White, 860);
    load_package_snapshot(model);
    draw_list_actions(!cachedInstallPackages.empty());

    char countText[96];
    std::snprintf(countText, sizeof(countText), "%zu package%s", cachedInstallPackages.size(), cachedInstallPackages.size() == 1 ? "" : "s");
    draw_text(countText, 444, 254, 28, Muted, 360);
    draw_text("sd:/install", 870, 254, 28, Muted, 650);
    if (cachedInstallPackages.empty())
    {
        draw_text("No packages on SD.", 444, 350, 34, Muted, 960);
        draw_text(safe(model->status), 444, 900, 26, White, 1340);
        return;
    }

    selectedPackageIndex = std::clamp(selectedPackageIndex, 0, static_cast<int>(cachedInstallPackages.size()) - 1);
    int visible = std::min(5, static_cast<int>(cachedInstallPackages.size()));
    int start = list_start(selectedPackageIndex, static_cast<int>(cachedInstallPackages.size()));
    for (int row = 0; row < visible; ++row)
    {
        int index = start + row;
        bool selected = contentFocused && index == selectedPackageIndex;
        int y = 318 + row * 96;
        const auto &validation = package_validation(model, cachedInstallPackages[index]);
        draw_rect(444, y, 1360, 84, selected ? AccentDark : FieldBackground);
        draw_text(cachedInstallPackages[index].c_str(), 464, y + 8, 28, White, 1310);
        draw_text(validation.c_str(), 464, y + 46, 22,
            validation.rfind("Ready:", 0) == 0 ? Ready : Warn, 1310);
        add_touch_target(444, y, 1360, 84, TouchCommand::Package, index);
    }

    if (contentFocused)
    {
        draw_button("Previous", 444, 822, 200, TouchCommand::Previous);
        draw_button("Next", 660, 822, 170, TouchCommand::Next);
        draw_button("Install...", 1534, 822, 270, TouchCommand::Confirm, 0, true, cachedInstallEnabled);
    }
    if (!model->manual_install_enabled)
        draw_text("Manual installation is disabled.", 870, 842, 26, Warn, 630);
    draw_text(safe(model->status), 444, 916, 25, White, 1350);
}

void draw_installed_page()
{
    draw_panel(414, 150, 1452, 826, contentFocused ? Accent : Border);
    draw_text("Title Locations", 444, 178, 42, White, 860);
    if (!installedTitlesLoaded)
        refresh_installed_titles();
    draw_list_actions(!cachedInstalledTitles.empty());

    int mlc = static_cast<int>(std::count_if(cachedInstalledTitles.begin(), cachedInstalledTitles.end(), [](const InstalledTitleEntry &title) {
        return title.storage == "NAND / MLC";
    }));
    int usb = static_cast<int>(cachedInstalledTitles.size()) - mlc;
    char mlcText[160], usbText[160];
    std::snprintf(mlcText, sizeof(mlcText), "NAND: %s (%d titles)",
        installedTitlesMlcReadable ? "folders readable" : titleCatalogAvailable ? "system catalog" : "browsing restricted", mlc);
    std::snprintf(usbText, sizeof(usbText), "USB: %s (%d titles)",
        installedTitlesUsbReadable ? "folders readable" : titleCatalogAvailable ? "system catalog" : "not readable / disconnected", usb);
    draw_text(mlcText, 444, 254, 26, Muted, 660);
    draw_text(usbText, 1120, 254, 26, Muted, 680);

    if (cachedInstalledTitles.empty())
    {
        draw_text(titleCatalogAvailable ? "No installed user titles reported." : "Title browsing is restricted. Installation may still work.",
            444, 350, 32, Warn, 1320);
        return;
    }

    selectedInstalledTitleIndex = std::clamp(selectedInstalledTitleIndex, 0, static_cast<int>(cachedInstalledTitles.size()) - 1);
    int start = list_start(selectedInstalledTitleIndex, static_cast<int>(cachedInstalledTitles.size()));
    int visible = std::min(5, static_cast<int>(cachedInstalledTitles.size()));
    for (int row = 0; row < visible; ++row)
    {
        int index = start + row;
        auto &title = cachedInstalledTitles[index];
        if (title.name.empty())
            title.name = read_installed_title_name(title.path.c_str());
        bool selected = contentFocused && index == selectedInstalledTitleIndex;
        int y = 318 + row * 96;
        draw_rect(444, y, 1360, 84, selected ? AccentDark : FieldBackground);
        draw_text(title.name.c_str(), 464, y + 8, 27, White, 750);
        draw_text(title.titleId.c_str(), 1230, y + 10, 23, Muted, 300);
        draw_text(title.storage.c_str(), 1560, y + 10, 22, Muted, 210);
        draw_text(title.path.c_str(), 464, y + 48, 22, Muted, 1310);
        add_touch_target(444, y, 1360, 84, TouchCommand::Title, index);
    }
    if (contentFocused)
    {
        draw_button("Previous", 444, 822, 200, TouchCommand::Previous);
        draw_button("Next", 660, 822, 170, TouchCommand::Next);
        draw_button("Launch...", 1534, 822, 270, TouchCommand::Confirm, 0, true);
    }
    draw_text("Title-folder access and installation are separate permissions.", 444, 916, 25, Muted, 1320);
}

void draw_confirmation()
{
    touchTargets.clear();
    draw_panel(650, 310, 1050, 540, Accent);
    draw_text(installConfirmOpen ? "Install package" : "Launch title", 694, 346, 42, White, 950);
    const char *name = installConfirmOpen ? cachedInstallPackages[selectedPackageIndex].c_str()
        : cachedInstalledTitles[selectedInstalledTitleIndex].name.c_str();
    draw_text(name, 694, 420, 32, White, 944);
    if (installConfirmOpen)
    {
        bool nandOnly = selected_package_requires_nand();
        if (nandOnly)
            selectedInstallTarget = 0;
        const auto policy = cachedPackagePolicies[cachedInstallPackages[selectedPackageIndex]];
        draw_text(nandOnly ? "Internal storage required"
            : policy == ASSISTANT_POLICY_UNKNOWN ? "Install to (requirements unverified)"
            : policy == ASSISTANT_POLICY_INVALID ? "Package requirements invalid; resend from UIF."
            : "Install to", 694, 484, 28, Muted, 950);
        draw_button("NAND", 694, 538, 360, TouchCommand::Nand, 0, selectedInstallTarget == 0);
        draw_button("USB", 1074, 538, 360, TouchCommand::Usb, 0, selectedInstallTarget == 1, !nandOnly);
        draw_text("Keep the console on until installation finishes.", 694, 642, 27, Muted, 930);
    }
    draw_button("Cancel", 1054, 738, 260, TouchCommand::Back);
    const bool validPolicy = !installConfirmOpen
        || cachedPackagePolicies[cachedInstallPackages[selectedPackageIndex]] != ASSISTANT_POLICY_INVALID;
    draw_button(installConfirmOpen ? "Install" : "Launch", 1340, 738, 310, TouchCommand::Confirm, 0, true, validPolicy);
}

void draw_auto_install_confirmation()
{
    touchTargets.clear();
    const bool enabling = !cachedAutoInstallEnabled;
    draw_panel(610, 282, 1120, 580, Accent);
    draw_text(enabling ? "Enable automatic installs?" : "Disable automatic installs?", 654, 326, 42, White, 1020);
    draw_text(
        enabling
            ? "A paired UIF device can install validated packages"
            : "Future transfers will stay on the SD card",
        654,
        410,
        29,
        White,
        1010);
    draw_text(
        enabling
            ? "only when you choose Copy and install for that transfer."
            : "until you install them from Packages.",
        654,
        462,
        29,
        White,
        1010);
    draw_text("Copy files only remains UIF's default.", 654, 558, 27, Muted, 990);
    draw_button("Cancel", 1060, 744, 260, TouchCommand::Back);
    draw_button(enabling ? "Enable" : "Disable", 1344, 744, 330, TouchCommand::Confirm, 0, true);
}

void draw_install_progress(const AssistantUiModel *model)
{
    touchTargets.clear();
    draw_panel(500, 304, 1320, 500, Accent);
    draw_text("Installing", 544, 346, 46, White, 1180);
    draw_text(safe(model->status), 544, 446, 30, White, 1230);
    draw_progress(544, 548, 1230, 38, model->install_done_bytes, model->install_total_bytes);
    draw_text("Keep the console on. HOME is available after installation.", 544, 652, 28, Muted, 1230);
}

void draw_diagnostics_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, Border);
    draw_text("Diagnostics", 444, 178, 42, White, 560);

    char http[96];
    std::snprintf(http, sizeof(http), "%u requests, %u errors", model->http_request_count, model->http_error_count);
    char pairStatus[96];
    std::snprintf(pairStatus, sizeof(pairStatus), "%u attempts, %u failures", model->pairing_attempt_count, model->pairing_failure_count);
    char udp[128];
    std::snprintf(udp, sizeof(udp), "%u probes, %u replies, %u broadcasts", model->discovery_probe_count, model->discovery_reply_count, model->discovery_broadcast_count);
    char heartbeat[64];
    std::snprintf(heartbeat, sizeof(heartbeat), "%u heartbeats", model->loop_heartbeat_count);
    draw_metric("HTTP", http, 444, 262, 320);
    draw_metric("Pairing", pairStatus, 800, 262, 320);
    draw_metric("UDP", udp, 1156, 262, 360);
    draw_metric("Loop", heartbeat, 1552, 262, 220);

    draw_text("Last request", 444, 410, 30, Muted, 320);
    draw_text(safe(model->last_http_request), 444, 452, 28, White, 1250);
    draw_text("Last error", 444, 522, 30, Muted, 320);
    draw_text(safe(model->last_http_error), 444, 564, 28, model->last_http_error[0] ? Danger : Dim, 1250);
}

void draw_config_page(const AssistantUiModel *model)
{
    draw_panel(414, 150, 1452, 636, Border);
    draw_text("Config Info", 444, 178, 42, White, 520);
    draw_text("Config", 444, 254, 30, Muted, 260);
    draw_text("sd:/wiiu/apps/uinjectforge-assistant/config.json", 444, 300, 32, White, 1000);
    draw_text("Log", 444, 356, 30, Muted, 200);
    draw_text("sd:/uinjectforge/assistant/assistant.log", 444, 398, 30, White, 900);

    char httpPort[64];
    char broadcastPort[64];
    std::snprintf(httpPort, sizeof(httpPort), "%d", model->http_port);
    std::snprintf(broadcastPort, sizeof(broadcastPort), "%d", model->broadcast_port);
    draw_metric("HTTP", httpPort, 444, 490, 260);
    draw_metric("UDP", broadcastPort, 740, 490, 300);
    draw_metric("UDP discovery", model->broadcast_enabled ? "enabled" : "off", 1076, 490, 300);
    draw_metric("UI", simpleUi ? "receiver" : "dashboard", 1412, 490, 360);

    draw_text(model->manual_install_enabled ? "Manual install: enabled" : "Manual install: disabled", 444, 610, 28, model->manual_install_enabled ? Ready : Muted, 620);
    draw_button(
        model->auto_install_enabled ? "Automatic installs: On" : "Automatic installs: Off",
        1010,
        590,
        520,
        TouchCommand::AutoInstall,
        0,
        model->auto_install_enabled,
        model->install_enabled);
    draw_text(model->install_enabled ? "A or tap to change this setting." : "Install assistance is disabled by config.json.", 444, 682, 26, model->install_enabled ? Muted : Warn, 1040);
    char component[160];
    std::snprintf(component, sizeof(component), "Version %s | Build %s", safe(model->component_version), safe(model->build_id));
    draw_text(component, 444, 734, 25, Dim, 1120);
}

void draw_simple_ui(const AssistantUiModel *model)
{
    SDL_SetRenderDrawColor(renderer, AppBackground.r, AppBackground.g, AppBackground.b, AppBackground.a);
    SDL_RenderClear(renderer);

    SDL_Color stateColor = Ready;
    if (model->transfer_active)
        stateColor = Warn;
    else if (std::strcmp(model->connection_state, "Starting") == 0)
        stateColor = Warn;
    else if (std::strcmp(model->connection_state, "Ready") != 0 && std::strcmp(model->connection_state, "Paired") != 0)
        stateColor = Danger;

    draw_rect(0, 0, DesignWidth, 104, TopBarBackground);
    draw_rect(0, 100, DesignWidth, 4, Accent);
    draw_brand_mark(54, 28);
    draw_text("UInjectForge Assistant", 108, 24, 44, White, 700);
    char component[160];
    std::snprintf(component, sizeof(component), "v%s | %s", safe(model->component_version), safe(model->build_id));
    draw_text(component, 824, 39, 23, Dim, 620);
    draw_status_pill(model->connection_state, stateColor, 1518, 24, 340);

    char endpoint[160];
    std::snprintf(endpoint, sizeof(endpoint), "%s:%d", model->ip_address[0] ? model->ip_address : "waiting", model->http_port);
    char pair[32];
    std::snprintf(pair, sizeof(pair), "%06u", model->pairing_code);

    draw_panel(54, 142, 860, 354, model->paired ? Ready : AccentDark);
    draw_text(model->startup_active ? "Starting Assistant" : model->paired ? "Paired" : "Pairing Code", 92, 182, 42, White, 720);
    draw_text(model->startup_active ? "Please wait" : model->paired ? "Connected to UIF" : pair, 92, 250, model->startup_active ? 64 : model->paired ? 54 : 112, model->paired ? Ready : AccentLight, 720);
    draw_text(model->startup_active ? safe(model->status) : model->paired ? "Ready for transfers from UIF." : "Enter this code in UIF.", 96, 396, 30, Muted, 720);

    draw_panel(952, 142, 914, 354, Border);
    draw_text("Receiver", 990, 182, 42, White, 520);
    draw_compact_metric("Address", endpoint, 990, 254, 520);
    draw_compact_metric("SD", safe(model->sd_root), 1538, 254, 268);
    draw_text("Keep open during transfers.", 990, 396, 28, Muted, 430);
    if (model->startup_active)
        draw_text("Settings available when ready.", 1440, 410, 27, Dim, 366);
    else
        draw_button(
            model->auto_install_enabled ? "Auto install: On" : "Auto install: Off",
            1440,
            384,
            366,
            TouchCommand::AutoInstall,
            0,
            model->auto_install_enabled,
            model->install_enabled);

    draw_panel(54, 530, 1812, 292, model->transfer_active ? Accent : Border);
    draw_text("Transfer", 92, 570, 42, White, 420);
    draw_text(model->transfer_active ? safe(model->package_display_name) : "Waiting for UIF", 92, 630, 36, White, 1150);
    if (!model->startup_active)
        draw_button("Packages & tools", 1410, 566, 396, TouchCommand::Dashboard);
    char files[80];
    std::snprintf(files, sizeof(files), "%d / %d files", model->uploaded_files, model->file_count);
    char uploaded[32];
    char total[32];
    size_text(model->uploaded_bytes, uploaded, sizeof(uploaded));
    size_text(model->total_bytes, total, sizeof(total));
    char bytes[96];
    std::snprintf(bytes, sizeof(bytes), "%s / %s", uploaded, total);
    draw_compact_metric("Files", files, 92, 714, 300);
    draw_compact_metric("Bytes", bytes, 430, 714, 360);
    draw_progress(840, 744, 820, 30, model->uploaded_bytes, model->total_bytes);

    draw_panel(54, 856, 1812, 138, Border);
    draw_text("Latest", 92, 890, 32, Muted, 260);
    const char *latest = model->activity_count > 0 ? model->activity_log[model->activity_count - 1] : safe(model->status);
    draw_text(latest, 250, 890, 30, White, 1260);
    draw_text("HOME", 1660, 890, 30, Muted, 160);

    draw_text("A Refresh", 62, 1022, 27, Dim, 260);
    if (!model->startup_active)
        draw_text("X Auto install", 330, 1022, 27, Dim, 330);
    draw_text("HOME Menu", model->startup_active ? 330 : 690, 1022, 27, Dim, 280);
    if (!model->startup_active)
        draw_text("Y / Wii Remote 2: Packages & tools", 1040, 1022, 27, Dim, 790);
    if (autoInstallConfirmOpen)
        draw_auto_install_confirmation();
    SDL_RenderPresent(renderer);
    refreshRequested = false;
}

}

extern "C" bool assistant_ui_init(void)
{
    if (initialized)
        return true;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return false;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    if (TTF_Init() != 0)
    {
        SDL_Quit();
        return false;
    }

    OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &systemFontData, &systemFontSize);

    window = SDL_CreateWindow(
        "UInjectForge Assistant",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WiiUWindowWidth,
        WiiUWindowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window)
    {
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    SDL_SetRenderTarget(renderer, nullptr);

    initialized = true;
    update_render_metrics(DefaultUiSafeScale);
    return true;
}

extern "C" void assistant_ui_shutdown(void)
{
    if (!initialized)
        return;

    clear_text_cache();
    for (auto &entry : fontCache)
        TTF_CloseFont(entry.second);
    fontCache.clear();
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);

    renderer = nullptr;
    window = nullptr;
    initialized = false;
    TTF_Quit();
    SDL_Quit();
}

extern "C" void assistant_ui_render(const AssistantUiModel *model)
{
    if (!initialized || !renderer || !model)
        return;

    cachedInstallEnabled = model->install_enabled && model->manual_install_enabled;
    cachedInstallGloballyEnabled = model->install_enabled;
    cachedAutoInstallEnabled = model->auto_install_enabled;
    installActive = model->install_active;
    startupActive = model->startup_active;
    simpleUi = model->simple_ui && !dashboardOpen;
    touchTargets.clear();
    if (cachedSdRoot != model->sd_root)
    {
        cachedSdRoot = model->sd_root;
        assistant_ui_invalidate_storage();
    }
    std::string completedTransfer = std::string(model->last_transfer_id) + model->last_transfer_status;
    if (!model->transfer_active && completedTransfer != lastCompletedTransfer)
    {
        lastCompletedTransfer = completedTransfer;
        assistant_ui_invalidate_storage();
    }
    if (!cachedInstallEnabled)
        installConfirmOpen = false;
    if (!cachedInstallGloballyEnabled)
        autoInstallConfirmOpen = false;

    update_render_metrics(model->ui_scale);

    if (simpleUi && !installActive)
    {
        draw_simple_ui(model);
        return;
    }

    SDL_SetRenderDrawColor(renderer, AppBackground.r, AppBackground.g, AppBackground.b, AppBackground.a);
    SDL_RenderClear(renderer);

    draw_rect(0, 0, DesignWidth, 116, TopBarBackground);
    draw_rect(0, 112, DesignWidth, 4, Accent);
    draw_brand_mark(48, 28);
    draw_text("UInjectForge Assistant", 102, 24, 48, White, 760);
    char component[160];
    std::snprintf(component, sizeof(component), "v%s | %s", safe(model->component_version), safe(model->build_id));
    draw_text(component, 102, 78, 23, Dim, 760);
    draw_topbar_connection(model);

    SDL_Color stateColor = Ready;
    if (installActive || std::strcmp(model->connection_state, "Transfer Active") == 0)
        stateColor = Warn;
    else if (std::strcmp(model->connection_state, "Ready") != 0 && std::strcmp(model->connection_state, "Paired") != 0)
        stateColor = Danger;

    draw_rect(1520, 28, 340, 58, stateColor);
    draw_text(installActive ? "Installing" : model->connection_state, 1544, 39, 32, SDL_Color{8, 12, 14, 255}, 292);

    draw_page_sidebar();
    if (installActive)
        draw_install_progress(model);
    else switch (selectedPage)
    {
        case 0:
            draw_overview_page(model);
            break;
        case 1:
            draw_receiver_page(model);
            break;
        case 2:
            draw_pairing_page(model);
            break;
        case 3:
            draw_transfer_page(model);
            break;
        case 4:
            draw_results_page(model);
            break;
        case 5:
            draw_activity_page(model);
            break;
        case 6:
            draw_sd_packages_page(model);
            break;
        case 7:
            draw_log_page(model);
            break;
        case 8:
            draw_installed_page();
            break;
        case 9:
            draw_network_page(model);
            break;
        case 10:
            draw_health_page(model);
            break;
        case 11:
            draw_setup_check_page(model);
            break;
        case 12:
            draw_diagnostics_page(model);
            break;
        case 13:
            draw_controls_page(model);
            break;
        case 14:
            draw_config_page(model);
            break;
        default:
            selectedPage = 0;
            draw_overview_page(model);
            break;
    }

    draw_rect(0, 1004, DesignWidth, 76, TopBarBackground);
    if (installActive)
        draw_text("Installation in progress", 54, 1022, 30, Warn, 1360);
    else
    {
        draw_text(contentFocused ? "Up/Down Items" : "Up/Down Pages", 54, 1022, 30, White, 350);
        draw_text(contentFocused ? "A Choose    B Back" : "A Open", 454, 1022, 30, Muted, 400);
        draw_text("X Refresh", 920, 1022, 30, Muted, 240);
        draw_text("HOME Menu", 1190, 1022, 30, Muted, 260);
        if (dashboardOpen)
            draw_button("Y Receiver", 1530, 1010, 300, TouchCommand::Receiver);
        if (selectedPage != PackagesPageIndex && selectedPage != InstalledTitlesPageIndex)
            draw_button("Refresh", 1620, 912, 210, TouchCommand::Refresh);
        if (installConfirmOpen || launchConfirmOpen)
            draw_confirmation();
        else if (autoInstallConfirmOpen)
            draw_auto_install_confirmation();
    }

    SDL_RenderPresent(renderer);
    refreshRequested = false;
}

void select_page(int page)
{
    selectedPage = std::clamp(page, 0, PageCount - 1);
    contentFocused = false;
    installConfirmOpen = false;
    launchConfirmOpen = false;
    autoInstallConfirmOpen = false;
    if (selectedPage == PackagesPageIndex)
        packagesLoaded = false;
    if (selectedPage == InstalledTitlesPageIndex)
        installedTitlesLoaded = false;
    if (selectedPage == 11)
        setupLoaded = false;
    refreshRequested = true;
}

bool handle_navigation_buttons(uint32_t trigger)
{
    if (!trigger || installActive || startupActive)
        return false;

    if ((trigger & VPAD_BUTTON_B) != 0)
    {
        if (installConfirmOpen || launchConfirmOpen || autoInstallConfirmOpen)
        {
            installConfirmOpen = false;
            launchConfirmOpen = false;
            autoInstallConfirmOpen = false;
        }
        else if (contentFocused)
            contentFocused = false;
        else
            select_page(0);
        refreshRequested = true;
        return true;
    }

    if (autoInstallConfirmOpen)
    {
        if ((trigger & VPAD_BUTTON_A) != 0)
        {
            std::memset(&pendingAction, 0, sizeof(pendingAction));
            pendingAction.type = ASSISTANT_UI_ACTION_SET_AUTO_INSTALL;
            pendingAction.enabled = !cachedAutoInstallEnabled;
            autoInstallConfirmOpen = false;
        }
        refreshRequested = true;
        return true;
    }

    if (simpleUi)
    {
        if ((trigger & VPAD_BUTTON_Y) != 0)
        {
            dashboardOpen = true;
            simpleUi = false;
            select_page(PackagesPageIndex);
            return true;
        }
        if ((trigger & VPAD_BUTTON_X) != 0 && cachedInstallGloballyEnabled)
        {
            autoInstallConfirmOpen = true;
            refreshRequested = true;
            return true;
        }
        refreshRequested = (trigger & VPAD_BUTTON_A) != 0;
        storageRefreshRequested = storageRefreshRequested || refreshRequested;
        return refreshRequested;
    }

    if (installConfirmOpen || launchConfirmOpen)
    {
        if (installConfirmOpen && (trigger & VPAD_BUTTON_A) != 0
            && cachedPackagePolicies[cachedInstallPackages[selectedPackageIndex]] == ASSISTANT_POLICY_INVALID)
            return true;
        if (installConfirmOpen && (trigger & (VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)) != 0)
            selectedInstallTarget = selected_package_requires_nand() ? 0 : 1 - selectedInstallTarget;
        else if ((trigger & VPAD_BUTTON_A) != 0)
        {
            std::memset(&pendingAction, 0, sizeof(pendingAction));
            if (installConfirmOpen && cachedInstallEnabled && !cachedInstallPackages.empty())
            {
                pendingAction.type = !selected_package_requires_nand() && selectedInstallTarget == 1
                    ? ASSISTANT_UI_ACTION_INSTALL_TO_USB : ASSISTANT_UI_ACTION_INSTALL_TO_NAND;
                std::snprintf(pendingAction.package_name, sizeof(pendingAction.package_name), "%s",
                    cachedInstallPackages[selectedPackageIndex].c_str());
            }
            else if (launchConfirmOpen && !cachedInstalledTitles.empty())
            {
                const auto &title = cachedInstalledTitles[selectedInstalledTitleIndex];
                pendingAction.type = ASSISTANT_UI_ACTION_LAUNCH_TITLE;
                std::snprintf(pendingAction.title_id, sizeof(pendingAction.title_id), "%s", title.titleId.c_str());
                std::snprintf(pendingAction.title_name, sizeof(pendingAction.title_name), "%s", title.name.c_str());
            }
            installConfirmOpen = false;
            launchConfirmOpen = false;
        }
        refreshRequested = true;
        return true;
    }

    if ((trigger & VPAD_BUTTON_Y) != 0 && dashboardOpen)
    {
        dashboardOpen = false;
        simpleUi = true;
        select_page(0);
        return true;
    }

    if ((trigger & VPAD_BUTTON_X) != 0)
    {
        storageRefreshRequested = true;
        assistant_ui_invalidate_storage();
        return true;
    }

    if (!contentFocused)
    {
        if ((trigger & (VPAD_BUTTON_UP | VPAD_BUTTON_LEFT)) != 0)
            select_page((selectedPage + PageCount - 1) % PageCount);
        else if ((trigger & (VPAD_BUTTON_DOWN | VPAD_BUTTON_RIGHT)) != 0)
            select_page((selectedPage + 1) % PageCount);
        else if ((trigger & VPAD_BUTTON_A) != 0)
        {
            if (selectedPage == ConfigPageIndex && cachedInstallGloballyEnabled)
            {
                autoInstallConfirmOpen = true;
                refreshRequested = true;
                return true;
            }
            contentFocused = (selectedPage == PackagesPageIndex && !cachedInstallPackages.empty())
                || (selectedPage == InstalledTitlesPageIndex && !cachedInstalledTitles.empty());
            if (!contentFocused)
            {
                storageRefreshRequested = true;
                assistant_ui_invalidate_storage();
            }
            refreshRequested = true;
        }
        return true;
    }

    int count = selectedPage == PackagesPageIndex
        ? static_cast<int>(cachedInstallPackages.size()) : static_cast<int>(cachedInstalledTitles.size());
    int &index = selectedPage == PackagesPageIndex ? selectedPackageIndex : selectedInstalledTitleIndex;
    if ((trigger & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN)) != 0 && count > 0)
        index = (index + ((trigger & VPAD_BUTTON_UP) != 0 ? count - 1 : 1)) % count;
    else if ((trigger & VPAD_BUTTON_A) != 0 && count > 0)
    {
        installConfirmOpen = selectedPage == PackagesPageIndex && cachedInstallEnabled;
        launchConfirmOpen = selectedPage == InstalledTitlesPageIndex;
        if (installConfirmOpen && selected_package_requires_nand())
            selectedInstallTarget = 0;
    }
    else if ((trigger & VPAD_BUTTON_LEFT) != 0)
        contentFocused = false;
    refreshRequested = true;
    return true;
}

void handle_touch_command(const TouchTarget &target)
{
    if (installActive || startupActive)
        return;
    switch (target.command)
    {
        case TouchCommand::Dashboard:
        case TouchCommand::Receiver:
            handle_navigation_buttons(VPAD_BUTTON_Y);
            break;
        case TouchCommand::Page:
            select_page(target.value);
            break;
        case TouchCommand::SidebarUp:
            select_page(std::max(0, sidebarStart - SidebarVisibleCount));
            break;
        case TouchCommand::SidebarDown:
            select_page(std::min(PageCount - 1, sidebarStart + SidebarVisibleCount));
            break;
        case TouchCommand::Package:
            if (target.value >= 0 && target.value < static_cast<int>(cachedInstallPackages.size()))
            {
                selectedPackageIndex = target.value;
                contentFocused = true;
            }
            break;
        case TouchCommand::Title:
            if (target.value >= 0 && target.value < static_cast<int>(cachedInstalledTitles.size()))
            {
                selectedInstalledTitleIndex = target.value;
                contentFocused = true;
            }
            break;
        case TouchCommand::Enter:
        case TouchCommand::Confirm:
            handle_navigation_buttons(VPAD_BUTTON_A);
            break;
        case TouchCommand::Back:
            handle_navigation_buttons(VPAD_BUTTON_B);
            break;
        case TouchCommand::Refresh:
            storageRefreshRequested = true;
            assistant_ui_invalidate_storage();
            break;
        case TouchCommand::Previous:
            handle_navigation_buttons(VPAD_BUTTON_UP);
            break;
        case TouchCommand::Next:
            handle_navigation_buttons(VPAD_BUTTON_DOWN);
            break;
        case TouchCommand::Nand:
            selectedInstallTarget = 0;
            break;
        case TouchCommand::Usb:
            if (!selected_package_requires_nand())
                selectedInstallTarget = 1;
            break;
        case TouchCommand::AutoInstall:
            if (cachedInstallGloballyEnabled)
                autoInstallConfirmOpen = true;
            break;
    }
    refreshRequested = true;
}

const TouchTarget *touch_target_at(float gamepadX, float gamepadY)
{
    if (gamepadX < 0 || gamepadX >= 854 || gamepadY < 0 || gamepadY >= 480 || renderScale <= 0)
        return nullptr;
    // The GamePad displays the renderer output scaled to 854x480. Invert the
    // same fit/letterbox transform used by drawing before testing hit regions.
    float x = (gamepadX * outputWidth / 854.0f - renderOriginX) / renderScale;
    float y = (gamepadY * outputHeight / 480.0f - renderOriginY) / renderScale;
    for (auto it = touchTargets.rbegin(); it != touchTargets.rend(); ++it)
        if (x >= it->rect.x && x < it->rect.x + it->rect.w
            && y >= it->rect.y && y < it->rect.y + it->rect.h)
            return &*it;
    return nullptr;
}

void handle_touch_sample(bool down, bool valid, float x, float y)
{
    if (installActive)
    {
        touchHeld = down;
        touchArmed = false;
        return;
    }
    if (down)
    {
        const TouchTarget *target = valid ? touch_target_at(x, y) : nullptr;
        if (!touchHeld)
        {
            touchArmed = target != nullptr;
            if (target)
                pressedTouch = *target;
        }
        else if (!target || target->command != pressedTouch.command || target->value != pressedTouch.value)
            touchArmed = false;
        touchHeld = true;
        return;
    }
    bool activate = touchHeld && touchArmed;
    touchHeld = false;
    touchArmed = false;
    if (activate)
        handle_touch_command(pressedTouch);
}

extern "C" bool assistant_ui_poll_input(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_WINDOWEVENT)
            refreshRequested = true;
        if (event.type == SDL_RENDER_TARGETS_RESET || event.type == SDL_RENDER_DEVICE_RESET)
        {
            clear_text_cache();
            refreshRequested = true;
        }
    }

    VPADStatus vpad;
    VPADReadError vpadError;
    std::memset(&vpad, 0, sizeof(vpad));
    if (VPADRead(VPAD_CHAN_0, &vpad, 1, &vpadError) > 0 && vpadError == VPAD_READ_SUCCESS)
    {
        VPADTouchData calibrated{};
        VPADGetTPCalibratedPointEx(VPAD_CHAN_0, VPAD_TP_854X480, &calibrated, &vpad.tpNormal);
        handle_touch_sample(vpad.tpNormal.touched != 0, calibrated.validity == VPAD_VALID, calibrated.x, calibrated.y);
        if (handle_navigation_buttons(vpad.trigger))
            return true;
    }

    for (int channel = WPAD_CHAN_0; channel <= WPAD_CHAN_3; channel++)
    {
        KPADStatus kpad;
        KPADError kpadError;
        std::memset(&kpad, 0, sizeof(kpad));
        if (KPADReadEx(static_cast<KPADChan>(channel), &kpad, 1, &kpadError) > 0 && kpadError == KPAD_ERROR_OK)
        {
            uint32_t normalized = 0;
            if ((kpad.trigger & WPAD_BUTTON_A) != 0)
                normalized |= VPAD_BUTTON_A;
            if ((kpad.trigger & WPAD_BUTTON_B) != 0)
                normalized |= VPAD_BUTTON_B;
            if ((kpad.trigger & WPAD_BUTTON_UP) != 0)
                normalized |= VPAD_BUTTON_UP;
            if ((kpad.trigger & WPAD_BUTTON_DOWN) != 0)
                normalized |= VPAD_BUTTON_DOWN;
            if ((kpad.trigger & WPAD_BUTTON_LEFT) != 0)
                normalized |= VPAD_BUTTON_LEFT;
            if ((kpad.trigger & WPAD_BUTTON_RIGHT) != 0)
                normalized |= VPAD_BUTTON_RIGHT;
            if ((kpad.trigger & WPAD_BUTTON_1) != 0)
                normalized |= VPAD_BUTTON_X;
            if ((kpad.trigger & WPAD_BUTTON_2) != 0)
                normalized |= VPAD_BUTTON_Y;

            if (kpad.extensionType == WPAD_EXT_CLASSIC || kpad.extensionType == WPAD_EXT_MPLUS_CLASSIC)
            {
                normalized = 0;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_A) != 0)
                    normalized |= VPAD_BUTTON_A;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_B) != 0)
                    normalized |= VPAD_BUTTON_B;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_UP) != 0)
                    normalized |= VPAD_BUTTON_UP;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_DOWN) != 0)
                    normalized |= VPAD_BUTTON_DOWN;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_LEFT) != 0)
                    normalized |= VPAD_BUTTON_LEFT;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_RIGHT) != 0)
                    normalized |= VPAD_BUTTON_RIGHT;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_X) != 0)
                    normalized |= VPAD_BUTTON_X;
                if ((kpad.classic.trigger & WPAD_CLASSIC_BUTTON_Y) != 0)
                    normalized |= VPAD_BUTTON_Y;
            }

            if (kpad.extensionType == WPAD_EXT_PRO_CONTROLLER)
            {
                normalized = 0;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_A) != 0)
                    normalized |= VPAD_BUTTON_A;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_B) != 0)
                    normalized |= VPAD_BUTTON_B;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_UP) != 0)
                    normalized |= VPAD_BUTTON_UP;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_DOWN) != 0)
                    normalized |= VPAD_BUTTON_DOWN;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_LEFT) != 0)
                    normalized |= VPAD_BUTTON_LEFT;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_RIGHT) != 0)
                    normalized |= VPAD_BUTTON_RIGHT;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_X) != 0)
                    normalized |= VPAD_BUTTON_X;
                if ((kpad.pro.trigger & WPAD_PRO_BUTTON_Y) != 0)
                    normalized |= VPAD_BUTTON_Y;
            }

            if (handle_navigation_buttons(normalized))
                return true;
        }
    }

    return false;
}

extern "C" bool assistant_ui_take_refresh_request(void)
{
    bool result = refreshRequested;
    refreshRequested = false;
    return result;
}

extern "C" bool assistant_ui_take_storage_refresh_request(void)
{
    bool result = storageRefreshRequested;
    storageRefreshRequested = false;
    return result;
}

extern "C" bool assistant_ui_take_action(AssistantUiAction *action)
{
    if (!action || pendingAction.type == ASSISTANT_UI_ACTION_NONE)
        return false;

    *action = pendingAction;
    std::memset(&pendingAction, 0, sizeof(pendingAction));
    return true;
}

extern "C" void assistant_ui_require_system_memory(const char *package_name)
{
    if (!package_name || !package_name[0])
        return;

    const std::string name(package_name);
    if (std::find(systemMemoryOnlyPackages.begin(), systemMemoryOnlyPackages.end(), name) == systemMemoryOnlyPackages.end())
        systemMemoryOnlyPackages.push_back(name);

    selectedInstallTarget = 0;
    refreshRequested = true;
}

extern "C" void assistant_ui_invalidate_storage(void)
{
    packagesLoaded = false;
    setupLoaded = false;
    installedTitlesLoaded = false;
    installConfirmOpen = false;
    launchConfirmOpen = false;
    autoInstallConfirmOpen = false;
    touchArmed = false;
    refreshRequested = true;
}

extern "C" void assistant_ui_set_install_active(bool active)
{
    installActive = active;
    installConfirmOpen = false;
    launchConfirmOpen = false;
    touchArmed = false;
    std::memset(&pendingAction, 0, sizeof(pendingAction));
    if (!active)
        assistant_ui_invalidate_storage();
    refreshRequested = true;
}

extern "C" bool assistant_ui_needs_log_tail(void)
{
    return !simpleUi && !installActive && selectedPage == 7;
}
