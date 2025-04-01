#include <cstdlib>
#include <string>
#include <string.h>
#include <map>
#include <vector>
#include <functional>
#include <algorithm>
#include <stdio.h>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <signal.h>
#include <fmt/format.h>

#include <dpp/json.h>
#include <dpp/export.h>
#include <dpp/version.h>
#include <dpp/snowflake.h>
#include <dpp/json_fwd.h>

namespace {

struct our_snowflake : public dpp::snowflake {
    our_snowflake(const dpp::snowflake &f):dpp::snowflake(f){}
    our_snowflake(dpp::snowflake &f):dpp::snowflake(f) {}
    our_snowflake(dpp::snowflake &&f):dpp::snowflake(f) {}
    our_snowflake(){}

    constexpr our_snowflake &operator=(const dpp::snowflake &rhs) {
        *(dpp::snowflake*)this = rhs;
        return *this;
    }

    constexpr our_snowflake &operator=(dpp::snowflake &&rhs) {
        *(dpp::snowflake*)this = rhs;
        return *this;
    }

    operator nlohmann::json() = delete;

    friend bool operator==(const our_snowflake &a, const our_snowflake &b) {
        return ((dpp::snowflake)a) == ((dpp::snowflake)b);
    }

    friend bool operator==(const our_snowflake &a, const dpp::snowflake &b) {
        return ((dpp::snowflake)a) == b;
    }
};

}
    
NLOHMANN_JSON_NAMESPACE_BEGIN

template<>
struct adl_serializer<our_snowflake> {
    static void from_json(const nlohmann::json &j, our_snowflake &value) {
        if (j.is_string()) {
            value = our_snowflake(j.template get<std::string>());
        } else
        if (j.is_number()) {
            value = our_snowflake(j.template get<uint64_t>());
        } else {
            value = our_snowflake(0);
        }
    }

    static void to_json(nlohmann::json &j, const our_snowflake &value) {
        j = value.operator uint64_t();
    }
};

NLOHMANN_JSON_NAMESPACE_END

#include <dpp/dpp.h>

namespace util {
    template<typename map, typename key_type = map::key_type, typename value_type = map::mapped_type>
    value_type *get_or_null(map &it, const key_type &key) {
        auto end = it.end();
        auto iter = it.find(key);
        if (iter == end) return (value_type*)(nullptr);
        return &(iter->second);
    }
    
    template<typename map, typename find_type, typename value_type = map::mapped_type>
    value_type *get_by_value_or_null(map &it, const find_type &value) {
        for (auto &[k, v] : it)
            if (v == value)
                return &v;
        return (value_type*)(nullptr);
    }

    struct hold;

    struct wait_for {
        bool done;
        std::mutex m;
        std::condition_variable cv;

        wait_for():done(false){}

        void notify() {
            std::unique_lock<std::mutex> lock(m);
            done = true;
            //printf("notify     %p\n", this);
            cv.notify_all();
        }

        void wait() {
            //printf("wait       %p\n", this);
            std::unique_lock<std::mutex> lock(m);
            while (!done) cv.wait(lock);
            //printf("complete   %p\n", this);
        }

        util::hold hold();
    };

    struct auto_wait : public wait_for {
        auto_wait() {
            //printf("auto_wait  %p\n", this);
        }

        ~auto_wait() {
            wait();
            //printf("~auto_wait %p\n", this);
        }
    };

    struct hold {
        hold(wait_for *w):w(w) { 
            //printf("auto_hold  %p\n", w);
        }
        hold(wait_for &w):hold(&w) { }
        hold(auto_wait &w):hold(&w) { }
        ~hold() {
            w->notify();
            //printf("~auto_hold %p\n", w);
        }
        wait_for *w;
    };

    util::hold util::wait_for::hold() {
        return { this };
    }
}

struct UserData;
struct GuildRoleData;
struct GuildUserData;
struct GuildData;

struct UserData {
    dpp::user cached;

    dpp::snowflake id;
    std::string username;
    std::string display_name;

    UserData() { }
    UserData(const dpp::user &user):cached(user) { }
};

struct GuildRoleData {
    dpp::role cached;

    GuildData *guild;

    dpp::snowflake id;
    std::string name;

    GuildRoleData():id(0),guild(0) {}
    GuildRoleData(GuildData *guild, const dpp::role &role):guild(guild),cached(role) { }

    friend bool operator==(const GuildRoleData &a, const std::string &b) {
        return a.name == b;
    }
};

struct GuildUserData {
    dpp::guild_member cached;

    UserData *user;
    GuildData *guild;

    std::string nickname;

    GuildUserData():user(0),guild(0) { }
    GuildUserData(UserData *user, GuildData *guild, const dpp::guild_member &guild_member):user(user),guild(guild),cached(guild_member) { }
};

struct ChannelData {
    dpp::channel cached;

    std::string name;
    dpp::snowflake id;

    ChannelData() { }
    ChannelData(const dpp::channel &channel):cached(channel) { }
};

struct GuildChannelData {
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(GuildChannelData, id, bot_allowed);

    ChannelData *channel;
    GuildData *guild;

    std::string name;
    our_snowflake id;

    bool bot_allowed;

    GuildChannelData():guild(0),channel(0),bot_allowed(1) { }
    GuildChannelData(GuildData *guild_data, ChannelData *channel_data):guild(guild_data),channel(channel_data),bot_allowed(1) { }
};

struct GuildData {
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(GuildData, id, name, verify_ephemeral, interact_ephemeral, welcome_channel, verify_role, bot_operator_role);

    dpp::guild cached;

    std::map<dpp::snowflake, GuildRoleData> roles;
    std::map<dpp::snowflake, GuildUserData> users;
    std::map<dpp::snowflake, GuildChannelData> channels;

    our_snowflake welcome_channel;
    our_snowflake verify_role;
    our_snowflake bot_operator_role;

    bool verify_ephemeral;
    bool interact_ephemeral;

    std::string name;
    our_snowflake id;

    GuildRoleData* get_role(const std::string &text) {
        return util::get_by_value_or_null(roles, text);
    }

    GuildRoleData* get_role(const dpp::snowflake &role_id) {
        return util::get_or_null(roles, role_id);
    }

    GuildUserData* get_user(const dpp::snowflake &user_id) {
        return util::get_or_null(users, user_id);
    }

    GuildChannelData* get_channel(const dpp::snowflake &channel_id) {
        return util::get_or_null(channels, channel_id);
    }

    GuildData() { }
    GuildData(const dpp::guild &guild):cached(guild),verify_ephemeral(1),interact_ephemeral(1) { }
};

struct ConfigData {
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(ConfigData, token_file, token, config_data_file, bot_data_file, pool_size);

    std::string token_file;
    std::string token;

    std::string bot_data_file;
    std::string config_data_file;

    uint32_t pool_size;

    int load_config() {
        if (!config_data_file.size()) return log_config("No path for config data\n");

        std::ifstream file(config_data_file);

        if (!file.is_open()) {
            log_config("No config data found, creating\n");
            return save_config();
        } 

        nlohmann::json j;

        if (!j.accept(file)) return log_config("Invalid config json data\n");

        file.seekg(0);
        file >> j;

        *this = j.template get<ConfigData>();

        log_config("Loaded config data json\n");

        return 0;
    }
    
    int save_config() {
        if (!config_data_file.size()) return log_config("No path for config data\n");

        std::ofstream file(config_data_file);

        if (!file.is_open()) return log_config("Could not open config data path for saving\n");

        nlohmann::json j = *this;

        file << j;

        log_config("Saved config data json\n");

        return 0;
    }

    int load_token() {
        if (token.size()) return 0;
        if (!token_file.size()) return log_config("No TOKEN file and supplied token in config is undefined\n");
        std::ifstream file(token_file);
        if (!file.is_open()) return log_config(fmt::format("Could not open token_file ({})\n", token_file));
        std::getline(file, token, '\n');
        if (!token.size()) return log_config("token length in TOKEN file is 0\n");
        return 0;
    }

    ConfigData()
        :token_file("TOKEN"),
         token(),
         config_data_file("config.json"),
         bot_data_file("data.json"),
         pool_size(0) { }

    protected:

    int log_config(const std::string &str) {
        return fprintf(stderr, "%s", str.c_str());
    }
};

struct BotDataContainer {
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BotDataContainer, guilds);

    std::map<dpp::snowflake, UserData> users;
    std::map<our_snowflake, GuildData> guilds;
    std::map<dpp::snowflake, ChannelData> channels;
};

struct BotData : public ConfigData, public BotDataContainer {
    dpp::cluster bot;

    int load_data() {
        if (!bot_data_file.size()) return log_config("No path for bot data\n");

        std::ifstream file(bot_data_file);

        if (!file.is_open()) return log_config("No bot data found, creating\n");

        nlohmann::json j;

        if (!j.accept(file)) return log_config("Invalid bot data json data\n");

        file.seekg(0);
        file >> j;

        *(BotDataContainer*)this = j.template get<BotDataContainer>();

        log_config("Loaded bot data json\n");

        return 0;
    }

    int save_data() {
        if (!bot_data_file.size()) return log_config("No path for bot data\n");

        std::ofstream file(bot_data_file);

        if (!file.is_open()) return log_config("Could not open bot data path for saving\n");

        nlohmann::json j = *(BotDataContainer*)this;

        file << j;

        log_config("Saved bot data json\n");

        return 0;
    }
};

#undef log
#define log(format, ...) fprintf(stderr, format __VA_OPT__(,) __VA_ARGS__)

struct Program : public BotData {
    std::function<void(const dpp::confirmation_callback_t&)> confirmation_handler;
    std::function<void(const dpp::ready_t&)> ready_handler;
    std::function<void(const dpp::slashcommand_t&)> slashcommand_handler;
    std::function<void(const dpp::guild_member_add_t&)> guild_user_add_handler;
    std::function<void(const dpp::message_create_t&)> message_handler;
    std::function<void(const dpp::button_click_t&)> button_click_handler;

    std::function<void(int)> signal_handler;
    
    bool did_init = false;
    bool did_load = false;

    Program() { }

    virtual int init() {
        confirmation_handler = std::bind(&Program::handle_confirm, this, std::placeholders::_1);
        ready_handler = std::bind(&Program::handle_ready, this, std::placeholders::_1);
        guild_user_add_handler = std::bind(&Program::handle_guild_user_add, this, std::placeholders::_1);
        message_handler = std::bind(&Program::handle_message, this, std::placeholders::_1);
        button_click_handler = std::bind(&Program::handle_button_click, this, std::placeholders::_1);
        slashcommand_handler = std::bind(&Program::handle_slashcommand, this, std::placeholders::_1);
        signal_handler = std::bind(&Program::handle_signal, this, std::placeholders::_1);

        did_init = true;
        return 0;
    }

    virtual int load() {
        if (!did_init)
            if (this->init())
                return -1;

        load_config();
        load_data();

        if (load_token())
            handle_error("No token supplied");

        new (&bot) dpp::cluster(token, dpp::i_guilds | dpp::i_default_intents | dpp::i_guild_members | dpp::i_message_content);

        bot.on_ready(ready_handler);
        bot.on_guild_member_add(guild_user_add_handler);
        bot.on_message_create(message_handler);
        bot.on_button_click(button_click_handler);
        bot.on_slashcommand(slashcommand_handler);

        logs("Connecting");

        did_load = true;
        return 0;
    }

    virtual int save() {
        save_data();

        return 0;
    }

    virtual int run() {
        if (!did_load)
            if (this->load())
                return -1;

        bot.start(dpp::st_wait);
        save();
        return 0;
    }

    virtual void safe_exit(int errcode = 0) {
        exit(errcode);
    }

    virtual void handle_error(const char *error, int errcode = -1) {
        fprintf(stderr, "Error: %s\n", error);
        safe_exit(errcode);
    }

    virtual void hint_exit() {
        logs("Terminating");
        bot.terminating = true;
    }

    /*
    #pragma GCC diagnostic ignored "-Wformat-security"
    template<typename ...Args>
    constexpr __attribute__((always_inline)) int log(const char* __restrict format, Args &&... args) const {
        return std::fprintf(stderr, format, std::forward<Args>(args)...);
    }
    #pragma GCC diagnostic warning "-Wformat-security"
    */

    int logs(const char *str) {
        return log("%s\n", str);
    }

    int logs(const std::string &str) {
        return log("%s\n", str.c_str());
    }

    int logs(const dpp::component &c) {
        int sum = 0;
        if (c.content.size() > 0)
            sum += log("%s", c.content);
        return sum + logs(c.components);
    }

    int logs(const std::vector<dpp::component> &c) {
        int sum = 0;
        for (const auto &_c : c)
            sum += logs(_c);
        return sum;
    }

    int logs(const dpp::message &m) {
        return logs(m.components) + log("[%lu] %s \"%s\"\n", m.author.id, m.author.username.c_str(), m.content.c_str());
    }

    virtual void guild_user_added(std::pair<const dpp::snowflake, GuildUserData> &pair) {
        auto &guser_data = pair.second;
        auto &guser = guser_data.cached;

        auto id = guser.user_id;
        auto nickname = guser_data.nickname = guser.get_nickname();
        auto username = guser_data.user->username;
        auto guild_id = guser_data.guild->id;
        auto guild_name = guser_data.guild->name;

        log("Cached guser   [%lu] %s [%lu] %s\n", id, username.c_str(), guild_id, guild_name.c_str());
    }

    virtual void user_added(std::pair<const dpp::snowflake, UserData> &pair) {
        auto &user_data = pair.second;
        auto &user = user_data.cached;

        auto id = user_data.id = user.id;
        auto name = user_data.username = user.username;
        auto display = user_data.display_name = user.global_name;

        log("Cached user    [%lu] (%s) %s\n", id, name.c_str(), display.c_str());
    }

    virtual void guild_added(std::pair<const our_snowflake, GuildData> &pair) {
        auto &data = pair.second;
        auto &guild = data.cached;

        auto id = data.id = guild.id;
        auto name = data.name = guild.name;

        auto *welcome_channel = get_guild_channel(&data, guild.system_channel_id);

        log("Cached guild   [%lu] %s\n", id, name.c_str());

        if (welcome_channel) {
            data.welcome_channel = welcome_channel->id;
            log("\twelcome_channel [%lu] %s\n", welcome_channel->id, welcome_channel->name.c_str());
        }
    }

    virtual void channel_added(std::pair<const dpp::snowflake, ChannelData> &pair) {
        auto &data = pair.second;
        auto &channel = data.cached;

        auto id = data.id = channel.id;
        auto name = data.name = channel.name;

        log("Cached channel [%lu] %s\n", id, name.c_str());
    }    

    virtual void guild_channel_added(std::pair<const dpp::snowflake, GuildChannelData> &pair) {
        auto &data = pair.second;
        auto *channel = data.channel;
        auto *guild = data.guild;

        assert(channel && "Channel null\n");
        assert(guild && "Guild null\n");        

        data.id = data.channel->id;
        data.name = data.channel->name;

        log("Cached gchannel %p %p\n", data.guild, data.channel);
    }

    virtual void guild_role_added(std::pair<const dpp::snowflake, GuildRoleData> &pair) {
        auto &data = pair.second;
        auto *guild = data.guild;
        auto &role = data.cached;
    
        assert(guild && "Guild null\n");

        data.id = role.id;
        data.name = role.name;

        log("Cached grole %p %lu\n", data.guild, data.id);
    }

    virtual void role_added(std::pair<const dpp::snowflake, GuildRoleData> &pair) {

    }

    GuildChannelData *get_guild_channel(GuildData *guild, const dpp::snowflake channel_id) {
        assert(guild && "guild is null\n");
        if (!guild->channels.contains(channel_id))
            add_guild_channel(guild, channel_id);
        return util::get_or_null(guild->channels, channel_id);
    }

    GuildUserData *get_guild_user(GuildData *guild, const dpp::snowflake user_id) {
        assert(guild && "guild is null\n");
        if (!guild->users.contains(user_id))
            add_guild_user(guild->id, user_id);
        return util::get_or_null(guild->users, user_id);
    }

    GuildRoleData *get_guild_role(GuildData *guild, const dpp::snowflake role_id) {
        assert(guild && "guild is null\n");
        //return guild->get_role(role_id);
        if (!guild->roles.contains(role_id))
            add_guild_role(guild->id, role_id);
        return util::get_or_null(guild->roles, role_id);
    }

    GuildRoleData *get_guild_role(GuildData *guild, const std::string &role_name) {
        assert(guild && "guild is null\n");
        return guild->get_role(role_name);
    }

    ChannelData *get_channel(const dpp::snowflake channel_id) {
        if (!channels.contains(channel_id))
            add_channel(channel_id);
        return util::get_or_null(channels, channel_id);
    }

    UserData *get_user(const dpp::snowflake &user_id) {
        //log("get_user %lu\n", user_id);
        if (!users.contains(user_id))
            add_user(user_id);
        return util::get_or_null(users, user_id);
    }

    GuildData *get_guild(const dpp::snowflake guild_id) {
        //log("get_guild %lu\n", guild_id);
        if (!guilds.contains(guild_id))
            add_guild(guild_id);
        return util::get_or_null(guilds, guild_id);
    }

    ChannelData *get_channel(const dpp::channel &channel) {
        return get_channel(channel.id);
    }

    GuildData *get_guild(const dpp::guild &guild) {
        return get_guild(guild.id);
    }

    GuildUserData *get_guild_user(const dpp::guild_member &user) {
        auto *guild_data = get_guild(user.guild_id);
        //log("get_guild_user %lu %p %lu\n", user.guild_id, guild_data, user.user_id);
        if (!guild_data) return nullptr;
        return get_guild_user(guild_data, user.user_id);
    }

    UserData *get_user(const dpp::user &user) {
        return get_user(user.id);
    }

    void add_channel(const dpp::snowflake channel_id, dpp::channel &channel) {
        assert(channel_id && "channel_id should not be 0 here\n");
        channel_added(*channels.emplace(std::make_pair(channel_id, channel)).first);
    }

    void add_guild(const dpp::snowflake guild_id, dpp::guild &guild) {
        assert(guild_id && "guild_id should not be 0 here\n");
        guild_added(*guilds.emplace(std::make_pair(guild_id, guild)).first);
    }

    void add_user(const dpp::snowflake &user_id, dpp::user &user) {
        assert(user_id && "user_id should not be 0 here\n");
        //log("add_user %lu %s\n", user_id, user.username.c_str());
        user_added(*users.emplace(std::make_pair(user_id, user)).first);
    }

    void add_guild_user(GuildData *guild_data, UserData *user_data, const dpp::snowflake &user_id, dpp::guild_member &guild_member) {
        assert(user_id && "user_id should not be 0 here\n");
        guild_user_added(*guild_data->users.emplace(std::make_pair(user_id, GuildUserData(user_data, guild_data, guild_member))).first);
    }

    void add_guild_role(GuildData *guild_data, const dpp::snowflake &role_id, dpp::role &guild_role) {
        assert(role_id && "role_id should not be 0 here\n");
        guild_role_added(*guild_data->roles.emplace(std::make_pair(role_id, GuildRoleData(guild_data, guild_role))).first);
    }

    void add_guild_channel(GuildData *guild_data, ChannelData *channel_data, const dpp::snowflake &channel_id) {
        assert(guild_data && "guild_data is null\n");
        assert(channel_id && "channel_id should not be 0 here\n");
        guild_channel_added(*guild_data->channels.emplace(std::make_pair(our_snowflake(channel_id), GuildChannelData(guild_data, channel_data))).first);
    }

    void add_guild_channel(GuildData *guild, const dpp::snowflake &channel_id) {
        assert(guild && "guild is null\n");
        if (!channel_id) {
            logs("Channel id is 0");
            return;
        }

        auto *channel = get_channel(channel_id);
        
        if (!channel) {
            logs("No channel to associate with guild");
            return;
        }

        add_guild_channel(guild, channel, channel_id);
    }

    void add_guild_user(dpp::guild_member &guild_member) {
        auto *guild_data = get_guild(guild_member.guild_id);
        auto *user_data = get_user(guild_member.user_id);
        
        if (!guild_data) {
            logs("No guild to associate with user");
            return;
        }

        if (!user_data) {
            logs("No user to associate with guild");
            return;
        }

        add_guild_user(guild_data, user_data, guild_member.user_id, guild_member);
    }

    void add_guild_user(GuildData *guild, const dpp::snowflake &user_id) {
        add_guild_user(guild->id, user_id);
    }

    void add_guild(std::pair<const dpp::snowflake, dpp::guild> &pair) {
        add_guild(pair.first, pair.second);
    }

    void add_guild_role(const dpp::snowflake guild_id, const dpp::snowflake role_id) {
        util::auto_wait w;
        auto *guild = get_guild(guild_id);
        if (!guild) return;
        bot.roles_get(guild_id, [&,guild_id,role_id](dpp::confirmation_callback_t e) {
            util::hold h(w);
            if (e.is_error()) {
                handle_apierror(e.get_error(), fmt::format("guild: {} getroles", (uint64_t)guild_id));
                return;
            }
            
            auto &roles = std::get<dpp::role_map>(e.value);
            for (auto &r : roles) {
                add_guild_role(guild, r.first, r.second);
            }
        });
    }

    void add_guild_user(const dpp::snowflake guild_id, const dpp::snowflake user_id) {
        //log("add_guild_user %lu %lu\n", guild_id, user_id);
        util::auto_wait w;
        bot.guild_get_member(guild_id, user_id, [&,guild_id,user_id](dpp::confirmation_callback_t e) {
            util::hold h(w);
            if (e.is_error()) { 
                handle_apierror(e.get_error(), fmt::format("guild: {} user: {}", (uint64_t)guild_id, (uint64_t)user_id));
                return;
            }

            add_guild_user(std::get<dpp::guild_member>(e.value));
            //logs("done add_guild_user");
        });
    }

    void add_guild(const dpp::snowflake guild_id) {
        util::auto_wait w;
        bot.guild_get(guild_id, [&,guild_id](dpp::confirmation_callback_t e) {
            util::hold h(w);
            if (e.is_error()) { 
                handle_apierror(e.get_error(), fmt::format("guild: {}", (uint64_t)guild_id));
                return;
            }

            add_guild(guild_id, std::get<dpp::guild>(e.value));
        });
    }

    void add_user(const dpp::snowflake user_id) {
        //log("add_user %lu\n", user_id);
        util::auto_wait w;
        bot.user_get(user_id, [&,user_id](dpp::confirmation_callback_t e) {
            util::hold h(w); 
            if (e.is_error()) { 
                handle_apierror(e.get_error(), fmt::format("user: {}", (uint64_t)user_id));
                return;
            }

            add_user(user_id, std::get<dpp::user_identified>(e.value));
        });
    }

    void add_channel(const dpp::snowflake channel_id) {
        util::auto_wait w;
        bot.channel_get(channel_id, [&,channel_id](dpp::confirmation_callback_t e) {
            util::hold h(w);
            if (e.is_error()) { 
                handle_apierror(e.get_error(), fmt::format("channel: {}", (uint64_t)channel_id));
                return;
            }

            add_channel(channel_id, std::get<dpp::channel>(e.value));
        });
    }

    void message_create(const dpp::message &m) {
        //logs(m.content);
        bot.message_create(m, confirmation_handler);
    }

    int handle_apierror(const dpp::error_info &e, std::string extra = "") {
        int sum = log("Error in confirmation\nError brief: %s\nError body: %s\n", e.message.c_str(), e.human_readable.c_str());
        for (const auto &s : e.errors)
            sum += logs(s.reason);
        if (extra.size())
            sum += log("Details: %s\n", extra.c_str());
        return sum;
    }

    int handle_confirm(const dpp::confirmation_callback_t &e) {
        if (e.is_error()) 
            return handle_apierror(e.get_error());
        return 0;
    }

    template<typename T>
    std::string or_default(T *v, std::string or_str = "`Not set`") {
        if (!v) {
            return or_str;
        }
        return v->cached.get_mention();
    }

    void handle_slashcommand(const dpp::slashcommand_t &e) {
        auto &command = e.command;
        const std::string name = command.get_command_name();
        auto interaction = command.get_command_interaction();
        auto &ops = interaction.options;
        auto &channel = command.channel;
        auto &channel_id = command.channel_id;
        auto argc = ops.size();

        auto base_message = dpp::message()
                                .set_channel_id(channel_id);

        auto base_embed = dpp::embed()
                                .set_color(dpp::colors::sti_blue)
                                ;//.set_author("Club Robot", bot.me.get_url(), bot.me.get_avatar_url());

        auto base_moreargs = dpp::message(base_message).add_embed(dpp::embed(base_embed).set_description("More arguments required"));

        auto make_base = [&base_message, &base_embed](const std::string_view &str) {
            return base_message.add_embed(base_embed.set_description(str));
        };

        if (!command.is_guild_interaction()) {
            e.reply(base_message.set_content("I only support commands on servers right now"));
            return;
        }

        auto *guild = get_guild(command.guild_id);

        if (!guild) {
            e.reply(base_message.set_content("An error occured!"));
            return;
        }

        bool guild_ephemeral = guild->interact_ephemeral;

        {
            //util::auto_wait w;
            //e.thinking(guild_ephemeral, [&w](const auto &d){w.notify();});
        }

        if (guild_ephemeral)
            base_message = base_message.set_flags(dpp::m_ephemeral);

        if (argc < 1) {
            e.reply(base_moreargs);
            return;
        }

        if (name == "help") {
            e.reply(base_message
                    .add_embed(
                    base_embed
                    .set_description("Verification bot")
                    ),
                confirmation_handler
            );
            return;
        }

        if (name == "setup") {
            if (ops[0].name == "role") {
                auto crole = std::get<dpp::snowflake>(e.get_parameter("role"));
                auto *role = get_guild_role(guild, crole);
                if (!role) {
                    e.reply(make_base(fmt::format("Failed to set role to {}", crole)));
                    return;
                }
                guild->bot_operator_role = crole;
                e.reply(make_base(fmt::format("Set bot operator role to {}", or_default(role, role->name))));
                return;
            }
            if (ops[0].name == "visibility") {
                auto cvisi = std::get<bool>(e.get_parameter("visibility"));
                guild->interact_ephemeral = !cvisi;
                e.reply(make_base(fmt::format("Set reply visibility to `{}`", cvisi)));
                return;
            }
            if (ops[0].name == "welcome_channel") {
                auto cchan = std::get<dpp::snowflake>(e.get_parameter("welcome_channel"));
                auto *chan = get_guild_channel(guild, cchan);
                if (!chan) {
                    e.reply(make_base(fmt::format("Failed to set welcome channel to {}", cchan)));
                    return;
                }
                e.reply(make_base(fmt::format("Set welcome channel to {}", or_default(chan->channel, chan->name))));
                return;
            }
            return;
        }

        if (name == "info") {
            auto *welcome_channel = get_guild_channel(guild, guild->welcome_channel);
            if (ops[0].name == "server") {
                e.reply(base_message.add_embed(base_embed.set_description(
                    fmt::format("\
Verification role \n\
{} \n\
Welcome channel \n\
{} \n\
Hide messages \n\
`{}` \
", 
or_default(get_guild_role(guild, guild->verify_role)),
welcome_channel ? or_default(welcome_channel->channel) : "`Not set`",
guild->interact_ephemeral
                    )
                )));
                return;
            }
            if (ops[0].name == "bot") {
                e.reply(make_base("\
Verification bot cortesy of VVC Robotics \n\
https://github.com/VVC-Robotics/Discord-Bot \
"
));
                return;
            }
            e.reply(base_moreargs);
            return;
        }

        if (name == "verify") {
            if (ops[0].name == "role") {
                auto crole = std::get<dpp::snowflake>(e.get_parameter("role"));
                auto *role = get_guild_role(guild, crole);
                if (!role) {
                    e.reply(make_base(fmt::format("Failed to set role to {}", crole)));
                    return;
                }
                guild->verify_role = crole;
                e.reply(make_base(fmt::format("Set bot operator role to {}", or_default(role, role->name))));
                return;
            }
            if (ops[0].name == "user") {
                auto *vrole = get_guild_role(guild, guild->verify_role);
                dpp::snowflake vroleid = vrole ? vrole->id : dpp::snowflake(0);
                auto cuser = std::get<dpp::snowflake>(e.get_parameter("set"));
                auto *user = get_guild_user(guild, cuser);
                if (!user || !ops[0].options.size()) {
                    e.reply(make_base(fmt::format("Failed to set user's role {}", cuser)));
                    return;
                }
                if (ops[0].name == "set") {
                    if (vroleid)
                        add_role(guild->id, cuser, vroleid);
                    else
                        add_or_create_role(guild->id, cuser, "Verified");
                    e.reply(make_base(fmt::format("Set {} as verified", or_default(user->user, user->user->username))));
                    return;
                }
                if (ops[1].name == "clear") {
                    if (!vroleid) {
                        auto *t = guild->get_role("Verified");
                        if (t) vroleid = t->id;
                    }
                    if (!vroleid) {
                        e.reply(make_base("No verified role!"));
                        return;
                    }
                    user->cached.remove_role(vroleid);
                    e.reply(make_base(fmt::format("Cleared verification of {}", or_default(user->user, user->user->username))));
                    return;
                }
            }
            return;
        }

        e.reply(base_moreargs);
        return;
    }

    void handle_ready(const dpp::ready_t &r) {
        logs("Connected");
        bot.set_presence(dpp::presence(dpp::ps_online, dpp::activity(dpp::activity_type::at_custom, ".", "Use /", "")));

        using sc = dpp::slashcommand;
        using co = dpp::command_option;
        using coc = dpp::command_option_choice;
        auto csc = dpp::co_sub_command;

        sc setup("setup", "Admin set up", bot.me.id);
        sc help("help", "Get help", bot.me.id);
        sc verify("verify", "Verification", bot.me.id);
        sc info("info", "Get info", bot.me.id);

        std::vector<co> setup_ops = {
            co(dpp::co_boolean, "visibility", "Set the visibility of my replies"),
            co(dpp::co_channel, "welcome_channel", "Set the welcome channel"),
            co(dpp::co_role, "role", "Set bot operator role")
        };

        std::vector<co> verify_ops = {
            co(csc, "all", "Set all members as verified"),
            co(csc, "none", "Clear verification status of all members"),
            co(dpp::co_user, "user", "User options")
                .add_choice(coc("set", "Set verification"))
                .add_choice(coc("clear", "Clear verification")),
            co(dpp::co_role, "role", "Set verification role")
        };

        std::vector<co> info_ops = {
            co(csc, "server", "Get current server config"),
            co(csc, "bot", "Get bot info")
        };

        auto add_ops = [](auto &v, auto &ops) {
            for (auto &c : ops)
                v.add_option(c);
        };

        add_ops(setup, setup_ops);
        add_ops(verify, verify_ops);
        add_ops(info, info_ops);

        std::vector<sc> commands = { setup, help, verify, info };

        if (!commands.empty())    
            bot.global_bulk_command_create(commands, confirmation_handler);
    
        {
        util::auto_wait w;
        bot.current_user_get_guilds([&](dpp::confirmation_callback_t e) -> dpp::task <void> {
            util::hold h(w);
            if (e.is_error()) { handle_apierror(e.get_error()); co_return; }

            auto &guildmap = std::get<dpp::guild_map>(e.value);
            
            log("Handling %lu guilds\n", guildmap.size());

            std::for_each(guildmap.begin(), guildmap.end(), [&](auto &pair){ 
                //add_guild(pair.first); // dpp::guild_map is returned incomplete, make a full request for guild data
                auto guild_id = pair.first;
                GuildData *cached = get_guild(guild_id);

                if (!cached || !cached->id) {
                    add_guild(pair.first);
                    return;
                }
                
                log("Found guild    [%lu] %s\n", cached->id, cached->name.c_str());
            });

            co_return;
        });
        }

        bot.start_timer([&](const dpp::timer& h) {
            save();
        }, 300);

        logs("Ready");
    }

    void handle_guild_user_add(const dpp::guild_member_add_t &e) {
        auto &guild = e.adding_guild;
        auto *guild_data = get_guild(guild);

        if (!guild_data) {
            logs("User added with no guild data associated");
            return;
        }

        auto &user = e.added;
        auto *user_data = get_user(user.user_id);
    
        if (!user_data) {
            logs("User added with no user data associated");
            return;
        }

        log("Cached guild user [%lu] %s");

        if (guild_data->welcome_channel) {
            message_create(create_welcome_message(user.get_mention(), guild_data->welcome_channel));
        } else {
            logs("No verification channel");
        }
    }

    void handle_message(const dpp::message_create_t &e) {
        logs(e.msg);

        if (e.msg.content == "devtest")
            message_create(create_welcome_message(e.msg.author.get_mention(), e.msg.channel_id));
    }

    void handle_button_click(const dpp::button_click_t &e) {
        auto &id = e.custom_id;
        auto &command = e.command;

        logs("button click");

        const auto &issuer = e.command.get_issuing_user();

        auto *user = get_user(issuer);

        log("Button clicked \"%s\" by %s\n", id.c_str(), user ? user->username.c_str() : "undefined");

        if (id == "verify_button") on_user_verify(e);
    }

    virtual void handle_signal(int sig) {
        log("\nSignal %i received\n", sig);
        this->hint_exit();
    }

    virtual void on_user_verify(const dpp::button_click_t &e) {
        auto &command = e.command;
        if (!command.is_guild_interaction()) {
            logs("User verification in guilds only");
            return;
        }

        auto &user = command.member;
        auto *guild_user = get_guild_user(user);
        
        if (!guild_user) {
            logs("No guild user associated with interaction");
            return;
        }

        add_or_create_role(guild_user, "Verified");
        e.reply(dpp::ir_update_message, fmt::format("You are now verified {}!", user.get_mention()));
    }

    void add_role(dpp::snowflake guild, dpp::snowflake user, dpp::snowflake role) {
        log("Adding role %lu to user %lu in guild %lu\n", role, user, guild);

        bot.guild_member_add_role(guild, user, role, confirmation_handler);
    }

    void create_role(dpp::snowflake guild, dpp::snowflake user, std::string role_name) {
        log("Creating role \"%s\" for user %lu in guild %lu\n", role_name.c_str(), user, guild);

        bot.role_create(dpp::role().set_name(role_name).set_guild_id(guild), [&,guild,user,role_name](dpp::confirmation_callback_t e) {
            if (e.is_error()) { handle_apierror(e.get_error()); return; }

            auto role = std::get<dpp::role>(e.value);

            add_role(guild, user, role.id);
        });
    }

    void add_or_create_role(dpp::snowflake guild, dpp::snowflake user, std::string role_name) {
        bot.roles_get(guild, [&,guild,user,role_name](dpp::confirmation_callback_t e) {
            if (e.is_error()) { handle_apierror(e.get_error()); return; }

            auto &roles = std::get<dpp::role_map>(e.value);
            auto iter = std::find_if(roles.begin(), roles.end(), [role_name](const auto &p) { return p.second.name == role_name; });

            if (iter != roles.end())
                add_role(guild, user, (*iter).second.id);
            else
                create_role(guild, user, role_name);
        });
    }

    void add_or_create_role(GuildUserData *user, std::string role_name) {
        add_or_create_role(user->guild->id, user->user->id, role_name);
    }

    dpp::message create_welcome_message(std::string user_mention, dpp::snowflake channel_id) {
        auto m = dpp::message();

        m.set_channel_id(channel_id);
        m.set_content("Welcome " + user_mention + "!\n\nClick the button to become verified!");
        
        // Set later
        //m.set_flags(dpp::m_ephemeral);

        m.add_component(dpp::component()
            .add_component(dpp::component()
            .set_type(dpp::cot_button)
            .set_label("Verify")
            .set_style(dpp::cos_primary)
            .set_id("verify_button"))
        );

        return m;
    }
};

int main() {
    static Program prog;

    prog.load();    
    signal(SIGINT, [](int i){ prog.signal_handler(i); });

    return prog.run();
}