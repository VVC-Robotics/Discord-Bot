#include <dpp/dpp.h>
#include <cstdlib>
#include <string>
#include <string.h>
#include <map>
#include <vector>
#include <functional>
#include <algorithm>
#include <stdio.h>
#include <fstream>

struct Guild : public dpp::guild {
    Guild() {}
    Guild(const dpp::guild &base):dpp::guild(base) {}
};

struct Program {
    dpp::cluster bot;
    std::map<dpp::snowflake, Guild> guilds;
    dpp::command_completion_event_t complete_handler;

    Program() {
        complete_handler = std::bind(&Program::handle_confirm, this, std::placeholders::_1);
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

    virtual void guild_added(std::pair<dpp::snowflake, Guild&> pair) {

    }

    void add_guild(dpp::snowflake guild_id) {
        bot.guild_get(guild_id, [&](dpp::confirmation_callback_t e) {
            if (e.is_error()) return;

            dpp::guild &v = std::get<dpp::guild>(e.value);

            if (guilds.contains(v.id)) return;
            
            guilds[v.id] = Guild(v);

            guild_added({v.id, guilds[v.id]});
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
    
    auto token = prog.load_token();
    if (token.size() < 1) prog.handle_error("No token in TOKEN file");
    //prog.log("Token: %s\n", token.c_str());

    new (&prog.bot) cluster(token, i_guilds | i_default_intents | i_guild_members | i_message_content);
    auto &bot = prog.bot;
    
    prog.logs("Starting bot");

    bot.on_ready([&](auto event){ prog.logs("Bot started"); });

    //bot.set_presence(presence(presence_status::ps_online, dpp::activity(activity_type::at_watching, "over this server")))

    snowflake verification_channel = 1351751824587100270, bot_id = 770713966636695602, role_id = 1351755417323044948;

    bot.on_guild_member_add([&](const guild_member_add_t &guild_member_add) {
        auto &guild = guild_member_add.adding_guild;
        auto &user = guild_member_add.added;
        prog.create_welcome_message(user.get_mention(), verification_channel);
    });

    bot.on_message_create([&](const message_create_t &message_create) {
        auto &msg = message_create.msg;
        auto &user = msg.author;

        prog.logs(message_create.msg);
        if (message_create.msg.content == "devtest")
            prog.message_create(prog.create_welcome_message(message_create.msg.author.get_mention(), message_create.msg.channel_id));
            //message_create.reply(prog.create_welcome_message(user.get_mention(), msg.channel_id), true, prog.complete_handler);
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

        if (id == "verify_button")
            prog.add_or_create_role(guild_id, user_id, "Verified");

    });

    bot.start(dpp::st_wait);
    return 0;
}