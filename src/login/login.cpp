﻿/*
===========================================================================

  Copyright (c) 2010-2016 Darkstar Dev Teams

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses/

===========================================================================
*/
#include "../common/mmo.h"
#include "../common/logging.h"
#include "../common/timer.h"
#include "../common/utils.h"
#include "../common/version.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef WIN32
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#endif

#include "lobby.h"
#include "login.h"
#include "login_auth.h"
#include "message_server.h"

const char* LOGIN_CONF_FILENAME   = nullptr;
const char* VERSION_INFO_FILENAME = nullptr;
const char* MAINT_CONF_FILENAME   = nullptr;

volatile bool consoleThreadRun = true;

login_config_t login_config; // main settings
version_info_t version_info;
maint_config_t maint_config;

Sql_t*      SqlHandle = nullptr;
std::thread messageThread;
std::thread consoleInputThread;

int32 do_init(int32 argc, char** argv)
{
    int32 i;
    LOGIN_CONF_FILENAME   = "conf/login.conf";
    VERSION_INFO_FILENAME = "conf/version.conf";
    MAINT_CONF_FILENAME   = "conf/maint.conf";

    // srand(gettick());

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "--h") == 0 || strcmp(argv[i], "--?") == 0 || strcmp(argv[i], "/?") == 0)
        {
            login_helpscreen(1);
        }
        else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "--v") == 0 || strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "/v") == 0)
        {
            login_versionscreen(1);
        }
        else if (strcmp(argv[i], "--login_config") == 0 || strcmp(argv[i], "--login-config") == 0)
        {
            LOGIN_CONF_FILENAME = argv[i + 1];
        }
        else if (strcmp(argv[i], "--run_once") == 0)
        { // close the map-server as soon as its done.. for testing [Celest]
            runflag = 0;
        }
    }

    login_config_default();
    config_read(LOGIN_CONF_FILENAME, "login", login_config_read);
    login_config_read_from_env();

    version_info_default();
    config_read(VERSION_INFO_FILENAME, "version info", version_info_read);

    maint_config_default();
    config_read(MAINT_CONF_FILENAME, "maint", maint_config_read);

    login_fd = makeListenBind_tcp(login_config.login_auth_ip.c_str(), login_config.login_auth_port, connect_client_login);
    ShowStatus("The login-server-auth is ready (Server is listening on the port %u).", login_config.login_auth_port);

    login_lobbydata_fd = makeListenBind_tcp(login_config.login_data_ip.c_str(), login_config.login_data_port, connect_client_lobbydata);
    ShowStatus("The login-server-lobbydata is ready (Server is listening on the port %u).", login_config.login_data_port);

    login_lobbyview_fd = makeListenBind_tcp(login_config.login_view_ip.c_str(), login_config.login_view_port, connect_client_lobbyview);
    ShowStatus("The login-server-lobbyview is ready (Server is listening on the port %u).", login_config.login_view_port);

    SqlHandle = Sql_Malloc();
    if (Sql_Connect(SqlHandle, login_config.mysql_login.c_str(), login_config.mysql_password.c_str(), login_config.mysql_host.c_str(), login_config.mysql_port,
                    login_config.mysql_database.c_str()) == SQL_ERROR)
    {
        exit(EXIT_FAILURE);
    }
    Sql_Keepalive(SqlHandle);

    const char* fmtQuery = "OPTIMIZE TABLE `accounts`,`accounts_banned`, `accounts_sessions`, `chars`,`char_equip`, \
                           `char_inventory`, `char_jobs`,`char_look`,`char_stats`, `char_vars`, `char_bazaar_msg`, \
                           `char_skills`, `char_titles`, `char_effects`, `char_exp`;";

    if (Sql_Query(SqlHandle, fmtQuery) == SQL_ERROR)
    {
        ShowError("do_init: Impossible to optimise tables");
    }

    ShowStatus("The login-server is ready to work...");
    messageThread = std::thread(message_server_init);

    if (!login_config.account_creation)
    {
        ShowStatus("New account creation is disabled in login_config.");
    }

    bool attached = isatty(0);

    if (attached)
    {
        consoleInputThread = std::thread([&]() {
            ShowStatus("Console input thread is ready..\r");
            // ctrl c apparently causes log spam
            auto lastInputTime = server_clock::now();
            while (consoleThreadRun)
            {
                if ((server_clock::now() - lastInputTime) > 1s)
                {
                    std::string line;
                    std::getline(std::cin, line);
                    std::istringstream       stream(line);
                    std::string              input;
                    std::vector<std::string> inputs;
                    while (stream >> input)
                    {
                        inputs.push_back(input);
                    }

                    if (!inputs.empty())
                    {
                        if (inputs[0] == "verlock")
                        {
                            // handle wrap around from 2->3 as 0
                            auto temp             = (++version_info.ver_lock) % 3;
                            version_info.ver_lock = temp;

                            const char* value = "";
                            switch (version_info.ver_lock)
                            {
                                case 0:
                                    value = "disabled";
                                    break;
                                case 1:
                                    value = "enabled - strict";
                                    break;
                                case 2:
                                    value = "enabled - greater than or equal";
                                    break;
                            }
                            ShowStatus("Version lock %i - %s\r", version_info.ver_lock, value);
                        }
                        else if (inputs[0] == "maint_mode")
                        {
                            if (inputs.size() >= 2)
                            {
                                uint8 mode = 0;
                                try
                                {
                                    mode = std::stoi(inputs[1]);
                                }
                                catch (...)
                                {
                                    // set to invalid if string -> uint8 fails
                                    mode = 0;
                                }

                                if (mode < 0 || mode > 2)
                                {
                                    ShowStatus("Maintenance mode %i not supported\r", maint_config.maint_mode);
                                }
                                else
                                {
                                    maint_config.maint_mode = mode;
                                    config_write(MAINT_CONF_FILENAME, "maint", maint_config_write);

                                    ShowStatus("Maintenance mode changed to %i\r", maint_config.maint_mode);
                                }
                            }
                            else
                            {
                                ShowStatus("Maintenance mode requires 1 argument (mode - 0-1)\r");
                            }
                        }
                        else
                        {
                            ShowStatus("Unknown console input command\r");
                        }
                    }

                    lastInputTime = server_clock::now();
                }
            };
            ShowStatus("Console input thread exiting..\r");
        });
    }
    return 0;
}

void do_final(int code)
{
    consoleThreadRun = false;
    message_server_close();
    if (messageThread.joinable())
    {
        messageThread.join();
    }
    if (consoleInputThread.joinable())
    {
        consoleInputThread.join();
    }
    if (SqlHandle)
    {
        Sql_Free(SqlHandle);
        SqlHandle = nullptr;
    }

    timer_final();
    socket_final();

    exit(code);
}

void do_abort()
{
    do_final(EXIT_FAILURE);
}
void set_server_type()
{
    SERVER_TYPE = XI_SERVER_LOGIN;
    SOCKET_TYPE = socket_type::TCP;
}

int do_sockets(fd_set* rfd, duration next)
{
    struct timeval timeout;
    int            ret;
    int            i;

    // can timeout until the next tick
    timeout.tv_sec  = (long)std::chrono::duration_cast<std::chrono::seconds>(next).count();
    timeout.tv_usec = (long)std::chrono::duration_cast<std::chrono::microseconds>(next - std::chrono::duration_cast<std::chrono::seconds>(next)).count();

    memcpy(rfd, &readfds, sizeof(*rfd));
    ret = sSelect(fd_max, rfd, nullptr, nullptr, &timeout);

    if (ret == SOCKET_ERROR)
    {
        if (sErrno != S_EINTR)
        {
            ShowFatalError("do_sockets: select() failed, error code %d!", sErrno);
            exit(EXIT_FAILURE);
        }
        return 0; // interrupted by a signal, just loop and try again
    }

    last_tick = time(nullptr);

#if defined(WIN32)
    // on windows, enumerating all members of the fd_set is way faster if we access the internals
    for (i = 0; i < (int)rfd->fd_count; ++i)
    {
        int fd = sock2fd(rfd->fd_array[i]);
        if (session[fd])
        {
            session[fd]->func_recv(fd);

            if (fd != login_fd && fd != login_lobbydata_fd && fd != login_lobbyview_fd)
            {
                session[fd]->func_parse(fd);

                if (!session[fd])
                    continue;

                //              RFIFOFLUSH(fd);
            }
        }
    }
#else
    // otherwise assume that the fd_set is a bit-array and enumerate it in a standard way
    for (i = 1; ret && i < fd_max; ++i)
    {
        if (sFD_ISSET(i, rfd) && session[i])
        {
            session[i]->func_recv(i);

            if (session[i])
            {
                if (i != login_fd && i != login_lobbydata_fd && i != login_lobbyview_fd)
                {
                    session[i]->func_parse(i);

                    if (!session[i])
                    {
                        continue;
                    }

                    //                          RFIFOFLUSH(fd);
                }
                --ret;
            }
        }
    }
#endif

    /*
        // parse input data on each socket
    for(i = 1; i < fd_max; i++)
    {
        if(!session[i])
            continue;

        if (session[i]->rdata_tick && DIFF_TICK(last_tick, session[i]->rdata_tick) > stall_time) {
            ShowInfo("Session #%d timed out", i);
            set_eof(i);
        }

        session[i]->func_parse(i);

        if(!session[i])
            continue;

        // after parse, check client's RFIFO size to know if there is an invalid packet (too big and not parsed)
        if (session[i]->rdata_size == RFIFO_SIZE && session[i]->max_rdata == RFIFO_SIZE) {
            set_eof(i);
            continue;
        }
        RFIFOFLUSH(i);
    }*/

    for (i = 1; i < fd_max; i++)
    {
        if (!session[i])
        {
            continue;
        }

        if (!session[i]->wdata.empty())
        {
            session[i]->func_send(i);
        }
    }
    return 0;
}

int parse_console(char* buf)
{
    return 0;
}

void login_config_read(const char* key, const char* value)
{
    int stdout_with_ansisequence = 0;
    int msg_silent               = 0; // Specifies how silent the console is.
    char timestamp_format[20]     = "[%d/%b] [%H:%M:%S]"; // For displaying Timestamps, default value

    if (strcmpi(key, "timestamp_format") == 0)
    {
        strncpy(timestamp_format, value, 19);
    }
    else if (strcmpi(key, "stdout_with_ansisequence") == 0)
    {
        stdout_with_ansisequence = config_switch(value);
    }
    else if (strcmpi(key, "console_silent") == 0)
    {
        ShowInfo("Console Silent Setting: %d", atoi(value));
        msg_silent = atoi(value);
    }
    else if (strcmp(key, "login_data_ip") == 0)
    {
        login_config.login_data_ip = std::string(value);
    }
    else if (strcmp(key, "login_data_port") == 0)
    {
        login_config.login_data_port = atoi(value);
    }
    else if (strcmp(key, "login_view_ip") == 0)
    {
        login_config.login_view_ip = std::string(value);
    }
    else if (strcmp(key, "login_view_port") == 0)
    {
        login_config.login_view_port = atoi(value);
    }
    else if (strcmp(key, "login_auth_ip") == 0)
    {
        login_config.login_auth_ip = std::string(value);
    }
    else if (strcmp(key, "login_auth_port") == 0)
    {
        login_config.login_auth_port = atoi(value);
    }
    else if (strcmp(key, "mysql_host") == 0)
    {
        login_config.mysql_host = std::string(value);
    }
    else if (strcmp(key, "mysql_login") == 0)
    {
        login_config.mysql_login = std::string(value);
    }
    else if (strcmp(key, "mysql_password") == 0)
    {
        login_config.mysql_password = std::string(value);
    }
    else if (strcmp(key, "mysql_port") == 0)
    {
        login_config.mysql_port = atoi(value);
    }
    else if (strcmp(key, "mysql_database") == 0)
    {
        login_config.mysql_database = std::string(value);
    }
    else if (strcmp(key, "search_server_port") == 0)
    {
        login_config.search_server_port = atoi(value);
    }
    else if (strcmp(key, "servername") == 0)
    {
        login_config.servername = std::string(value);
    }
    else if (strcmpi(key, "import") == 0)
    {
        config_read(value, "login", login_config_read);
    }
    else if (strcmp(key, "msg_server_port") == 0)
    {
        login_config.msg_server_port = atoi(value);
    }
    else if (strcmp(key, "msg_server_ip") == 0)
    {
        login_config.msg_server_ip = std::string(value);
    }
    else if (strcmp(key, "log_user_ip") == 0)
    {
        login_config.log_user_ip = config_switch(value);
    }
    else if (strcmp(key, "account_creation") == 0)
    {
        login_config.account_creation = config_switch(value);
    }
    else
    {
        ShowWarning("Unknown setting '%s' with value '%s' in  login file", key, value);
    }
}

void version_info_read(const char* key, const char* value)
{
    if (strcmp(key, "CLIENT_VER") == 0)
    {
        version_info.client_ver = std::string(value);
    }
    else if (strcmp(key, "VER_LOCK") == 0)
    {
        version_info.ver_lock = std::atoi(value);

        if (version_info.ver_lock > 2 || version_info.ver_lock < 0)
        {
            ShowError("ver_lock not within bounds (0..2) was %i, defaulting to 1\r", version_info.ver_lock);
            version_info.ver_lock = 1;
        }
    }
    else
    {
        ShowWarning("Unknown setting '%s' with value '%s' in  version info file", key, value);
    }
}

void login_config_default()
{
    login_config.login_data_ip   = "127.0.0.1";
    login_config.login_data_port = 54230;
    login_config.login_view_ip   = "127.0.0.1";
    login_config.login_view_port = 54001;
    login_config.login_auth_ip   = "127.0.0.1";
    login_config.login_auth_port = 54231;

    login_config.servername = "Nameless";

    login_config.mysql_host     = "";
    login_config.mysql_login    = "";
    login_config.mysql_password = "";
    login_config.mysql_database = "";
    login_config.mysql_port     = 3306;

    login_config.search_server_port = 54002;
    login_config.msg_server_port    = 54003;
    login_config.msg_server_ip      = "127.0.0.1";

    login_config.log_user_ip      = "false";
    login_config.account_creation = "true";
}

void login_config_read_from_env()
{
    login_config.mysql_login     = std::getenv("XI_DB_USER") ? std::getenv("XI_DB_USER") : login_config.mysql_login;
    login_config.mysql_password  = std::getenv("XI_DB_USER_PASSWD") ? std::getenv("XI_DB_USER_PASSWD") : login_config.mysql_password;
    login_config.mysql_host      = std::getenv("XI_DB_HOST") ? std::getenv("XI_DB_HOST") : login_config.mysql_host;
    login_config.mysql_port      = std::getenv("XI_DB_PORT") ? std::stoi(std::getenv("XI_DB_PORT")) : login_config.mysql_port;
    login_config.mysql_database  = std::getenv("XI_DB_NAME") ? std::getenv("XI_DB_NAME") : login_config.mysql_database;
    login_config.msg_server_ip   = std::getenv("XI_MSG_IP") ? std::getenv("XI_MSG_IP") : login_config.msg_server_ip;
    login_config.msg_server_port = std::getenv("XI_MSG_PORT") ? std::stoi(std::getenv("XI_MSG_PORT")) : login_config.msg_server_port;
}

void version_info_default()
{
    version_info.client_ver = "99999999_9"; // xxYYMMDD_m = xx:MajorRelease YY:year MM:month DD:day _m:MinorRelease
    version_info.ver_lock   = 2;
}

void maint_config_read(const char* key, const char* value)
{
    if (strcmp(key, "MAINT_MODE") == 0)
    {
        maint_config.maint_mode = std::atoi(value);

        if (maint_config.maint_mode > 2 || maint_config.maint_mode < 0)
        {
            ShowError("maint_mode not within bounds (0..1) was %i, defaulting to 0\r", maint_config.maint_mode);
            maint_config.maint_mode = 0;
        }
    }
    else
    {
        ShowWarning("Unknown setting '%s' with value '%s' in  maint info file", key, value);
    }
}

void maint_config_default()
{
    maint_config.maint_mode = 0;
}

std::string maint_config_write(const char* key)
{
    if (strcmp(key, "MAINT_MODE") == 0)
    {
        return std::to_string(maint_config.maint_mode);
    }

    ShowWarning("Did not find value for setting '%s'", key);

    return std::string();
}

int32 config_read(const char* fileName, const char* config, const std::function<void(const char*, const char*)>& method)
{
    char  line[1024];
    char  key[1024];
    char  value[1024];
    FILE* fp;

    fp = fopen(fileName, "r");
    if (fp == nullptr)
    {
        ShowError("%s configuration file not found at: %s", config, fileName);
        return 1;
    }

    while (fgets(line, sizeof(line), fp))
    {
        char* ptr;

        if (line[0] == '#')
        {
            continue;
        }
        if (sscanf(line, "%[^:]: %[^\t\r\n]", key, value) < 2)
        {
            continue;
        }

        // Strip trailing spaces
        ptr = value + strlen(value);
        while (--ptr >= value && *ptr == ' ')
        {
            ;
        }
        ptr++;
        *ptr = '\0';

        method(key, value);
    }

    fclose(fp);
    return 0;
}

int32 config_write(const char* fileName, const char* config, const std::function<std::string(const char*)>& method)
{
    char                     line[1024];
    char                     key[1024];
    char                     value[1024];
    std::vector<std::string> lines;
    FILE*                    fp;

    fp = fopen(fileName, "r");
    if (fp == nullptr)
    {
        ShowError("%s configuration file not found at: %s", config, fileName);
        return 1;
    }

    while (fgets(line, sizeof(line), fp))
    {
        char* ptr;

        if (line[0] == '#' || sscanf(line, "%[^:]: %[^\t\r\n]", key, value) < 2)
        {
            lines.emplace_back(line);
            continue;
        }

        // Strip trailing spaces
        ptr = value + strlen(value);
        while (--ptr >= value && *ptr == ' ')
        {
            ;
        }
        ptr++;
        *ptr = '\0';

        auto replace  = method(key);
        auto new_line = std::string(key);
        new_line.append(": ");
        new_line.append(replace);
        lines.push_back(new_line);
    }
    fclose(fp);

    fp = fopen(fileName, "w");
    if (fp == nullptr)
    {
        ShowError("%s configuration file not found at: %s - unable to write changes", config, fileName);
        return 1;
    }

    for (auto& item : lines)
    {
        fputs(item.c_str(), fp);
    }
    fclose(fp);
    return 0;
}

void login_versionscreen(int32 flag)
{
    ShowInfo("Server version %d.%02d.%02d", XI_MAJOR_VERSION, XI_MINOR_VERSION, XI_REVISION);
    ShowInfo("Repository:\thttps://github.com/LandSandBoat/server");
    ShowInfo("Website:\thttps://landsandboat.github.io/server/");
    if (flag)
    {
        exit(EXIT_FAILURE);
    }
}

void login_helpscreen(int32 flag)
{
    ShowMessage("Usage: login-server [options]");
    ShowMessage("Options:");
    ShowMessage("  Commands\t\t\tDescription");
    ShowMessage("-----------------------------------------------------------------------------");
    ShowMessage("  --help, --h, --?, /?     Displays this help screen");
    ShowMessage("  --login-config <file>    Load login-server configuration from <file>");
    ShowMessage("  --lan-config   <file>    Load lan configuration from <file>");
    ShowMessage("  --version, --v, -v, /v   Displays the server's version");
    ShowMessage("");
    if (flag)
    {
        exit(EXIT_FAILURE);
    }
}

void log_init(int argc, char** argv)
{
    std::string logFile;

#ifdef WIN32
    logFile = "log\\login-server.log";
#else
    logFile = "log/login-server.log";
#endif
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--log") == 0)
        {
            logFile = argv[i + 1];
        }
    }
    logging::InitializeLog("login", logFile);
}

///////////////////////////////////////////////////////
////////////////////////////////////////////////////////
