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
        bot.message_create(m, [&](dpp::confirmation_callback_t e) {
            if (e.is_error())
                logs(e.get_error().message);
        });
    }
};

int main() {
    using namespace dpp;
    Program prog;
    
    auto token = prog.load_token();
    if (token.size() < 1) prog.handle_error("No token in TOKEN file");
    //prog.log("Token: %s\n", token.c_str());

    new (&prog.bot) cluster(token, i_guilds | i_default_intents | i_guild_members);
    auto &bot = prog.bot;
    
    prog.logs("Starting bot");

    bot.on_ready([&](auto event){ prog.logs("Bot started"); });

    bot.on_socket_close([&](auto event) { prog.logs("Bot stopping"); });

    //bot.set_presence(presence(presence_status::ps_online, dpp::activity(activity_type::at_watching, "over this server")))

    snowflake verification_channel = 1351751824587100270;

    bot.on_guild_member_add([&](const guild_member_add_t &guild_member_add) {
        auto &guild = guild_member_add.adding_guild;
        auto &user = guild_member_add.added;
        std::string str = "Welcome " + user.get_mention() + "!\n\nReact to the checkbox to become verified";
        prog.logs(str);
        auto m = message(guild.system_channel_id, str);
        //m.set_flags(m_ephemeral);
        m.add_component(component().set_emoji("ballot_box_with_check"));

        auto channel_id = guild.system_channel_id;
        channel_id = verification_channel; // change!

        prog.message_create(m);
    });

    bot.on_message_reaction_add([&](const message_reaction_add_t &message_reaction_add) {
        auto &channel = message_reaction_add.channel_id;
    });

    bot.start(dpp::st_wait);
    return 0;
}