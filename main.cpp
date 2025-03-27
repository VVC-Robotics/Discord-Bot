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

#include <dpp/dpp.h>

struct UserData;
struct RoleData;
struct GuildUserData;
struct GuildData;

struct UserData {
    dpp::user cached;

    dpp::snowflake id;
    std::string username;

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
    UserData *user;
    GuildData *guild;

    GuildUserData():user(0),guild(0) { }
    GuildUserData(UserData *user, GuildData *guild):user(user),guild(guild) { }
};

struct GuildChannelData {
    dpp::channel cached;
    GuildData *guild;

    std::string name;
    dpp::snowflake id;

    GuildChannelData() { }
    GuildChannelData(const dpp::channel &channel):cached(channel) { }
};

struct GuildData {
    dpp::guild cached;

    std::map<dpp::snowflake, RoleData> roles;

    GuildChannelData *welcome_channel;
    bool verify_ephemeral;

    std::string name;
    dpp::snowflake id;

    RoleData* get_role(const std::string &text) {
        for (auto &[s, r] : roles)
            if (r == text)
                return &r;
        return nullptr;
    }

    GuildData() { }
    GuildData(const dpp::guild &guild):cached(guild),verify_ephemeral(1) { }
};

struct Program {
    dpp::cluster bot;
    std::map<dpp::snowflake, GuildData> guilds;
    std::map<dpp::snowflake, GuildChannelData> channels;
    dpp::command_completion_event_t complete_handler;
    std::function<void(const dpp::ready_t&)> ready_handler;

    Program() {
        complete_handler = std::bind(&Program::handle_confirm, this, std::placeholders::_1);
        ready_handler = std::bind(&Program::handle_ready, this, std::placeholders::_1);
    }

    int load() {
        auto token = load_token();

        if (token.size() < 1) handle_error("No token in TOKEN file");

        new (&bot) dpp::cluster(token, dpp::i_guilds | dpp::i_default_intents | dpp::i_guild_members | dpp::i_message_content);

        bot.on_ready(ready_handler);

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

    virtual void guild_added(std::pair<const dpp::snowflake, GuildData> &pair) {
        auto &data = pair.second;
        auto &guild = data.cached;

        auto id = data.id = guild.id;
        auto name = data.name = guild.name;

        auto *welcome_channel = data.welcome_channel = get_channel(guild.system_channel_id);
        
        log("Cached guild   [%lu] %s\n", id, name.c_str());
    }

    virtual void channel_added(std::pair<const dpp::snowflake, GuildChannelData> &pair) {
        auto &data = pair.second;
        auto &channel = data.cached;

        auto id = data.id = channel.id;
        auto name = data.name = channel.name;

        auto *guild = data.guild = get_guild(channel.guild_id);

        log("Cached channel [%lu] %s\n", id, name.c_str());
    }    

    virtual void role_added(std::pair<const dpp::snowflake, RoleData> &pair) {

    }

    template<typename iterable, typename kv, typename ret = iterable::mapped_type>
    ret *get_or_null(iterable &it, kv &k) {
        auto end = it.end();
        //auto iter = std::find(it.begin(), end, k);
        auto iter = it.find(k);
        if (iter == end) return (ret*)(nullptr);
        return &(iter->second);
    }

    GuildChannelData *get_channel(const dpp::snowflake channel_id) {
        if (!channels.contains(channel_id))
            add_channel(channel_id);
        return get_or_null(channels, channel_id);
    }

    GuildData *get_guild(const dpp::snowflake guild_id) { 
        if (!guilds.contains(guild_id))
            add_guild(guild_id);
        return get_or_null(guilds, guild_id);
    }

    void add_channel(const dpp::snowflake channel_id, dpp::channel &channel) {
        channel_added(*channels.emplace(std::make_pair(channel_id, channel)).first);
    }

    void add_channel(const dpp::snowflake channel_id) {
        bot.channel_get(channel_id, [&,channel_id](dpp::confirmation_callback_t e) {
            if (e.is_error()) return;

            add_channel(channel_id, std::get<dpp::channel>(e.value));
        });
    }

    void add_guild(const dpp::snowflake guild_id, dpp::guild &guild) {
        guild_added(*guilds.emplace(std::make_pair(guild_id, guild)).first);
    }

    void add_guild(std::pair<const dpp::snowflake, dpp::guild> &pair) {
        add_guild(pair.first, pair.second);
    }

    void add_guild(dpp::snowflake guild_id) {
        bot.guild_get(guild_id, [&,guild_id](dpp::confirmation_callback_t e) {
            if (e.is_error()) return;

            add_guild(guild_id, std::get<dpp::guild>(e.value));
        });
    }

    void message_create(const dpp::message &m) {
        logs(m.content);
        bot.message_create(m, complete_handler);
    }

    int handle_apierror(const dpp::error_info &e) {
        int sum = log("Error in confirmation\nError brief: %s\nError body: %s\n", e.message.c_str(), e.human_readable.c_str());
        for (const auto &s : e.errors)
            sum += logs(s.reason);
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
    auto &bot = prog.bot;

    bot.on_guild_member_add([&](const guild_member_add_t &guild_member_add) {
        auto &guild = guild_member_add.adding_guild;
        auto &user = guild_member_add.added;
        auto &channel_id = guild.system_channel_id;
        prog.message_create(prog.create_welcome_message(user.get_mention(), channel_id));
    });

    bot.on_message_create([&](const message_create_t &message_create) {
        auto &msg = message_create.msg;
        auto &user = msg.author;

        prog.logs(message_create.msg);
        if (message_create.msg.content == "devtest")
            prog.message_create(prog.create_welcome_message(message_create.msg.author.get_mention(), message_create.msg.channel_id));
    });

    bot.on_button_click([&](const button_click_t &button_click) {
        auto &id = button_click.custom_id;
        auto &command = button_click.command;

        if (!command.is_guild_interaction())
            return;

        auto &user = button_click.command.member;
        auto user_id = user.user_id;
        auto guild_id = user.guild_id;

        prog.log("Button clicked: %s by %lu\n", id.c_str(), user_id);

        if (id == "verify_button") {
            prog.add_or_create_role(guild_id, user_id, "Verified");
            button_click.reply(ir_update_message, std::format("You are now verified {}!", user.get_mention()));
        }
    });

    return prog.run();
}