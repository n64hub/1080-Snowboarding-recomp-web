// RetroAchievements support (see include/game_achievements.h for the rules it
// follows). This file holds the rcheevos client, its HTTP transport, the files
// it keeps in the app folder and the unlock pop-up.
//
// Files in the app folder:
//   achievements_login.json   {"username": ..., "token": ...}
//   achievements_local.json   {"<game id>": [achievement ids earned here]}

#include "game_achievements.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "json/json.hpp"

#include "rc_api_user.h"
#include "rc_client.h"
#include "rc_consoles.h"
#include "rhash/md5.h"

#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"
#include "recompui/recompui.h"
#include "util/file.h"
#include "elements/ui_label.h"

#include "game_version.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

extern std::vector<recomp::GameEntry> supported_games;

namespace {
using json = nlohmann::json;
using clock_type = std::chrono::steady_clock;

constexpr uint32_t rdram_size = 0x800000;

std::atomic<uint8_t*> rdram_base{ nullptr };
rc_client_t* client = nullptr;

// Everything below is guarded by state_mutex.
std::mutex state_mutex;
game::achievements::SignInState sign_in_state = game::achievements::SignInState::SignedOut;
std::string username;
std::string message;
bool load_requested = false;     // a load of the game's set was started (or done)
clock_type::time_point retry_load_at{};
uint32_t loaded_game_id = 0;
std::set<uint32_t> local_unlocks;
std::set<uint32_t> site_unlocks;     // already unlocked on retroachievements.org
uint32_t state_version = 1;

void bump_version() {
    state_version++;
}

std::filesystem::path app_file(const char* name) {
    return recompui::file::get_app_folder_path() / name;
}

json read_json(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return json::object();
    }
    json data = json::parse(file, nullptr, false);
    return data.is_object() ? data : json::object();
}

void write_json(const std::filesystem::path& path, const json& data) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::trunc);
        if (!file) {
            return;
        }
        file << data.dump(2);
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
}

void save_login(const std::string& user, const std::string& token) {
    write_json(app_file("achievements_login.json"), json{ { "username", user }, { "token", token } });
}

void forget_login() {
    std::error_code ec;
    std::filesystem::remove(app_file("achievements_login.json"), ec);
}

// Caller holds state_mutex.
void load_local_unlocks(uint32_t game_id) {
    local_unlocks.clear();
    json data = read_json(app_file("achievements_local.json"));
    auto it = data.find(std::to_string(game_id));
    if (it != data.end() && it->is_array()) {
        for (const auto& id : *it) {
            if (id.is_number_unsigned()) {
                local_unlocks.insert(id.get<uint32_t>());
            }
        }
    }
}

// Caller holds state_mutex.
void save_local_unlocks(uint32_t game_id) {
    std::filesystem::path path = app_file("achievements_local.json");
    json data = read_json(path);
    data[std::to_string(game_id)] = std::vector<uint32_t>(local_unlocks.begin(), local_unlocks.end());
    write_json(path, data);
}

std::string user_agent() {
    char clause[64] = {};
    if (client != nullptr) {
        rc_client_get_user_agent_clause(client, clause, sizeof(clause));
    }
#ifdef _WIN32
    const char* os = "Windows";
#else
    const char* os = "Linux";
#endif
    return std::string("1080SnowboardingRecompiled/") + game_version + " (" + os + ") " + clause;
}

// ---------------------------------------------------------------------------
// HTTP. rcheevos hands us a URL and optional form data; each request runs on
// its own thread and reports back through rcheevos's callback.

struct HttpResult {
    int status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    std::string body;
};

#ifdef _WIN32
std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), count);
    return out;
}

HttpResult http_request(const std::string& url, const char* post_data, const char* content_type) {
    HttpResult result;
    std::wstring wurl = widen(url);

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    wchar_t extra[2048] = {};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        result.status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        return result;
    }
    std::wstring object = std::wstring(path) + extra;

    HINTERNET session = WinHttpOpen(widen(user_agent()).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        session = WinHttpOpen(widen(user_agent()).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == nullptr) {
        return result;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 30000, 30000);
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = nullptr;
    if (connection != nullptr) {
        DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connection, post_data != nullptr ? L"POST" : L"GET", object.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    }
    if (request != nullptr) {
        std::wstring headers;
        if (post_data != nullptr) {
            headers = L"Content-Type: " + widen(content_type != nullptr ? content_type : "application/x-www-form-urlencoded");
        }
        DWORD post_length = post_data != nullptr ? (DWORD)strlen(post_data) : 0;
        BOOL sent = WinHttpSendRequest(request,
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), (DWORD)-1L,
            (LPVOID)post_data, post_length, post_length, 0);
        if (sent && WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0;
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                    break;
                }
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                    break;
                }
                result.body.append(chunk.data(), read);
            }
            result.status = (int)status;
        }
        WinHttpCloseHandle(request);
    }
    if (connection != nullptr) {
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return result;
}
#elif defined(__EMSCRIPTEN__)
// A page has neither WinHTTP nor curl, and achievements are off there (init() below): every request fails.
HttpResult http_request(const std::string&, const char*, const char*) {
    return {};
}
#else
// Linux builds are for development; they use the curl command.
std::string shell_quote(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') {
            out += "'\\''";
        }
        else {
            out += c;
        }
    }
    return out + "'";
}

HttpResult http_request(const std::string& url, const char* post_data, const char* content_type) {
    static std::atomic<uint32_t> counter{ 0 };
    HttpResult result;
    std::filesystem::path body_file = std::filesystem::temp_directory_path() /
        ("1080ra-" + std::to_string(counter++) + ".txt");
    std::string command = "curl -sS -m 30 -A " + shell_quote(user_agent());
    if (post_data != nullptr) {
        {
            std::ofstream out(body_file, std::ios::binary | std::ios::trunc);
            out << post_data;
        }
        command += " -H " + shell_quote(std::string("Content-Type: ") +
            (content_type != nullptr ? content_type : "application/x-www-form-urlencoded"));
        command += " --data-binary @" + shell_quote(body_file.string());
    }
    command += " -w '\\n%{http_code}' " + shell_quote(url);
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe != nullptr) {
        char buffer[4096];
        size_t count;
        while ((count = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
            result.body.append(buffer, count);
        }
        pclose(pipe);
        size_t newline = result.body.rfind('\n');
        if (newline != std::string::npos) {
            int status = atoi(result.body.c_str() + newline + 1);
            result.body.resize(newline);
            if (status > 0) {
                result.status = status;
            }
        }
    }
    std::error_code ec;
    std::filesystem::remove(body_file, ec);
    return result;
}
#endif

void RC_CCONV server_call(const rc_api_request_t* request, rc_client_server_callback_t callback,
                          void* callback_data, rc_client_t*) {
    std::string url = request->url;
    bool has_post = request->post_data != nullptr;
    std::string post = has_post ? request->post_data : "";
    std::string content_type = request->content_type != nullptr ? request->content_type : "";
    std::thread([url, has_post, post, content_type, callback, callback_data]() {
        HttpResult result = http_request(url, has_post ? post.c_str() : nullptr,
            content_type.empty() ? nullptr : content_type.c_str());
        rc_api_server_response_t response{};
        response.body = result.body.c_str();
        response.body_length = result.body.size();
        response.http_status_code = result.status;
        callback(&response, callback_data);
    }).detach();
}

// RetroAchievements reads N64 RDRAM the way mupen64plus stores it: as
// host-endian 32-bit words, which is exactly how the runtime stores it too.
uint32_t RC_CCONV read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t*) {
    if (address >= rdram_size) {
        return 0;
    }
    num_bytes = std::min(num_bytes, rdram_size - address);
    uint8_t* rdram = rdram_base.load();
    if (rdram == nullptr) {
        memset(buffer, 0, num_bytes);
    }
    else {
        memcpy(buffer, rdram + address, num_bytes);
    }
    return num_bytes;
}

void RC_CCONV log_message(const char* text, const rc_client_t*) {
    fprintf(stderr, "[achievements] %s\n", text);
}

// ---------------------------------------------------------------------------
// Unlock pop-up: a small panel at the top of the screen that does not take
// input, shown for a few seconds per unlock.

struct Toast {
    std::string header;
    std::string title;
    std::string text;
};

std::mutex toast_mutex;
std::deque<Toast> toast_queue;
bool toast_ready = false;
bool toast_visible = false;
clock_type::time_point toast_hide_at{};
recompui::ContextId toast_context = recompui::ContextId::null();
recompui::Label* toast_header = nullptr;
recompui::Label* toast_title = nullptr;
recompui::Label* toast_text = nullptr;

void queue_toast(std::string header, std::string title, std::string text) {
    std::lock_guard lock{ toast_mutex };
    toast_queue.push_back(Toast{ std::move(header), std::move(title), std::move(text) });
}

void update_toasts() {
    std::lock_guard lock{ toast_mutex };
    if (!toast_ready) {
        return;
    }
    auto now = clock_type::now();
    if (toast_visible && now >= toast_hide_at) {
        if (recompui::is_context_shown(toast_context)) {
            recompui::hide_context(toast_context);
        }
        toast_visible = false;
    }
    if (!toast_visible && !toast_queue.empty()) {
        Toast toast = std::move(toast_queue.front());
        toast_queue.pop_front();
        // The caller may have another context open (the launcher does).
        recompui::ContextId previous = recompui::try_close_current_context();
        toast_context.open();
        toast_header->set_text(toast.header);
        toast_title->set_text(toast.title);
        toast_text->set_text(toast.text);
        toast_context.close();
        if (previous != recompui::ContextId::null()) {
            previous.open();
        }
        if (!recompui::is_context_shown(toast_context)) {
            recompui::show_context(toast_context, "");
        }
        toast_visible = true;
        toast_hide_at = now + std::chrono::seconds(5);
    }
}

// ---------------------------------------------------------------------------
// rcheevos callbacks.

void RC_CCONV event_handler(const rc_client_event_t* event, rc_client_t*) {
    if (event->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && event->achievement != nullptr) {
        const rc_client_achievement_t* achievement = event->achievement;
        bool is_new = false;
        {
            std::lock_guard lock{ state_mutex };
            if (site_unlocks.count(achievement->id) != 0) {
                return;
            }
            is_new = local_unlocks.insert(achievement->id).second;
            if (is_new) {
                save_local_unlocks(loaded_game_id);
                bump_version();
            }
        }
        if (is_new) {
            queue_toast("Achievement Unlocked",
                std::string(achievement->title) + "  \xC2\xB7  " + std::to_string(achievement->points) + " points",
                achievement->description);
        }
    }
    else if (event->type == RC_CLIENT_EVENT_GAME_COMPLETED) {
        queue_toast("Set Complete", "1080\xC2\xB0 Snowboarding", "Every achievement for this game is unlocked.");
    }
    else if (event->type == RC_CLIENT_EVENT_SERVER_ERROR && event->server_error != nullptr) {
        std::lock_guard lock{ state_mutex };
        message = event->server_error->error_message != nullptr ? event->server_error->error_message : "Server error";
        bump_version();
    }
}

void RC_CCONV unlocks_callback(const rc_api_server_response_t* server_response, void*) {
    rc_api_fetch_user_unlocks_response_t response{};
    if (rc_api_process_fetch_user_unlocks_server_response(&response, server_response) == RC_OK && response.response.succeeded) {
        std::lock_guard lock{ state_mutex };
        site_unlocks.insert(response.achievement_ids, response.achievement_ids + response.num_achievement_ids);
        bump_version();
    }
    rc_api_destroy_fetch_user_unlocks_response(&response);
}

// In spectator mode rcheevos does not start a session, so it never learns what
// the player already unlocked on the site. Ask for that list (read only).
void fetch_site_unlocks(rc_client_t* c, uint32_t game_id) {
    const rc_client_user_t* user = rc_client_get_user_info(c);
    if (user == nullptr || game_id == 0) {
        return;
    }
    rc_api_fetch_user_unlocks_request_t params{};
    params.username = user->username;
    params.api_token = user->token;
    params.game_id = game_id;
    params.hardcore = 0;
    rc_api_request_t request{};
    if (rc_api_init_fetch_user_unlocks_request(&request, &params) == RC_OK) {
        server_call(&request, unlocks_callback, nullptr, c);
    }
    rc_api_destroy_request(&request);
}

void RC_CCONV load_game_callback(int result, const char* error_message, rc_client_t* c, void*) {
    uint32_t fetch_game_id = 0;
    {
        std::lock_guard lock{ state_mutex };
        if (result == RC_OK) {
            const rc_client_game_t* game = rc_client_get_game_info(c);
            loaded_game_id = game != nullptr ? game->id : 0;
            load_local_unlocks(loaded_game_id);
            site_unlocks.clear();
            fetch_game_id = loaded_game_id;
            message.clear();
        }
        else if (result == RC_NO_GAME_LOADED) {
            message = "RetroAchievements has no set for this version of the game.";
        }
        else {
            message = error_message != nullptr ? error_message : "Could not load the achievement set.";
            load_requested = false;   // try again in a little while
            retry_load_at = clock_type::now() + std::chrono::seconds(30);
        }
        bump_version();
    }
    if (fetch_game_id != 0) {
        fetch_site_unlocks(c, fetch_game_id);
    }
}

void RC_CCONV login_callback(int result, const char* error_message, rc_client_t* c, void*) {
    std::lock_guard lock{ state_mutex };
    if (result == RC_OK) {
        const rc_client_user_t* user = rc_client_get_user_info(c);
        if (user != nullptr) {
            username = user->display_name != nullptr && user->display_name[0] != '\0' ? user->display_name : user->username;
            save_login(user->username, user->token);
        }
        sign_in_state = game::achievements::SignInState::SignedIn;
        message.clear();
    }
    else {
        sign_in_state = game::achievements::SignInState::SignedOut;
        message = error_message != nullptr ? error_message : "Sign in failed.";
        if (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN || result == RC_ACCESS_DENIED) {
            forget_login();
        }
    }
    bump_version();
}

bool md5_of_rom(std::string& out) {
    std::filesystem::path rom = recompui::file::get_app_folder_path() / supported_games[0].stored_filename();
    std::ifstream file(rom, std::ios::binary);
    if (!file) {
        return false;
    }
    md5_state_t md5;
    md5_init(&md5);
    std::vector<char> buffer(1 << 20);
    while (file) {
        file.read(buffer.data(), buffer.size());
        std::streamsize count = file.gcount();
        if (count > 0) {
            md5_append(&md5, reinterpret_cast<const md5_byte_t*>(buffer.data()), (int)count);
        }
    }
    md5_byte_t digest[16];
    md5_finish(&md5, digest);
    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    out = hex;
    return true;
}
}

void game::achievements::init() {
#ifdef __EMSCRIPTEN__
    // Off on a page, which has no HTTP transport for rcheevos (http_request above): without a client every call here
    // does nothing, and the launcher leaves out its Achievements page.
    return;
#endif
    client = rc_client_create(read_memory, server_call);
    if (client == nullptr) {
        return;
    }
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_WARN, log_message);
    rc_client_set_event_handler(client, event_handler);
    rc_client_set_hardcore_enabled(client, 0);
    // Unlocks are not sent to RetroAchievements until it approves this program.
    rc_client_set_spectator_mode_enabled(client, 1);
    // Development aid: point at a test server instead of retroachievements.org.
    if (const char* host = std::getenv("GAME_RA_HOST")) {
        rc_client_set_host(client, host);
    }

    json login = read_json(app_file("achievements_login.json"));
    if (login.contains("username") && login.contains("token") && login["username"].is_string() && login["token"].is_string()) {
        std::string user = login["username"].get<std::string>();
        std::string token = login["token"].get<std::string>();
        {
            std::lock_guard lock{ state_mutex };
            sign_in_state = SignInState::SigningIn;
            username = user;
            bump_version();
        }
        rc_client_begin_login_with_token(client, user.c_str(), token.c_str(), login_callback, nullptr);
    }
}

void game::achievements::launcher_tick() {
    if (client == nullptr) {
        return;
    }
    update_toasts();
    {
        std::lock_guard lock{ state_mutex };
        if (sign_in_state != SignInState::SignedIn || load_requested || clock_type::now() < retry_load_at) {
            return;
        }
        if (!recomp::is_rom_valid(supported_games[0].game_id)) {
            return;
        }
        load_requested = true;
    }
    std::string hash;
    if (!md5_of_rom(hash)) {
        std::lock_guard lock{ state_mutex };
        load_requested = false;
        return;
    }
    rc_client_begin_load_game(client, hash.c_str(), load_game_callback, nullptr);
}

void game::achievements::frame() {
    if (client == nullptr) {
        return;
    }
    // The VI clock also ticks in the launcher, before the game has set up its memory.
    if (rc_client_is_game_loaded(client) && ultramodern::is_game_started()) {
        rc_client_do_frame(client);
    }
    else {
        rc_client_idle(client);
    }
    update_toasts();
}

void game::achievements::set_rdram(uint8_t* rdram) {
    rdram_base.store(rdram);
}

void game::achievements::sign_in(const std::string& user, const std::string& password) {
    if (client == nullptr || user.empty() || password.empty()) {
        return;
    }
    {
        std::lock_guard lock{ state_mutex };
        if (sign_in_state != SignInState::SignedOut) {
            return;
        }
        sign_in_state = SignInState::SigningIn;
        username = user;
        message.clear();
        bump_version();
    }
    rc_client_begin_login_with_password(client, user.c_str(), password.c_str(), login_callback, nullptr);
}

void game::achievements::sign_out() {
    if (client == nullptr) {
        return;
    }
    rc_client_unload_game(client);
    rc_client_logout(client);
    forget_login();
    std::lock_guard lock{ state_mutex };
    sign_in_state = SignInState::SignedOut;
    username.clear();
    message.clear();
    load_requested = false;
    loaded_game_id = 0;
    local_unlocks.clear();
    site_unlocks.clear();
    bump_version();
}

game::achievements::Snapshot game::achievements::snapshot() {
    Snapshot snap;
    std::set<uint32_t> earned_here;
    std::set<uint32_t> earned_on_site;
    {
        std::lock_guard lock{ state_mutex };
        snap.version = state_version;
        snap.state = sign_in_state;
        snap.username = username;
        snap.message = message;
        earned_here = local_unlocks;
        earned_on_site = site_unlocks;
    }
    // rcheevos is queried without state_mutex held, since its callbacks take it.
    if (client == nullptr || snap.state != SignInState::SignedIn || !rc_client_is_game_loaded(client)) {
        return snap;
    }
    const rc_client_game_t* game = rc_client_get_game_info(client);
    snap.game_title = game != nullptr && game->title != nullptr ? game->title : "";
    rc_client_achievement_list_t* list = rc_client_create_achievement_list(client,
        RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if (list != nullptr) {
        for (uint32_t b = 0; b < list->num_buckets; b++) {
            const auto& bucket = list->buckets[b];
            for (uint32_t i = 0; i < bucket.num_achievements; i++) {
                const rc_client_achievement_t* a = bucket.achievements[i];
                // RetroAchievements adds a 0-point "Warning: Unknown Emulator" entry for programs
                // it hasn't approved yet. It isn't part of the game's set, so leave it out.
                if (a->id == 101000001 || (a->title != nullptr && strncmp(a->title, "Warning: Unknown Emulator", 25) == 0)) {
                    continue;
                }
                Entry entry;
                entry.id = a->id;
                entry.title = a->title != nullptr ? a->title : "";
                entry.description = a->description != nullptr ? a->description : "";
                entry.points = a->points;
                bool on_site = earned_on_site.count(a->id) != 0;
                entry.unlocked_here = !on_site && earned_here.count(a->id) != 0;
                entry.unlocked = on_site || entry.unlocked_here || a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
                snap.entries.push_back(std::move(entry));
            }
        }
        rc_client_destroy_achievement_list(list);
    }
    // Unlocked first, then in the set's own order.
    std::stable_sort(snap.entries.begin(), snap.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.unlocked != b.unlocked) {
            return a.unlocked;
        }
        return a.id < b.id;
    });
    snap.game_has_set = !snap.entries.empty();
    return snap;
}

void game::achievements::init_toasts() {
    using namespace recompui;
    std::lock_guard lock{ toast_mutex };
    if (toast_ready) {
        return;
    }
    ContextId previous = try_close_current_context();
    toast_context = create_context();
    toast_context.set_captures_input(false);
    toast_context.set_captures_mouse(false);
    toast_context.open();
    Element* root = toast_context.get_root_element();
    Element* panel = toast_context.create_element<Element>(root, 0, "div", false);
    panel->set_position(Position::Absolute);
    panel->set_top(32.0f);
    panel->set_left(50.0f, Unit::Percent);
    panel->set_width(640.0f);
    panel->set_margin_left(-320.0f);
    panel->set_padding(20.0f);
    panel->set_background_color(Color{ 12, 22, 38, 230 });
    panel->set_border_width(2.0f);
    panel->set_border_color(Color{ 240, 200, 80, 220 });
    panel->set_border_radius(12.0f);
    panel->set_display(Display::Flex);
    panel->set_flex_direction(FlexDirection::Column);
    toast_header = toast_context.create_element<Label>(panel, "", theme::Typography::LabelSM);
    toast_header->set_color(Color{ 240, 200, 80, 255 });
    toast_header->set_text_transform(TextTransform::Uppercase);
    toast_header->set_letter_spacing(2.0f);
    toast_title = toast_context.create_element<Label>(panel, "", theme::Typography::LabelLG);
    toast_title->set_color(Color{ 255, 255, 255, 255 });
    toast_title->set_margin_top(4.0f);
    toast_text = toast_context.create_element<Label>(panel, "", theme::Typography::Body);
    toast_text->set_color(Color{ 230, 240, 250, 255 });
    toast_text->set_margin_top(6.0f);
    toast_context.close();
    if (previous != ContextId::null()) {
        previous.open();
    }
    toast_ready = true;
}
