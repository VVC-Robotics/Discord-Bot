#include <cstdlib>
#include <string>
#include <string.h>
#include <map>
#include <vector>
#include <functional>
#include <algorithm>
#include <stdio.h>
#include <fstream>
#include <format>
#include <mutex>
#include <condition_variable>

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
struct RoleData;
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

struct RoleData {
    dpp::role cached;

    dpp::snowflake id;
    std::string name;

    RoleData():id(0) {}
    RoleData(const dpp::role &role):cached(role) { }

    friend bool operator==(const RoleData &a, const std::string &b) {
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
    ChannelData *channel;
    GuildData *guild;

    std::string name;
    dpp::snowflake id;

    GuildChannelData():guild(0),channel(0) { }
    GuildChannelData(GuildData *guild_data, ChannelData *channel_data):guild(guild_data),channel(channel_data) { }
};

struct GuildData {
    dpp::guild cached;

    std::map<dpp::snowflake, RoleData> roles;
    std::map<dpp::snowflake, GuildUserData> users;
    std::map<dpp::snowflake, GuildChannelData> channels;

    GuildChannelData *welcome_channel;
    bool verify_ephemeral;

    std::string name;
    dpp::snowflake id;

    RoleData* get_role(const std::string &text) {
        return util::get_by_value_or_null(roles, text);
    }

    GuildUserData* get_user(const dpp::snowflake &user_id) {
        return util::get_or_null(users, user_id);
    }

    GuildChannelData* get_channel(const dpp::snowflake &channel_id) {
        return util::get_or_null(channels, channel_id);
    }

    GuildData() { }
    GuildData(const dpp::guild &guild):cached(guild),verify_ephemeral(1) { }
};

struct Program {
    dpp::cluster bot;
    std::map<dpp::snowflake, UserData> users;
    std::map<dpp::snowflake, GuildData> guilds;
    std::map<dpp::snowflake, ChannelData> channels;
    dpp::command_completion_event_t complete_handler;
    std::function<void(const dpp::ready_t&)> ready_handler;
    std::function<void(const dpp::guild_member_add_t&)> guild_user_add_handler;
    std::function<void(const dpp::message_create_t&)> message_handler;
    std::function<void(const dpp::button_click_t&)> button_click_handler;
    using completion_callback = std::function<void()>;

    Program() { }

    virtual int init() {
        complete_handler = std::bind(&Program::handle_confirm, this, std::placeholders::_1);
        ready_handler = std::bind(&Program::handle_ready, this, std::placeholders::_1);
        guild_user_add_handler = std::bind(&Program::handle_guild_user_add, this, std::placeholders::_1);
        message_handler = std::bind(&Program::handle_message, this, std::placeholders::_1);
        button_click_handler = std::bind(&Program::handle_button_click, this, std::placeholders::_1);

        return 0;
    }

    virtual int load() {
        this->init();

        auto token = load_token();

        if (token.size() < 1) handle_error("No token in TOKEN file");

        new (&bot) dpp::cluster(token, dpp::i_guilds | dpp::i_default_intents | dpp::i_guild_members | dpp::i_message_content);

        bot.on_ready(ready_handler);
        bot.on_guild_member_add(guild_user_add_handler);
        bot.on_message_create(message_handler);
        bot.on_button_click(button_click_handler);

        logs("Connecting");

        return 0;
    }

    int run() {
        bot.start(dpp::st_wait);
        return 0;
    }

    void safe_exit(int errcode = 0) {
        exit(errcode);
    }

    void handle_error(const char *error, int errcode = -1) {
        fprintf(stderr, "Error: %s\n", error);
        safe_exit(errcode);
    }

    std::string load_token(const char *filepath = nullptr) {
        auto path = filepath;
        if (!path) path = "TOKEN";
        auto file = std::ifstream(path, std::ios::in);
        if (!file.is_open()) return "";
        std::string ret;
        std::getline(file, ret, '\n');
        return ret;
    }

    template<typename ...Args>
    int log(const char *format, Args &&... args) {
        return fprintf(stderr, format, std::forward<Args>(args)...);
    }

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

    virtual void guild_added(std::pair<const dpp::snowflake, GuildData> &pair) {
        auto &data = pair.second;
        auto &guild = data.cached;

        auto id = data.id = guild.id;
        auto name = data.name = guild.name;

        auto *welcome_channel = data.welcome_channel = get_guild_channel(&data, guild.system_channel_id);
        
        log("Cached guild   [%lu] %s\n", id, name.c_str());

        if (welcome_channel)
            log("\twelcome_channel [%lu] %s\n", welcome_channel->id, welcome_channel->name.c_str());
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

    virtual void role_added(std::pair<const dpp::snowflake, RoleData> &pair) {

    }

    GuildChannelData *get_guild_channel(GuildData *guild, const dpp::snowflake channel_id) {
        if (!guild->channels.contains(channel_id))
            add_guild_channel(guild, channel_id);
        return util::get_or_null(guild->channels, channel_id);
    }

    GuildUserData *get_guild_user(GuildData *guild, const dpp::snowflake user_id) {
        if (!guild->users.contains(user_id))
            add_guild_user(guild->id, user_id);
        return util::get_or_null(guild->users, user_id);
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

    void add_guild_channel(GuildData *guild_data, ChannelData *channel_data, const dpp::snowflake &channel_id) {
        assert(channel_id && "channel_id should not be 0 here\n");
        guild_channel_added(*guild_data->channels.emplace(std::make_pair(channel_id, GuildChannelData(guild_data, channel_data))).first);
    }

    void add_guild_channel(GuildData *guild, const dpp::snowflake &channel_id) {
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

    void add_guild_user(const dpp::snowflake guild_id, const dpp::snowflake user_id) {
        //log("add_guild_user %lu %lu\n", guild_id, user_id);
        util::auto_wait w;
        bot.guild_get_member(guild_id, user_id, [&,guild_id,user_id](dpp::confirmation_callback_t e) {
            util::hold h(w);
            if (e.is_error()) { 
                handle_apierror(e.get_error(), std::format("guild: {} user: {}", (uint64_t)guild_id, (uint64_t)user_id));
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
                handle_apierror(e.get_error(), std::format("guild: {}", (uint64_t)guild_id));
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
                handle_apierror(e.get_error(), std::format("user: {}", (uint64_t)user_id));
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
                handle_apierror(e.get_error(), std::format("channel: {}", (uint64_t)channel_id));
                return;
            }

            add_channel(channel_id, std::get<dpp::channel>(e.value));
        });
    }

    void message_create(const dpp::message &m) {
        //logs(m.content);
        bot.message_create(m, complete_handler);
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

    void handle_ready(const dpp::ready_t &r) {
        logs("Connected");

        bot.current_user_get_guilds([&](dpp::confirmation_callback_t e) {
            if (e.is_error()) { handle_apierror(e.get_error()); return; }

            auto &guildmap = std::get<dpp::guild_map>(e.value);
            
            log("Handling %lu guilds\n", guildmap.size());

            std::for_each(guildmap.begin(), guildmap.end(), [&](auto &pair){ 
                add_guild(pair.first); // dpp::guild_map is returned incomplete, make a full request for guild data
            });
        });
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
            message_create(create_welcome_message(user.get_mention(), guild_data->welcome_channel->id));
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
        e.reply(dpp::ir_update_message, std::format("You are now verified {}!", user.get_mention()));
    }

    void add_role(dpp::snowflake guild, dpp::snowflake user, dpp::snowflake role) {
        log("Adding role %lu to user %lu in guild %lu\n", role, user, guild);

        bot.guild_member_add_role(guild, user, role, complete_handler);
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
    using namespace dpp;

    Program prog;
    prog.load();    

    return prog.run();
}