﻿/*
===========================================================================

Copyright (c) 2010-2015 Darkstar Dev Teams

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

#include "../../common/logging.h"
#include "../../common/socket.h"
#include "../../common/sql.h"
#include "../../common/timer.h"
#include "../../common/utils.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../lua/luautils.h"

#include "../ai/ai_container.h"
#include "../ai/states/attack_state.h"
#include "../ai/states/item_state.h"

#include "../packets/char_abilities.h"
#include "../packets/char_appearance.h"
#include "../packets/char_equip.h"
#include "../packets/char_health.h"
#include "../packets/char_job_extra.h"
#include "../packets/char_jobs.h"
#include "../packets/char_recast.h"
#include "../packets/char_skills.h"
#include "../packets/char_stats.h"
#include "../packets/char_sync.h"
#include "../packets/char_update.h"
#include "../packets/chat_message.h"
#include "../packets/conquest_map.h"
#include "../packets/delivery_box.h"
#include "../packets/inventory_assign.h"
#include "../packets/inventory_finish.h"
#include "../packets/inventory_item.h"
#include "../packets/inventory_modify.h"
#include "../packets/key_items.h"
#include "../packets/linkshell_equip.h"
#include "../packets/menu_jobpoints.h"
#include "../packets/menu_merit.h"
#include "../packets/message_basic.h"
#include "../packets/message_combat.h"
#include "../packets/message_special.h"
#include "../packets/message_standard.h"
#include "../packets/quest_mission_log.h"

#include "../packets/roe_sparkupdate.h"
#include "../packets/server_ip.h"
#include "../packets/timer_bar_util.h"

#include "../ability.h"
#include "../alliance.h"
#include "../conquest_system.h"
#include "../grades.h"
#include "../item_container.h"
#include "../latent_effect_container.h"
#include "../linkshell.h"
#include "../map.h"
#include "../mob_modifier.h"
#include "../recast_container.h"
#include "../roe.h"
#include "../spell.h"
#include "../status_effect_container.h"
#include "../trait.h"
#include "../treasure_pool.h"
#include "../unitychat.h"
#include "../universal_container.h"
#include "../vana_time.h"
#include "../weapon_skill.h"

#include "../entities/automatonentity.h"
#include "../entities/charentity.h"
#include "../entities/mobentity.h"
#include "../entities/petentity.h"

#include "battleutils.h"
#include "blueutils.h"
#include "charutils.h"
#include "itemutils.h"
#include "petutils.h"
#include "puppetutils.h"
#include "zoneutils.h"

/************************************************************************
 *                                                                       *
 *  Таблицы получаемого опыта                                            *
 *                                                                       *
 ************************************************************************/

// Number of rows in the exp table
static constexpr int32                               ExpTableRowCount = 60;
std::array<std::array<uint16, 20>, ExpTableRowCount> g_ExpTable;
std::array<uint16, 100>                              g_ExpPerLevel;

/************************************************************************
 *                                                                       *
 *                                                                       *
 *                                                                       *
 ************************************************************************/

namespace charutils
{
    /************************************************************************
     *                                                                       *
     *  Расчет характеристик персонажей                                      *
     *                                                                       *
     ************************************************************************/

    void CalculateStats(CCharEntity* PChar)
    {
        // Объявление переменных, нужных для рассчета.

        float raceStat  = 0; // конечное число HP для уровня на основе расы.
        float jobStat   = 0; // конечное число HP для уровня на основе первичной профессии.
        float sJobStat  = 0; // коенчное число HP для уровня на основе вторичной профессии.
        int32 bonusStat = 0; // бонусное число HP которое добавляется при соблюдении некоторых условий.

        int32 baseValueColumn   = 0; // номер колонки с базовым количеством HP
        int32 scaleTo60Column   = 1; // номер колонки с модификатором до 60 уровня
        int32 scaleOver30Column = 2; // номер колонки с модификатором после 30 уровня
        int32 scaleOver60Column = 3; // номер колонки с модификатором после 60 уровня
        int32 scaleOver75Column = 4; // номер колонки с модификатором после 75 уровня
        int32 scaleOver60       = 2; // номер колонки с модификатором для расчета MP после 60 уровня
        int32 scaleOver75       = 3; // номер колонки с модификатором для расчета Статов после 75-го уровня

        uint8 grade;

        uint8      mlvl        = PChar->GetMLevel();
        uint8      slvl        = PChar->GetSLevel();
        JOBTYPE    mjob        = PChar->GetMJob();
        JOBTYPE    sjob        = PChar->GetSJob();
        MERIT_TYPE statMerit[] = { MERIT_STR, MERIT_DEX, MERIT_VIT, MERIT_AGI, MERIT_INT, MERIT_MND, MERIT_CHR };

        uint8 race = 0; // Human

        switch (PChar->look.race)
        {
            case 3:
            case 4:
                race = 1;
                break; // Elvaan
            case 5:
            case 6:
                race = 2;
                break; // Tarutaru
            case 7:
                race = 3;
                break; // Mithra
            case 8:
                race = 4;
                break; // Galka
        }

        // Расчет прироста HP от main job

        int32 mainLevelOver30     = std::clamp(mlvl - 30, 0, 30); // Расчет условия +1HP каждый лвл после 30 уровня
        int32 mainLevelUpTo60     = (mlvl < 60 ? mlvl - 1 : 59);  // Первый режим рассчета до 60 уровня (Используется так же и для MP)
        int32 mainLevelOver60To75 = std::clamp(mlvl - 60, 0, 15); // Второй режим расчета после 60 уровня
        int32 mainLevelOver75     = (mlvl < 75 ? 0 : mlvl - 75);  // Третий режим расчета после 75 уровня

        //Расчет бонусного количества HP

        int32 mainLevelOver10           = (mlvl < 10 ? 0 : mlvl - 10);  // +2HP на каждом уровне после 10
        int32 mainLevelOver50andUnder60 = std::clamp(mlvl - 50, 0, 10); // +2HP на каждом уровне в промежутке от 50 до 60 уровня
        int32 mainLevelOver60           = (mlvl < 60 ? 0 : mlvl - 60);

        // Расчет прироста HP от дополнительной профессии

        int32 subLevelOver10 = std::clamp(slvl - 10, 0, 20); // +1HP на каждый уровень после 10 (/2)
        int32 subLevelOver30 = (slvl < 30 ? 0 : slvl - 30);  // +1HP на каждый уровень после 30

        // Расчет raceStat jobStat bonusStat sJobStat
        // Расчет по расе

        grade = grade::GetRaceGrades(race, 0);

        raceStat = grade::GetHPScale(grade, baseValueColumn) + (grade::GetHPScale(grade, scaleTo60Column) * mainLevelUpTo60) +
                   (grade::GetHPScale(grade, scaleOver30Column) * mainLevelOver30) + (grade::GetHPScale(grade, scaleOver60Column) * mainLevelOver60To75) +
                   (grade::GetHPScale(grade, scaleOver75Column) * mainLevelOver75);

        // raceStat = (int32)(statScale[grade][baseValueColumn] + statScale[grade][scaleTo60Column] * (mlvl - 1));

        // Расчет по main job
        grade = grade::GetJobGrade(mjob, 0);

        jobStat = grade::GetHPScale(grade, baseValueColumn) + (grade::GetHPScale(grade, scaleTo60Column) * mainLevelUpTo60) +
                  (grade::GetHPScale(grade, scaleOver30Column) * mainLevelOver30) + (grade::GetHPScale(grade, scaleOver60Column) * mainLevelOver60To75) +
                  (grade::GetHPScale(grade, scaleOver75Column) * mainLevelOver75);

        // Расчет бонусных HP
        bonusStat = (mainLevelOver10 + mainLevelOver50andUnder60) * 2;

        // Расчет по support job
        if (slvl > 0)
        {
            grade = grade::GetJobGrade(sjob, 0);

            sJobStat = grade::GetHPScale(grade, baseValueColumn) + (grade::GetHPScale(grade, scaleTo60Column) * (slvl - 1)) +
                       (grade::GetHPScale(grade, scaleOver30Column) * subLevelOver30) + subLevelOver30 + subLevelOver10;
            sJobStat = sJobStat / 2;
        }

        uint16 MeritBonus   = PChar->PMeritPoints->GetMeritValue(MERIT_MAX_HP, PChar);
        PChar->health.maxhp = (int16)(map_config.player_hp_multiplier * (raceStat + jobStat + bonusStat + sJobStat) + MeritBonus);

        //Начало расчера MP

        raceStat = 0;
        jobStat  = 0;
        sJobStat = 0;

        // Расчет MP расе.
        grade = grade::GetRaceGrades(race, 1);

        //Если у main job нет МП рейтинга, расчитиваем расовый бонус на основе уровня subjob уровня(при условии, что у него есть МП рейтинг)
        if (grade::GetJobGrade(mjob, 1) == 0)
        {
            if (grade::GetJobGrade(sjob, 1) != 0 && slvl > 0) // В этом выражении ошибка
            {
                raceStat =
                    (grade::GetMPScale(grade, 0) + grade::GetMPScale(grade, scaleTo60Column) * (slvl - 1)) / map_config.sj_mp_divisor; // Вот здесь ошибка
            }
        }
        else
        {
            //Расчет нормального расового бонуса
            raceStat = grade::GetMPScale(grade, 0) + grade::GetMPScale(grade, scaleTo60Column) * mainLevelUpTo60 +
                       grade::GetMPScale(grade, scaleOver60) * mainLevelOver60;
        }

        //Для главной профессии
        grade = grade::GetJobGrade(mjob, 1);
        if (grade > 0)
        {
            jobStat = grade::GetMPScale(grade, 0) + grade::GetMPScale(grade, scaleTo60Column) * mainLevelUpTo60 +
                      grade::GetMPScale(grade, scaleOver60) * mainLevelOver60;
        }

        //Для дополнительной профессии
        if (slvl > 0)
        {
            grade    = grade::GetJobGrade(sjob, 1);
            sJobStat = (grade::GetMPScale(grade, 0) + grade::GetMPScale(grade, scaleTo60Column) * (slvl - 1)) / map_config.sj_mp_divisor;
        }

        MeritBonus          = PChar->PMeritPoints->GetMeritValue(MERIT_MAX_MP, PChar);
        PChar->health.maxmp = (int16)(map_config.player_mp_multiplier * (raceStat + jobStat + sJobStat) + MeritBonus); // результат расчета MP

        //Начало расчета характеристик

        uint8 counter = 0;

        for (uint8 StatIndex = 2; StatIndex <= 8; ++StatIndex)
        {
            // расчет по расе
            grade    = grade::GetRaceGrades(race, StatIndex);
            raceStat = grade::GetStatScale(grade, 0) + grade::GetStatScale(grade, scaleTo60Column) * mainLevelUpTo60;

            if (mainLevelOver60 > 0)
            {
                raceStat += grade::GetStatScale(grade, scaleOver60) * mainLevelOver60;

                if (mainLevelOver75 > 0)
                {
                    raceStat += grade::GetStatScale(grade, scaleOver75) * mainLevelOver75 - (mlvl >= 75 ? 0.01f : 0);
                }
            }

            // расчет по профессии
            grade   = grade::GetJobGrade(mjob, StatIndex);
            jobStat = grade::GetStatScale(grade, 0) + grade::GetStatScale(grade, scaleTo60Column) * mainLevelUpTo60;

            if (mainLevelOver60 > 0)
            {
                jobStat += grade::GetStatScale(grade, scaleOver60) * mainLevelOver60;

                if (mainLevelOver75 > 0)
                {
                    jobStat += grade::GetStatScale(grade, scaleOver75) * mainLevelOver75 - (mlvl >= 75 ? 0.01f : 0);
                }
            }

            // расчет по дополнительной профессии
            if (slvl > 0)
            {
                grade    = grade::GetJobGrade(sjob, StatIndex);
                sJobStat = (grade::GetStatScale(grade, 0) + grade::GetStatScale(grade, scaleTo60Column) * (slvl - 1)) / 2;
            }
            else
            {
                sJobStat = 0;
            }

            // get each merit bonus stat, str,dex,vit and so on...
            MeritBonus = PChar->PMeritPoints->GetMeritValue(statMerit[StatIndex - 2], PChar);

            // Вывод значения
            ref<uint16>(&PChar->stats, counter) = (uint16)(map_config.player_stat_multiplier * (raceStat + jobStat + sJobStat) + MeritBonus);
            counter += 2;
        }
    }

    /************************************************************************
     *                                                                       *
     *  Предварительная версия загрузки персонажа. Функция будет             *
     *  оптимизирована после определения всех необходимых данных и таблиц    *
     *                                                                       *
     ************************************************************************/

    void LoadChar(CCharEntity* PChar)
    {
        uint8  meritPoints = 0;
        uint16 limitPoints = 0;
        int32  HP          = 0;
        int32  MP          = 0;

        const char* fmtQuery = "SELECT "
                               "charname,"                     //  0
                               "pos_zone,"                     //  1
                               "pos_prevzone,"                 //  2
                               "pos_rot,"                      //  3
                               "pos_x,"                        //  4
                               "pos_y,"                        //  5
                               "pos_z,"                        //  6
                               "moghouse,"                     //  7
                               "boundary,"                     //  8
                               "home_zone,"                    //  9
                               "home_rot,"                     // 10
                               "home_x,"                       // 11
                               "home_y,"                       // 12
                               "home_z,"                       // 13
                               "nation,"                       // 14
                               "quests,"                       // 15
                               "keyitems,"                     // 16
                               "abilities,"                    // 17
                               "weaponskills,"                 // 18
                               "titles,"                       // 19
                               "zones,"                        // 20
                               "missions,"                     // 21
                               "assault,"                      // 22
                               "campaign,"                     // 23
                               "eminence,"                     // 24
                               "playtime,"                     // 25
                               "campaign_allegiance,"          // 26
                               "isstylelocked,"                // 27
                               "moghancement,"                 // 28
                               "UNIX_TIMESTAMP(`lastupdate`)," // 29
                               "languages "                    // 30
                               "FROM chars "
                               "WHERE charid = %u";

        int32 ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->targid = 0x400;
            PChar->SetName(Sql_GetData(SqlHandle, 0));

            PChar->loc.destination = (uint16)Sql_GetIntData(SqlHandle, 1);
            PChar->loc.prevzone    = (uint16)Sql_GetIntData(SqlHandle, 2);
            PChar->loc.p.rotation  = (uint8)Sql_GetIntData(SqlHandle, 3);
            PChar->loc.p.x         = Sql_GetFloatData(SqlHandle, 4);
            PChar->loc.p.y         = Sql_GetFloatData(SqlHandle, 5);
            PChar->loc.p.z         = Sql_GetFloatData(SqlHandle, 6);
            PChar->m_moghouseID    = Sql_GetIntData(SqlHandle, 7);
            PChar->loc.boundary    = (uint16)Sql_GetIntData(SqlHandle, 8);

            PChar->profile.home_point.destination = (uint16)Sql_GetIntData(SqlHandle, 9);
            PChar->profile.home_point.p.rotation  = (uint8)Sql_GetIntData(SqlHandle, 10);
            PChar->profile.home_point.p.x         = Sql_GetFloatData(SqlHandle, 11);
            PChar->profile.home_point.p.y         = Sql_GetFloatData(SqlHandle, 12);
            PChar->profile.home_point.p.z         = Sql_GetFloatData(SqlHandle, 13);

            PChar->profile.nation = (uint8)Sql_GetIntData(SqlHandle, 14);

            size_t length = 0;
            char*  quests = nullptr;
            Sql_GetData(SqlHandle, 15, &quests, &length);
            memcpy(PChar->m_questLog, quests, (length > sizeof(PChar->m_questLog) ? sizeof(PChar->m_questLog) : length));

            length         = 0;
            char* keyitems = nullptr;
            Sql_GetData(SqlHandle, 16, &keyitems, &length);
            memcpy((void*)&PChar->keys, keyitems, (length > sizeof(PChar->keys) ? sizeof(PChar->keys) : length));

            length          = 0;
            char* abilities = nullptr;
            Sql_GetData(SqlHandle, 17, &abilities, &length);
            memcpy(PChar->m_LearnedAbilities, abilities, (length > sizeof(PChar->m_LearnedAbilities) ? sizeof(PChar->m_LearnedAbilities) : length));

            length             = 0;
            char* weaponskills = nullptr;
            Sql_GetData(SqlHandle, 18, &weaponskills, &length);
            memcpy(&PChar->m_LearnedWeaponskills, weaponskills,
                   (length > sizeof(PChar->m_LearnedWeaponskills) ? sizeof(PChar->m_LearnedWeaponskills) : length));

            length       = 0;
            char* titles = nullptr;
            Sql_GetData(SqlHandle, 19, &titles, &length);
            memcpy(PChar->m_TitleList, titles, (length > sizeof(PChar->m_TitleList) ? sizeof(PChar->m_TitleList) : length));

            length      = 0;
            char* zones = nullptr;
            Sql_GetData(SqlHandle, 20, &zones, &length);
            memcpy(PChar->m_ZonesList, zones, (length > sizeof(PChar->m_ZonesList) ? sizeof(PChar->m_ZonesList) : length));

            length         = 0;
            char* missions = nullptr;
            Sql_GetData(SqlHandle, 21, &missions, &length);
            memcpy(PChar->m_missionLog, missions, (length > sizeof(PChar->m_missionLog) ? sizeof(PChar->m_missionLog) : length));

            length        = 0;
            char* assault = nullptr;
            Sql_GetData(SqlHandle, 22, &assault, &length);
            memcpy(&PChar->m_assaultLog, assault, (length > sizeof(PChar->m_assaultLog) ? sizeof(PChar->m_assaultLog) : length));

            length         = 0;
            char* campaign = nullptr;
            Sql_GetData(SqlHandle, 23, &campaign, &length);
            memcpy(&PChar->m_campaignLog, campaign, (length > sizeof(PChar->m_campaignLog) ? sizeof(PChar->m_campaignLog) : length));

            length         = 0;
            char* eminence = nullptr;
            Sql_GetData(SqlHandle, 24, &eminence, &length);
            memcpy(&PChar->m_eminenceLog, eminence, (length > sizeof(PChar->m_eminenceLog) ? sizeof(PChar->m_eminenceLog) : length));

            PChar->SetPlayTime(Sql_GetUIntData(SqlHandle, 25));
            PChar->profile.campaign_allegiance = (uint8)Sql_GetIntData(SqlHandle, 26);
            PChar->setStyleLocked(Sql_GetIntData(SqlHandle, 27) == 1);
            PChar->SetMoghancement(Sql_GetUIntData(SqlHandle, 28));
            PChar->lastOnline = Sql_GetUIntData(SqlHandle, 29);
            PChar->search.language = (uint8)Sql_GetUIntData(SqlHandle, 30);
        }

        LoadSpells(PChar);

        fmtQuery = "SELECT "
                   "rank_points,"          // 0
                   "rank_sandoria,"        // 1
                   "rank_bastok,"          // 2
                   "rank_windurst,"        // 3
                   "fame_sandoria,"        // 4
                   "fame_bastok,"          // 5
                   "fame_windurst,"        // 6
                   "fame_norg, "           // 7
                   "fame_jeuno, "          // 8
                   "fame_aby_konschtat, "  // 9
                   "fame_aby_tahrongi, "   // 10
                   "fame_aby_latheine, "   // 11
                   "fame_aby_misareaux, "  // 12
                   "fame_aby_vunkerl, "    // 13
                   "fame_aby_attohwa, "    // 14
                   "fame_aby_altepa, "     // 15
                   "fame_aby_grauberg, "   // 16
                   "fame_aby_uleguerand, " // 17
                   "fame_adoulin,"         // 18
                   "unity_leader "         // 19
                   "FROM char_profile "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->profile.rankpoints = Sql_GetUIntData(SqlHandle, 0);

            PChar->profile.rank[0] = (uint8)Sql_GetIntData(SqlHandle, 1);
            PChar->profile.rank[1] = (uint8)Sql_GetIntData(SqlHandle, 2);
            PChar->profile.rank[2] = (uint8)Sql_GetIntData(SqlHandle, 3);

            PChar->profile.fame[0]      = (uint16)Sql_GetIntData(SqlHandle, 4);  // Sandoria
            PChar->profile.fame[1]      = (uint16)Sql_GetIntData(SqlHandle, 5);  // Bastok
            PChar->profile.fame[2]      = (uint16)Sql_GetIntData(SqlHandle, 6);  // Windurst
            PChar->profile.fame[3]      = (uint16)Sql_GetIntData(SqlHandle, 7);  // Norg
            PChar->profile.fame[4]      = (uint16)Sql_GetIntData(SqlHandle, 8);  // Jeuno
            PChar->profile.fame[5]      = (uint16)Sql_GetIntData(SqlHandle, 9);  // AbysseaKonschtat
            PChar->profile.fame[6]      = (uint16)Sql_GetIntData(SqlHandle, 10); // AbysseaTahrongi
            PChar->profile.fame[7]      = (uint16)Sql_GetIntData(SqlHandle, 11); // AbysseaLaTheine
            PChar->profile.fame[8]      = (uint16)Sql_GetIntData(SqlHandle, 12); // AbysseaMisareaux
            PChar->profile.fame[9]      = (uint16)Sql_GetIntData(SqlHandle, 13); // AbysseaVunkerl
            PChar->profile.fame[10]     = (uint16)Sql_GetIntData(SqlHandle, 14); // AbysseaAttohwa
            PChar->profile.fame[11]     = (uint16)Sql_GetIntData(SqlHandle, 15); // AbysseaAltepa
            PChar->profile.fame[12]     = (uint16)Sql_GetIntData(SqlHandle, 16); // AbysseaGrauberg
            PChar->profile.fame[13]     = (uint16)Sql_GetIntData(SqlHandle, 17); // AbysseaUleguerand
            PChar->profile.fame[14]     = (uint16)Sql_GetIntData(SqlHandle, 18); // Adoulin
            PChar->profile.unity_leader = (uint8)Sql_GetUIntData(SqlHandle, 19);
        }

        roeutils::onCharLoad(PChar);

        fmtQuery = "SELECT "
                   "inventory," // 0
                   "safe,"      // 1
                   "locker,"    // 2
                   "satchel,"   // 3
                   "sack,"      // 4
                   "`case`,"    // 5
                   "wardrobe,"  // 6
                   "wardrobe2," // 7
                   "wardrobe3," // 8
                   "wardrobe4 " // 9
                   "FROM char_storage "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->getStorage(LOC_INVENTORY)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 0));
            PChar->getStorage(LOC_MOGSAFE)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 1));
            PChar->getStorage(LOC_MOGSAFE2)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 1));
            PChar->getStorage(LOC_TEMPITEMS)->AddBuff(50);
            PChar->getStorage(LOC_MOGLOCKER)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 2));
            PChar->getStorage(LOC_MOGSATCHEL)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 3));
            PChar->getStorage(LOC_MOGSACK)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 4));
            PChar->getStorage(LOC_MOGCASE)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 5));

            PChar->getStorage(LOC_WARDROBE)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 6));
            PChar->getStorage(LOC_WARDROBE2)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 7));
            PChar->getStorage(LOC_WARDROBE3)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 8));
            PChar->getStorage(LOC_WARDROBE4)->AddBuff((uint8)Sql_GetIntData(SqlHandle, 9));
        }

        fmtQuery = "SELECT face, race, size, head, body, hands, legs, feet, main, sub, ranged "
                   "FROM char_look "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->look.face = (uint8)Sql_GetIntData(SqlHandle, 0);
            PChar->look.race = (uint8)Sql_GetIntData(SqlHandle, 1);
            PChar->look.size = (uint8)Sql_GetIntData(SqlHandle, 2);

            PChar->look.head   = (uint16)Sql_GetIntData(SqlHandle, 3);
            PChar->look.body   = (uint16)Sql_GetIntData(SqlHandle, 4);
            PChar->look.hands  = (uint16)Sql_GetIntData(SqlHandle, 5);
            PChar->look.legs   = (uint16)Sql_GetIntData(SqlHandle, 6);
            PChar->look.feet   = (uint16)Sql_GetIntData(SqlHandle, 7);
            PChar->look.main   = (uint16)Sql_GetIntData(SqlHandle, 8);
            PChar->look.sub    = (uint16)Sql_GetIntData(SqlHandle, 9);
            PChar->look.ranged = (uint16)Sql_GetIntData(SqlHandle, 10);
            memcpy(&PChar->mainlook, &PChar->look, sizeof(PChar->look));
        }

        fmtQuery = "SELECT head, body, hands, legs, feet, main, sub, ranged FROM char_style WHERE charid = %u;";
        ret      = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->styleItems[SLOT_HEAD]   = (uint16)Sql_GetIntData(SqlHandle, 0);
            PChar->styleItems[SLOT_BODY]   = (uint16)Sql_GetIntData(SqlHandle, 1);
            PChar->styleItems[SLOT_HANDS]  = (uint16)Sql_GetIntData(SqlHandle, 2);
            PChar->styleItems[SLOT_LEGS]   = (uint16)Sql_GetIntData(SqlHandle, 3);
            PChar->styleItems[SLOT_FEET]   = (uint16)Sql_GetIntData(SqlHandle, 4);
            PChar->styleItems[SLOT_MAIN]   = (uint16)Sql_GetIntData(SqlHandle, 5);
            PChar->styleItems[SLOT_SUB]    = (uint16)Sql_GetIntData(SqlHandle, 6);
            PChar->styleItems[SLOT_RANGED] = (uint16)Sql_GetIntData(SqlHandle, 7);
        }

        fmtQuery = "SELECT unlocked, genkai, war, mnk, whm, blm, rdm, thf, pld, drk, bst, brd, rng, sam, nin, drg, smn, blu, cor, pup, dnc, sch, geo, run "
                   "FROM char_jobs "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->jobs.unlocked = (uint32)Sql_GetUIntData(SqlHandle, 0);
            PChar->jobs.genkai   = (uint8)Sql_GetUIntData(SqlHandle, 1);

            PChar->jobs.job[JOB_WAR] = (uint8)Sql_GetIntData(SqlHandle, 2);
            PChar->jobs.job[JOB_MNK] = (uint8)Sql_GetIntData(SqlHandle, 3);
            PChar->jobs.job[JOB_WHM] = (uint8)Sql_GetIntData(SqlHandle, 4);
            PChar->jobs.job[JOB_BLM] = (uint8)Sql_GetIntData(SqlHandle, 5);
            PChar->jobs.job[JOB_RDM] = (uint8)Sql_GetIntData(SqlHandle, 6);
            PChar->jobs.job[JOB_THF] = (uint8)Sql_GetIntData(SqlHandle, 7);
            PChar->jobs.job[JOB_PLD] = (uint8)Sql_GetIntData(SqlHandle, 8);
            PChar->jobs.job[JOB_DRK] = (uint8)Sql_GetIntData(SqlHandle, 9);
            PChar->jobs.job[JOB_BST] = (uint8)Sql_GetIntData(SqlHandle, 10);
            PChar->jobs.job[JOB_BRD] = (uint8)Sql_GetIntData(SqlHandle, 11);
            PChar->jobs.job[JOB_RNG] = (uint8)Sql_GetIntData(SqlHandle, 12);
            PChar->jobs.job[JOB_SAM] = (uint8)Sql_GetIntData(SqlHandle, 13);
            PChar->jobs.job[JOB_NIN] = (uint8)Sql_GetIntData(SqlHandle, 14);
            PChar->jobs.job[JOB_DRG] = (uint8)Sql_GetIntData(SqlHandle, 15);
            PChar->jobs.job[JOB_SMN] = (uint8)Sql_GetIntData(SqlHandle, 16);
            PChar->jobs.job[JOB_BLU] = (uint8)Sql_GetIntData(SqlHandle, 17);
            PChar->jobs.job[JOB_COR] = (uint8)Sql_GetIntData(SqlHandle, 18);
            PChar->jobs.job[JOB_PUP] = (uint8)Sql_GetIntData(SqlHandle, 19);
            PChar->jobs.job[JOB_DNC] = (uint8)Sql_GetIntData(SqlHandle, 20);
            PChar->jobs.job[JOB_SCH] = (uint8)Sql_GetIntData(SqlHandle, 21);
            PChar->jobs.job[JOB_GEO] = (uint8)Sql_GetIntData(SqlHandle, 22);
            PChar->jobs.job[JOB_RUN] = (uint8)Sql_GetIntData(SqlHandle, 23);
        }

        fmtQuery = "SELECT mode, war, mnk, whm, blm, rdm, thf, pld, drk, bst, brd, rng, sam, nin, drg, smn, blu, cor, pup, dnc, sch, geo, run, merits, limits "
                   "FROM char_exp "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->MeritMode         = (uint8)Sql_GetIntData(SqlHandle, 0);
            PChar->jobs.exp[JOB_WAR] = (uint16)Sql_GetIntData(SqlHandle, 1);
            PChar->jobs.exp[JOB_MNK] = (uint16)Sql_GetIntData(SqlHandle, 2);
            PChar->jobs.exp[JOB_WHM] = (uint16)Sql_GetIntData(SqlHandle, 3);
            PChar->jobs.exp[JOB_BLM] = (uint16)Sql_GetIntData(SqlHandle, 4);
            PChar->jobs.exp[JOB_RDM] = (uint16)Sql_GetIntData(SqlHandle, 5);
            PChar->jobs.exp[JOB_THF] = (uint16)Sql_GetIntData(SqlHandle, 6);
            PChar->jobs.exp[JOB_PLD] = (uint16)Sql_GetIntData(SqlHandle, 7);
            PChar->jobs.exp[JOB_DRK] = (uint16)Sql_GetIntData(SqlHandle, 8);
            PChar->jobs.exp[JOB_BST] = (uint16)Sql_GetIntData(SqlHandle, 9);
            PChar->jobs.exp[JOB_BRD] = (uint16)Sql_GetIntData(SqlHandle, 10);
            PChar->jobs.exp[JOB_RNG] = (uint16)Sql_GetIntData(SqlHandle, 11);
            PChar->jobs.exp[JOB_SAM] = (uint16)Sql_GetIntData(SqlHandle, 12);
            PChar->jobs.exp[JOB_NIN] = (uint16)Sql_GetIntData(SqlHandle, 13);
            PChar->jobs.exp[JOB_DRG] = (uint16)Sql_GetIntData(SqlHandle, 14);
            PChar->jobs.exp[JOB_SMN] = (uint16)Sql_GetIntData(SqlHandle, 15);
            PChar->jobs.exp[JOB_BLU] = (uint16)Sql_GetIntData(SqlHandle, 16);
            PChar->jobs.exp[JOB_COR] = (uint16)Sql_GetIntData(SqlHandle, 17);
            PChar->jobs.exp[JOB_PUP] = (uint16)Sql_GetIntData(SqlHandle, 18);
            PChar->jobs.exp[JOB_DNC] = (uint16)Sql_GetIntData(SqlHandle, 19);
            PChar->jobs.exp[JOB_SCH] = (uint16)Sql_GetIntData(SqlHandle, 20);
            PChar->jobs.exp[JOB_GEO] = (uint16)Sql_GetIntData(SqlHandle, 21);
            PChar->jobs.exp[JOB_RUN] = (uint16)Sql_GetIntData(SqlHandle, 22);
            meritPoints              = (uint8)Sql_GetIntData(SqlHandle, 23);
            limitPoints              = (uint16)Sql_GetIntData(SqlHandle, 24);
        }

        fmtQuery = "SELECT nameflags, mjob, sjob, hp, mp, mhflag, title, bazaar_message, zoning, "
                   "pet_id, pet_type, pet_hp, pet_mp "
                   "FROM char_stats WHERE charid = %u;";

        ret          = Sql_Query(SqlHandle, fmtQuery, PChar->id);
        uint8 zoning = 0;

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->nameflags.flags = (uint32)Sql_GetUIntData(SqlHandle, 0);

            PChar->SetMJob(Sql_GetUIntData(SqlHandle, 1));
            PChar->SetSJob(Sql_GetUIntData(SqlHandle, 2));

            HP = Sql_GetIntData(SqlHandle, 3);
            MP = Sql_GetIntData(SqlHandle, 4);

            PChar->profile.mhflag = (uint8)Sql_GetIntData(SqlHandle, 5);
            PChar->profile.title  = (uint16)Sql_GetIntData(SqlHandle, 6);

            int8* bazaarMessage = Sql_GetData(SqlHandle, 7);
            if (bazaarMessage != nullptr)
            {
                PChar->bazaar.message.insert(0, (char*)Sql_GetData(SqlHandle, 7));
            }
            else
            {
                PChar->bazaar.message = '\0';
            }

            zoning = Sql_GetUIntData(SqlHandle, 8);

            // Determine if the pet should be respawned.
            int16 petHP = Sql_GetUIntData(SqlHandle, 11);
            if (petHP)
            {
                PChar->petZoningInfo.petHP      = petHP;
                PChar->petZoningInfo.petID      = Sql_GetUIntData(SqlHandle, 9);
                PChar->petZoningInfo.petMP      = Sql_GetIntData(SqlHandle, 12);
                PChar->petZoningInfo.petType    = static_cast<PET_TYPE>(Sql_GetUIntData(SqlHandle, 10));
                PChar->petZoningInfo.respawnPet = true;
            }
        }

        Sql_Query(SqlHandle, "UPDATE char_stats SET zoning = 0 WHERE charid = %u", PChar->id);

        if (zoning == 2)
        {
            ShowDebug("Player <%s> logging in to zone <%u>", PChar->name.c_str(), PChar->getZone());
        }

        PChar->SetMLevel(PChar->jobs.job[PChar->GetMJob()]);
        PChar->SetSLevel(PChar->jobs.job[PChar->GetSJob()]);

        fmtQuery = "SELECT id, time, recast FROM char_recast WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);
        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
        {
            while (Sql_NextRow(SqlHandle) == SQL_SUCCESS)
            {
                uint32    cast_time  = Sql_GetUIntData(SqlHandle, 1);
                uint32    recast     = Sql_GetUIntData(SqlHandle, 2);
                time_t    now        = time(nullptr);
                uint32    chargeTime = 0;
                uint8     maxCharges = 0;
                Charge_t* charge     = ability::GetCharge(PChar, Sql_GetUIntData(SqlHandle, 0));
                if (charge != nullptr)
                {
                    chargeTime = charge->chargeTime;
                    maxCharges = charge->maxCharges;
                }
                if (now < cast_time + recast)
                {
                    PChar->PRecastContainer->Load(RECAST_ABILITY, Sql_GetUIntData(SqlHandle, 0), (cast_time + recast - (uint32)now), chargeTime, maxCharges);
                }
            }
        }

        fmtQuery = "SELECT skillid, value, rank "
                   "FROM char_skills "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
        {
            while (Sql_NextRow(SqlHandle) == SQL_SUCCESS)
            {
                uint8 SkillID = (uint8)Sql_GetUIntData(SqlHandle, 0);

                if (SkillID < MAX_SKILLTYPE)
                {
                    PChar->RealSkills.skill[SkillID] = (uint16)Sql_GetUIntData(SqlHandle, 1);
                    if (SkillID >= SKILL_FISHING)
                    {
                        PChar->RealSkills.rank[SkillID] = (uint8)Sql_GetUIntData(SqlHandle, 2);
                    }
                }
            }
        }

        fmtQuery = "SELECT outpost_sandy, outpost_bastok, outpost_windy, runic_portal, maw, "
                   "campaign_sandy, campaign_bastok, campaign_windy, homepoints, survivals "
                   "FROM char_unlocks "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->teleport.outpostSandy   = Sql_GetUIntData(SqlHandle, 0);
            PChar->teleport.outpostBastok  = Sql_GetUIntData(SqlHandle, 1);
            PChar->teleport.outpostWindy   = Sql_GetUIntData(SqlHandle, 2);
            PChar->teleport.runicPortal    = Sql_GetUIntData(SqlHandle, 3);
            PChar->teleport.pastMaw        = Sql_GetUIntData(SqlHandle, 4);
            PChar->teleport.campaignSandy  = Sql_GetUIntData(SqlHandle, 5);
            PChar->teleport.campaignBastok = Sql_GetUIntData(SqlHandle, 6);
            PChar->teleport.campaignWindy  = Sql_GetUIntData(SqlHandle, 7);

            size_t length = 0;
            char*  buf    = nullptr;
            Sql_GetData(SqlHandle, 8, &buf, &length);
            memcpy(&PChar->teleport.homepoint, buf, (length > sizeof(PChar->teleport.homepoint) ? sizeof(PChar->teleport.homepoint) : length));

            length = 0;
            buf    = nullptr;
            Sql_GetData(SqlHandle, 9, &buf, &length);
            memcpy(&PChar->teleport.survival, buf, (length > sizeof(PChar->teleport.survival) ? sizeof(PChar->teleport.survival) : length));
        }

        PChar->PMeritPoints = new CMeritPoints(PChar);
        PChar->PMeritPoints->SetMeritPoints(meritPoints);
        PChar->PMeritPoints->SetLimitPoints(limitPoints);
        PChar->PJobPoints = new CJobPoints(PChar);

        fmtQuery = "SELECT "
                   "gmlevel, "    // 0
                   "mentor, "     // 1
                   "job_master, " // 2
                   "nnameflags "  // 3
                   "FROM chars "
                   "WHERE charid = %u;";

        ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->m_GMlevel             = (uint8)Sql_GetUIntData(SqlHandle, 0);
            PChar->m_mentorUnlocked      = Sql_GetUIntData(SqlHandle, 1) > 0;
            PChar->m_jobMasterDisplay    = Sql_GetUIntData(SqlHandle, 2) > 0;
            PChar->menuConfigFlags.flags = (uint32)Sql_GetUIntData(SqlHandle, 3);
        }

        charutils::LoadInventory(PChar);

        CalculateStats(PChar);
        blueutils::LoadSetSpells(PChar);
        puppetutils::LoadAutomaton(PChar);
        BuildingCharSkillsTable(PChar);
        BuildingCharAbilityTable(PChar);
        BuildingCharTraitsTable(PChar);
        jobpointutils::RefreshGiftMods(PChar);

        PChar->animation = (HP == 0 ? ANIMATION_DEATH : ANIMATION_NONE);

        PChar->StatusEffectContainer->LoadStatusEffects();

        charutils::LoadEquip(PChar);
        PChar->health.hp = zoneutils::IsResidentialArea(PChar) ? PChar->GetMaxHP() : HP;
        PChar->health.mp = zoneutils::IsResidentialArea(PChar) ? PChar->GetMaxMP() : MP;
        PChar->UpdateHealth();
        PChar->m_event.EventID = luautils::OnZoneIn(PChar);
        luautils::OnGameIn(PChar, zoning == 1);
    }

    void LoadSpells(CCharEntity* PChar)
    {
        // disable all spells
        PChar->m_SpellList.reset();

        std::string enabledContent = "\"\"";

        // Compile a string of all enabled expansions
        for (auto&& expan : { "COP", "TOAU", "WOTG", "ACP", "AMK", "ASA", "ABYSSEA", "SOA" })
        {
            if (luautils::IsContentEnabled(expan))
            {
                enabledContent += ",\"";
                enabledContent += expan;
                enabledContent += "\"";
            }
        }

        // Select all player spells from enabled expansions
        const char* fmtQuery = "SELECT char_spells.spellid "
                               "FROM char_spells "
                               "JOIN spell_list "
                               "ON spell_list.spellid = char_spells.spellid "
                               "WHERE charid = %u AND "
                               "(spell_list.content_tag IN (%s) OR "
                               "spell_list.content_tag IS NULL);";

        int32 ret = Sql_Query(SqlHandle, fmtQuery, PChar->id, enabledContent.c_str());

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
        {
            while (Sql_NextRow(SqlHandle) == SQL_SUCCESS)
            {
                uint16 spellId = Sql_GetUIntData(SqlHandle, 0);

                if (spell::GetSpell(static_cast<SpellID>(spellId)) != nullptr)
                {
                    PChar->m_SpellList.set(spellId);
                }
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  Загружаем инвентарь персонажа                                        *
     *                                                                       *
     ************************************************************************/

    void LoadInventory(CCharEntity* PChar)
    {
        const char* Query = "SELECT "
                            "itemid,"     // 0
                            "location,"   // 1
                            "slot,"       // 2
                            "quantity,"   // 3
                            "bazaar,"     // 4
                            "signature, " // 5
                            "extra "      // 6
                            "FROM char_inventory "
                            "WHERE charid = %u "
                            "ORDER BY FIELD(location,0,1,9,2,3,4,5,6,7,8,10,11,12)";

        int32 ret = Sql_Query(SqlHandle, Query, PChar->id);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
        {
            while (Sql_NextRow(SqlHandle) == SQL_SUCCESS)
            {
                CItem* PItem = itemutils::GetItem(Sql_GetIntData(SqlHandle, 0));

                if (PItem != nullptr)
                {
                    PItem->setLocationID((uint8)Sql_GetUIntData(SqlHandle, 1));
                    PItem->setSlotID(Sql_GetUIntData(SqlHandle, 2));
                    PItem->setQuantity(Sql_GetUIntData(SqlHandle, 3));
                    PItem->setCharPrice(Sql_GetUIntData(SqlHandle, 4));

                    size_t length = 0;
                    char*  extra  = nullptr;
                    Sql_GetData(SqlHandle, 6, &extra, &length);
                    memcpy(PItem->m_extra, extra, (length > sizeof(PItem->m_extra) ? sizeof(PItem->m_extra) : length));

                    if (PItem->getCharPrice() != 0)
                    {
                        PItem->setSubType(ITEM_LOCKED);
                    }

                    if (PItem->isType(ITEM_LINKSHELL))
                    {
                        if (static_cast<CItemLinkshell*>(PItem)->GetLSType() == 0)
                        {
                            static_cast<CItemLinkshell*>(PItem)->SetLSType((LSTYPE)(PItem->getID() - 0x200));
                        }
                        int8 EncodedString[16];
                        EncodeStringLinkshell(Sql_GetData(SqlHandle, 5), EncodedString);
                        PItem->setSignature(EncodedString);
                    }
                    else if (PItem->getFlag() & (ITEM_FLAG_INSCRIBABLE))
                    {
                        int8 EncodedString[13];
                        EncodeStringSignature(Sql_GetData(SqlHandle, 5), EncodedString);
                        PItem->setSignature(EncodedString);
                    }

                    if (PItem->isType(ITEM_FURNISHING) && (PItem->getLocationID() == LOC_MOGSAFE || PItem->getLocationID() == LOC_MOGSAFE2))
                    {
                        if (((CItemFurnishing*)PItem)->isInstalled()) // способ узнать, что предмет действительно установлен
                        {
                            PChar->getStorage(LOC_STORAGE)->AddBuff(((CItemFurnishing*)PItem)->getStorage());
                        }
                    }
                    PChar->getStorage(PItem->getLocationID())->InsertItem(PItem, PItem->getSlotID());
                }
            }
        }

        // apply augments
        // loop over each container
        for (uint8 i = 0; i < MAX_CONTAINER_ID; ++i)
        {
            CItemContainer* PItemContainer = PChar->getStorage(i);

            if (PItemContainer != nullptr)
            {
                // now find each item in the container
                for (uint8 y = 0; y < MAX_CONTAINER_SIZE; ++y)
                {
                    CItem* PItem = (CItem*)PItemContainer->GetItem(y);

                    // check if the item is valid and can have an augment applied to it
                    if (PItem != nullptr && ((PItem->isType(ITEM_EQUIPMENT) || PItem->isType(ITEM_WEAPON)) && !PItem->isSubType(ITEM_CHARGED)))
                    {
                        // check if there are any valid augments to be applied to the item
                        for (uint8 j = 0; j < 4; ++j)
                        {
                            // found a match, apply the augment
                            if (((CItemEquipment*)PItem)->getAugment(j) != 0)
                            {
                                ((CItemEquipment*)PItem)->ApplyAugment(j);
                            }
                        }
                    }
                }
            }
        }
    }

    void LoadEquip(CCharEntity* PChar)
    {
        const char* Query = "SELECT "
                            "slotid,"
                            "equipslotid,"
                            "containerid "
                            "FROM char_equip "
                            "WHERE charid = %u;";

        int ret = Sql_Query(SqlHandle, Query, PChar->id);

        if (ret != SQL_ERROR)
        {
            CItemLinkshell* PLinkshell1   = nullptr;
            CItemLinkshell* PLinkshell2   = nullptr;
            bool            hasMainWeapon = false;

            while (Sql_NextRow(SqlHandle) == SQL_SUCCESS)
            {
                if (Sql_GetUIntData(SqlHandle, 1) < 16)
                {
                    if (Sql_GetUIntData(SqlHandle, 1) == SLOT_MAIN)
                    {
                        hasMainWeapon = true;
                    }

                    EquipItem(PChar, Sql_GetUIntData(SqlHandle, 0), Sql_GetUIntData(SqlHandle, 1), Sql_GetUIntData(SqlHandle, 2));
                }
                else
                {
                    uint8  SlotID     = Sql_GetUIntData(SqlHandle, 0);
                    uint8  equipSlot  = Sql_GetUIntData(SqlHandle, 1);
                    uint8  LocationID = Sql_GetUIntData(SqlHandle, 2);
                    CItem* PItem      = PChar->getStorage(LocationID)->GetItem(SlotID);

                    if ((PItem != nullptr) && PItem->isType(ITEM_LINKSHELL))
                    {
                        PItem->setSubType(ITEM_LOCKED);
                        PChar->equip[equipSlot]    = SlotID;
                        PChar->equipLoc[equipSlot] = LocationID;
                        if (equipSlot == SLOT_LINK1)
                        {
                            PLinkshell1 = (CItemLinkshell*)PItem;
                        }
                        else if (equipSlot == SLOT_LINK2)
                        {
                            PLinkshell2 = (CItemLinkshell*)PItem;
                        }
                    }
                }
            }

            // If no weapon is equipped, equip the appropriate unarmed weapon item
            if (!hasMainWeapon)
            {
                CheckUnarmedWeapon(PChar);
            }

            if (PLinkshell1)
            {
                ret = Sql_Query(SqlHandle, "SELECT broken FROM linkshells WHERE linkshellid = %u LIMIT 1", PLinkshell1->GetLSID());
                if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS && Sql_GetUIntData(SqlHandle, 0) == 1)
                { // if the linkshell has been broken, unequip
                    uint8 SlotID     = PLinkshell1->getSlotID();
                    uint8 LocationID = PLinkshell1->getLocationID();
                    PLinkshell1->setSubType(ITEM_UNLOCKED);
                    PChar->equip[SLOT_LINK1] = 0;
                    Sql_Query(SqlHandle, "DELETE char_equip FROM char_equip WHERE charid = %u AND slotid = %u AND containerid = %u", PChar->id, SlotID,
                              LocationID);
                }
                else
                {
                    linkshell::AddOnlineMember(PChar, PLinkshell1, 1);
                }
            }
            if (PLinkshell2)
            {
                ret = Sql_Query(SqlHandle, "SELECT broken FROM linkshells WHERE linkshellid = %u LIMIT 1", PLinkshell2->GetLSID());
                if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS && Sql_GetUIntData(SqlHandle, 0) == 1)
                { // if the linkshell has been broken, unequip
                    uint8 SlotID     = PLinkshell2->getSlotID();
                    uint8 LocationID = PLinkshell2->getLocationID();
                    PLinkshell2->setSubType(ITEM_UNLOCKED);
                    PChar->equip[SLOT_LINK2] = 0;
                    Sql_Query(SqlHandle, "DELETE char_equip FROM char_equip WHERE charid = %u AND slotid = %u AND containerid = %u", PChar->id, SlotID,
                              LocationID);
                }
                else
                {
                    linkshell::AddOnlineMember(PChar, PLinkshell2, 2);
                }
            }
        }
        else
        {
            ShowError("Loading error from char_equip");
        }
    }

    /************************************************************************
     *                                                                       *
     *  Отправляем список текущих/завершенных квестов и миссий               *
     *                                                                       *
     ************************************************************************/

    void SendQuestMissionLog(CCharEntity* PChar)
    {
        // Quests (Current + Completed):
        // --------------------------------
        for (int8 areaID = 0; areaID <= QUESTS_COALITION; areaID++)
        {
            PChar->pushPacket(new CQuestMissionLogPacket(PChar, areaID, LOG_QUEST_CURRENT));
            PChar->pushPacket(new CQuestMissionLogPacket(PChar, areaID, LOG_QUEST_COMPLETE));
        }

        // Completed Missions:
        // --------------------------------
        // Completed missions for Nation + Zilart Missions are all sent in single packet
        PChar->pushPacket(new CQuestMissionLogPacket(PChar, MISSION_ZILART, LOG_MISSION_COMPLETE));

        // Completed missions for TOAU and WOTG are sent in the same packet
        PChar->pushPacket(new CQuestMissionLogPacket(PChar, MISSION_TOAU, LOG_MISSION_COMPLETE));

        // Completed Assaults were sent in the same packet as completed TOAU quests

        // Completed Campaign Operations
        PChar->pushPacket(new CQuestMissionLogPacket(PChar, MISSION_CAMPAIGN, LOG_MISSION_COMPLETE));
        PChar->pushPacket(new CQuestMissionLogPacket(PChar, MISSION_CAMPAIGN, LOG_CAMPAIGN_TWO));

        // Current Missions:
        // --------------------------------
        // Current TOAU, Assault, WOTG, and Campaign mission were sent in the same packet as current TOAU quests

        // Current Nation, Zilart, COP, Add-On, SOA, and ROV missions are all sent in a shared, single packet.
        // So sending this packet updates multiple Mission logs at once.
        PChar->pushPacket(new CQuestMissionLogPacket(PChar, MISSION_ZILART, LOG_MISSION_CURRENT));
    }

    /************************************************************************
     *                                                                       *
     *  Отправляем списки ключевых предметов персонажа                       *
     *                                                                       *
     ************************************************************************/

    void SendKeyItems(CCharEntity* PChar)
    {
        for (uint8 table = 0; table < MAX_KEYS_TABLE; table++)
        {
            PChar->pushPacket(new CKeyItemsPacket(PChar, (KEYS_TABLE)table));
        }
    }

    /************************************************************************
     *                                                                       *
     *  Отправляем персонажу весь его инвентарь                              *
     *                                                                       *
     ************************************************************************/

    void SendInventory(CCharEntity* PChar)
    {
        auto pushContainer = [&](auto LocationID) {
            CItemContainer* container = PChar->getStorage(LocationID);
            if (container == nullptr)
            {
                return;
            }

            uint8 size = container->GetSize();
            for (uint8 slotID = 0; slotID <= size; ++slotID)
            {
                CItem* PItem = PChar->getStorage(LocationID)->GetItem(slotID);
                if (PItem != nullptr)
                {
                    PChar->pushPacket(new CInventoryItemPacket(PItem, LocationID, slotID));
                }
            }
        };

        // Send important items first
        // Note: it's possible that non-essential inventory items are sent in response to another packet
        for (auto&& containerID : { LOC_INVENTORY, LOC_TEMPITEMS, LOC_WARDROBE, LOC_WARDROBE2, LOC_WARDROBE3, LOC_WARDROBE4, LOC_MOGSAFE, LOC_STORAGE,
                                    LOC_MOGLOCKER, LOC_MOGSATCHEL, LOC_MOGSACK, LOC_MOGCASE, LOC_MOGSAFE2 })
        {
            pushContainer(containerID);
        }

        for (int32 i = 0; i < 16; ++i)
        {
            CItem* PItem = PChar->getEquip((SLOTTYPE)i);
            if (PItem != nullptr)
            {
                PItem->setSubType(ITEM_LOCKED);
                PChar->pushPacket(new CInventoryAssignPacket(PItem, INV_NODROP));
            }
        }

        CItem* PItem = PChar->getEquip(SLOT_LINK1);
        if (PItem != nullptr)
        {
            PItem->setSubType(ITEM_LOCKED);

            PChar->nameflags.flags |= FLAG_LINKSHELL;
            PChar->pushPacket(new CInventoryItemPacket(PItem, PChar->equipLoc[SLOT_LINK1], PChar->equip[SLOT_LINK1]));
            PChar->pushPacket(new CInventoryAssignPacket(PItem, INV_LINKSHELL));
            PChar->pushPacket(new CLinkshellEquipPacket(PChar, 1));
        }
        else
        {
            PChar->nameflags.flags &= ~FLAG_LINKSHELL;
        }

        PItem = PChar->getEquip(SLOT_LINK2);
        if (PItem != nullptr)
        {
            PItem->setSubType(ITEM_LOCKED);

            PChar->pushPacket(new CInventoryItemPacket(PItem, PChar->equipLoc[SLOT_LINK2], PChar->equip[SLOT_LINK2]));
            PChar->pushPacket(new CInventoryAssignPacket(PItem, INV_LINKSHELL));
            PChar->pushPacket(new CLinkshellEquipPacket(PChar, 2));
        }
        PChar->pushPacket(new CInventoryFinishPacket());
    }

    /************************************************************************
     *                                                                       *
     *  Добавляем новый предмет персонажу в выбранный контейнер              *
     *                                                                       *
     ************************************************************************/

    // TODO: мне не нравится параметр silens, нужно придумать что-нибудь более элегантное

    uint8 AddItem(CCharEntity* PChar, uint8 LocationID, uint16 ItemID, uint32 quantity, bool silence)
    {
        if (PChar->getStorage(LocationID)->GetFreeSlotsCount() == 0 || quantity == 0)
        {
            return ERROR_SLOTID;
        }

        CItem* PItem = itemutils::GetItem(ItemID);

        if (PItem != nullptr)
        {
            PItem->setQuantity(quantity);
            return AddItem(PChar, LocationID, PItem, silence);
        }
        ShowWarning("charplugin::AddItem: Item <%i> is not found in a database", ItemID);
        return ERROR_SLOTID;
    }

    /************************************************************************
     *                                                                       *
     *  Добавляем новый предмет персонажу в выбранный контейнер              *
     *                                                                       *
     ************************************************************************/

    uint8 AddItem(CCharEntity* PChar, uint8 LocationID, CItem* PItem, bool silence)
    {
        if (PItem->isType(ITEM_CURRENCY))
        {
            UpdateItem(PChar, LocationID, 0, PItem->getQuantity());
            delete PItem;
            return 0;
        }
        if (PItem->getFlag() & ITEM_FLAG_RARE)
        {
            if (HasItem(PChar, PItem->getID()))
            {
                if (!silence)
                {
                    PChar->pushPacket(new CMessageStandardPacket(PChar, PItem->getID(), 0, MsgStd::ItemEx));
                }
                delete PItem;
                return ERROR_SLOTID;
            }
        }

        uint8 SlotID = PChar->getStorage(LocationID)->InsertItem(PItem);

        if (SlotID != ERROR_SLOTID)
        {
            // uint8 charges = (PItem->isType(ITEM_USABLE) ? ((CItemUsable*)PItem)->getCurrentCharges() : 0);

            const char* Query = "INSERT INTO char_inventory("
                                "charid,"
                                "location,"
                                "slot,"
                                "itemId,"
                                "quantity,"
                                "signature,"
                                "extra) "
                                "VALUES(%u,%u,%u,%u,%u,'%s','%s')";

            int8 signature[21];
            if (PItem->isType(ITEM_LINKSHELL))
            {
                DecodeStringLinkshell((int8*)PItem->getSignature(), signature);
            }
            else
            {
                DecodeStringSignature((int8*)PItem->getSignature(), signature);
            }

            char extra[sizeof(PItem->m_extra) * 2 + 1];
            Sql_EscapeStringLen(SqlHandle, extra, (const char*)PItem->m_extra, sizeof(PItem->m_extra));

            if (Sql_Query(SqlHandle, Query, PChar->id, LocationID, SlotID, PItem->getID(), PItem->getQuantity(), signature, extra) == SQL_ERROR)
            {
                ShowError("charplugin::AddItem: Cannot insert item to database");
                PChar->getStorage(LocationID)->InsertItem(nullptr, SlotID);
                delete PItem;
                return ERROR_SLOTID;
            }
            PChar->pushPacket(new CInventoryItemPacket(PItem, LocationID, SlotID));
            PChar->pushPacket(new CInventoryFinishPacket());
        }
        else
        {
            ShowDebug("charplugin::AddItem: Location %i is full", LocationID);
            delete PItem;
        }
        return SlotID;
    }

    /************************************************************************
     *                                                                       *
     *  Проверяем наличие предмета у персонажа                               *
     *                                                                       *
     ************************************************************************/

    bool HasItem(CCharEntity* PChar, uint16 ItemID)
    {
        for (uint8 LocID = 0; LocID < MAX_CONTAINER_ID; ++LocID)
        {
            if (PChar->getStorage(LocID)->SearchItem(ItemID) != ERROR_SLOTID)
            {
                return true;
            }
        }
        return false;
    }

    void UpdateSubJob(CCharEntity* PChar)
    {
        jobpointutils::RefreshGiftMods(PChar);
        charutils::BuildingCharSkillsTable(PChar);
        charutils::CalculateStats(PChar);
        charutils::CheckValidEquipment(PChar);
        PChar->PRecastContainer->ChangeJob();
        charutils::BuildingCharAbilityTable(PChar);
        charutils::BuildingCharTraitsTable(PChar);

        PChar->UpdateHealth();
        PChar->health.hp = PChar->GetMaxHP();
        PChar->health.mp = PChar->GetMaxMP();

        charutils::SaveCharStats(PChar);
        charutils::SaveCharJob(PChar, PChar->GetMJob());
        charutils::SaveCharExp(PChar, PChar->GetMJob());
        PChar->updatemask |= UPDATE_HP;

        PChar->pushPacket(new CCharJobsPacket(PChar));
        PChar->pushPacket(new CCharStatsPacket(PChar));
        PChar->pushPacket(new CCharSkillsPacket(PChar));
        PChar->pushPacket(new CCharRecastPacket(PChar));
        PChar->pushPacket(new CCharAbilitiesPacket(PChar));
        PChar->pushPacket(new CCharUpdatePacket(PChar));
        PChar->pushPacket(new CMenuMeritPacket(PChar));
        PChar->pushPacket(new CCharSyncPacket(PChar));
    }

    /************************************************************************
     *                                                                       *
     *  Перемещаем предмет в указанную ячейки или первую пустую              *
     *                                                                       *
     ************************************************************************/

    uint8 MoveItem(CCharEntity* PChar, uint8 LocationID, uint8 SlotID, uint8 NewSlotID)
    {
        CItemContainer* PItemContainer = PChar->getStorage(LocationID);

        if (PItemContainer->GetFreeSlotsCount() != 0)
        {
            if (NewSlotID == ERROR_SLOTID)
            {
                NewSlotID = PItemContainer->InsertItem(PItemContainer->GetItem(SlotID));
            }
            else
            {
                if (PItemContainer->GetItem(NewSlotID) != nullptr)
                {
                    NewSlotID = ERROR_SLOTID;
                }
            }
            if (NewSlotID != ERROR_SLOTID)
            {
                const char* Query = "UPDATE char_inventory "
                                    "SET slot = %u "
                                    "WHERE charid = %u AND location = %u AND slot = %u";

                if (Sql_Query(SqlHandle, Query, NewSlotID, PChar->id, LocationID, SlotID) != SQL_ERROR && Sql_AffectedRows(SqlHandle) != 0)
                {
                    PItemContainer->InsertItem(nullptr, SlotID);

                    PChar->pushPacket(new CInventoryItemPacket(nullptr, LocationID, SlotID));
                    PChar->pushPacket(new CInventoryItemPacket(PItemContainer->GetItem(NewSlotID), LocationID, NewSlotID));
                    return NewSlotID;
                }
                PItemContainer->InsertItem(nullptr, NewSlotID); // отменяем все изменения контейнера
            }
        }
        ShowError("charutils::MoveItem: item can't be moved");
        return ERROR_SLOTID;
    }

    /************************************************************************
     *                                                                       *
     *  Update the number of items in the specified container and slot       *
     *                                                                       *
     ************************************************************************/

    uint32 UpdateItem(CCharEntity* PChar, uint8 LocationID, uint8 slotID, int32 quantity, bool force)
    {
        CItem* PItem = PChar->getStorage(LocationID)->GetItem(slotID);

        if (PItem == nullptr)
        {
            ShowDebug("UpdateItem: No item in slot %u", slotID);
            PChar->pushPacket(new CInventoryItemPacket(nullptr, LocationID, slotID));
            return 0;
        }

        uint16 ItemID = PItem->getID();

        if ((int32)(PItem->getQuantity() - PItem->getReserve() + quantity) < 0)
        {
            ShowDebug("UpdateItem: %s trying to move invalid quantity %u of itemID %u", PChar->GetName(), quantity, ItemID);
            return 0;
        }

        auto* PState = dynamic_cast<CItemState*>(PChar->PAI->GetCurrentState());
        if (PState)
        {
            CItem* item = PState->GetItem();

            if (item && item->getSlotID() == PItem->getSlotID() && item->getLocationID() == PItem->getLocationID() && !force)
            {
                return 0;
            }
        }

        uint32 newQuantity = PItem->getQuantity() + quantity;

        if (newQuantity > PItem->getStackSize())
        {
            newQuantity = PItem->getStackSize();
        }

        if (newQuantity > 0 || PItem->isType(ITEM_CURRENCY))
        {
            const char* Query = "UPDATE char_inventory "
                                "SET quantity = %u "
                                "WHERE charid = %u AND location = %u AND slot = %u;";

            if (Sql_Query(SqlHandle, Query, newQuantity, PChar->id, LocationID, slotID) != SQL_ERROR)
            {
                PItem->setQuantity(newQuantity);
                PChar->pushPacket(new CInventoryModifyPacket(LocationID, slotID, newQuantity));
            }
        }
        else if (newQuantity == 0)
        {
            const char* Query = "DELETE FROM char_inventory WHERE charid = %u AND location = %u AND slot = %u;";

            if (Sql_Query(SqlHandle, Query, PChar->id, LocationID, slotID) != SQL_ERROR)
            {
                PChar->getStorage(LocationID)->InsertItem(nullptr, slotID);
                PChar->pushPacket(new CInventoryItemPacket(nullptr, LocationID, slotID));

                if (PChar->getStyleLocked() && !HasItem(PChar, ItemID))
                {
                    if (PItem->isType(ITEM_WEAPON))
                    {
                        if (PChar->styleItems[SLOT_MAIN] == ItemID)
                        {
                            charutils::UpdateWeaponStyle(PChar, SLOT_MAIN, (CItemWeapon*)PChar->getEquip(SLOT_MAIN));
                        }
                        else if (PChar->styleItems[SLOT_SUB] == ItemID)
                        {
                            charutils::UpdateWeaponStyle(PChar, SLOT_SUB, (CItemWeapon*)PChar->getEquip(SLOT_SUB));
                        }
                    }
                    else if (PItem->isType(ITEM_EQUIPMENT))
                    {
                        auto equipSlotID = ((CItemEquipment*)PItem)->getSlotType();
                        if (PChar->styleItems[equipSlotID] == ItemID)
                        {
                            switch (equipSlotID)
                            {
                                case SLOT_HEAD:
                                case SLOT_BODY:
                                case SLOT_HANDS:
                                case SLOT_LEGS:
                                case SLOT_FEET:
                                    charutils::UpdateArmorStyle(PChar, equipSlotID);
                                    break;
                            }
                        }
                    }
                }

                delete PItem;
            }
        }
        return ItemID;
    }

    /************************************************************************
     *                                                                       *
     *  Check the possibility of trade between the characters                *
     *                                                                       *
     ************************************************************************/

    bool CanTrade(CCharEntity* PChar, CCharEntity* PTarget)
    {
        if (PTarget->getStorage(LOC_INVENTORY)->GetFreeSlotsCount() < PChar->UContainer->GetItemsCount())
        {
            ShowDebug("Unable to trade, %s doesn't have enough inventory space", PTarget->GetName());
            return false;
        }
        for (uint8 slotid = 0; slotid <= 8; ++slotid)
        {
            CItem* PItem = PChar->UContainer->GetItem(slotid);

            if (PItem != nullptr && PItem->getFlag() & ITEM_FLAG_RARE)
            {
                if (HasItem(PTarget, PItem->getID()))
                {
                    ShowDebug("Unable to trade, %s has the rare item already (%s)", PTarget->GetName(), PItem->getName());
                    return false;
                }
            }
        }
        return true;
    }

    /************************************************************************
     *                                                                       *
     *  Совершаем обмен между персонажами                                    *
     *                                                                       *
     ************************************************************************/

    void DoTrade(CCharEntity* PChar, CCharEntity* PTarget)
    {
        ShowDebug("%s->%s trade item movement started", PChar->GetName(), PTarget->GetName());
        for (uint8 slotid = 0; slotid <= 8; ++slotid)
        {
            CItem* PItem = PChar->UContainer->GetItem(slotid);

            if (PItem != nullptr)
            {
                if (PItem->getStackSize() == 1 && PItem->getReserve() == 1)
                {
                    CItem* PNewItem = itemutils::GetItem(PItem);
                    ShowDebug("Adding %s to %s inventory stacksize 1", PNewItem->getName(), PTarget->GetName());
                    PNewItem->setReserve(0);
                    AddItem(PTarget, LOC_INVENTORY, PNewItem);
                }
                else
                {
                    ShowDebug("Adding %s to %s inventory", PItem->getName(), PTarget->GetName());
                    AddItem(PTarget, LOC_INVENTORY, PItem->getID(), PItem->getReserve());
                }
                ShowDebug("Removing %s from %s's inventory", PItem->getName(), PChar->GetName());
                auto amount = PItem->getReserve();
                PItem->setReserve(0);
                UpdateItem(PChar, LOC_INVENTORY, PItem->getSlotID(), (int32)(0 - amount));
                PChar->UContainer->ClearSlot(slotid);
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  Снимаем с персонажа экипированный предмет без обновления внешного    *
     *  вида. Используется как вспомогательная функция в связке с другими    *
     *                                                                       *
     ************************************************************************/

    void UnequipItem(CCharEntity* PChar, uint8 equipSlotID, bool update)
    {
        CItem* PItem = PChar->getEquip((SLOTTYPE)equipSlotID);

        if ((PItem != nullptr) && PItem->isType(ITEM_EQUIPMENT))
        {
            auto removeSlotID = ((CItemEquipment*)PItem)->getRemoveSlotId();

            for (auto i = 0u; i < sizeof(removeSlotID) * 8; ++i)
            {
                if (removeSlotID & (1 << i))
                {
                    if (i >= SLOT_HEAD && i <= SLOT_FEET)
                    {
                        switch (i)
                        {
                            case SLOT_HEAD:
                                PChar->look.head = 0;
                                break;
                            case SLOT_BODY:
                                PChar->look.body = 0;
                                break;
                            case SLOT_HANDS:
                                PChar->look.hands = 0;
                                break;
                            case SLOT_LEGS:
                                PChar->look.legs = 0;
                                break;
                            case SLOT_FEET:
                                PChar->look.feet = 0;
                                break;
                        }
                    }
                }
            }

            // Call the LUA event before actually "unequipping" the item so the script can do stuff with it first
            if (((CItemEquipment*)PItem)->getScriptType() & SCRIPT_EQUIP)
            {
                luautils::OnItemCheck(PChar, PItem, ITEMCHECK::UNEQUIP, nullptr);
            }

            // todo: issues as item 0 reference is being handled as a real equipment piece
            //      thought to be source of nin bug
            PChar->equip[equipSlotID]    = 0;
            PChar->equipLoc[equipSlotID] = 0;

            if (((CItemEquipment*)PItem)->getScriptType() & SCRIPT_EQUIP)
            {
                PChar->m_EquipFlag = 0;
                for (uint8 i = 0; i < 16; ++i)
                {
                    CItem* PItem = PChar->getEquip((SLOTTYPE)i);

                    if ((PItem != nullptr) && PItem->isType(ITEM_EQUIPMENT))
                    {
                        PChar->m_EquipFlag |= ((CItemEquipment*)PItem)->getScriptType();
                    }
                }
            }

            if (PItem->isSubType(ITEM_CHARGED))
            {
                PChar->PRecastContainer->Del(RECAST_ITEM, PItem->getSlotID() << 8 |
                                                              PItem->getLocationID()); // Also remove item from the Recast List no matter what bag its in
            }
            PItem->setSubType(ITEM_UNLOCKED);

            if (equipSlotID == SLOT_SUB)
            {
                // Removed sub item, if main hand is empty, then possibly eligible for H2H weapon
                if (!PChar->getEquip(SLOT_MAIN) || !PChar->getEquip(SLOT_MAIN)->isType(ITEM_EQUIPMENT))
                {
                    CheckUnarmedWeapon(PChar);
                }
                PChar->m_dualWield = false;
            }
            PChar->delEquipModifiers(&((CItemEquipment*)PItem)->modList, ((CItemEquipment*)PItem)->getReqLvl(), equipSlotID);
            PChar->PLatentEffectContainer->DelLatentEffects(((CItemEquipment*)PItem)->getReqLvl(), equipSlotID);
            PChar->delPetModifiers(&((CItemEquipment*)PItem)->petModList);

            PChar->pushPacket(new CInventoryAssignPacket(PItem, INV_NORMAL)); //???
            PChar->pushPacket(new CEquipPacket(0, equipSlotID, LOC_INVENTORY));

            switch (equipSlotID)
            {
                case SLOT_HEAD:
                    PChar->look.head = 0;
                    break;
                case SLOT_BODY:
                    PChar->look.body = 0;
                    break;
                case SLOT_HANDS:
                    PChar->look.hands = 0;
                    break;
                case SLOT_LEGS:
                    PChar->look.legs = 0;
                    break;
                case SLOT_FEET:
                    PChar->look.feet = 0;
                    break;
                case SLOT_SUB:
                {
                    PChar->look.sub            = 0;
                    PChar->m_Weapons[SLOT_SUB] = itemutils::GetUnarmedItem(); // << equips "nothing" in the sub slot to prevent multi attack exploit
                    PChar->health.tp           = 0;
                    PChar->StatusEffectContainer->DelStatusEffect(EFFECT_AFTERMATH);
                    BuildingCharWeaponSkills(PChar);
                    UpdateWeaponStyle(PChar, equipSlotID, nullptr);
                }
                break;
                case SLOT_AMMO:
                {
                    if (PChar->equip[SLOT_RANGED] == 0)
                    {
                        PChar->look.ranged = 0;
                    }
                    PChar->m_Weapons[SLOT_AMMO] = nullptr;
                    UpdateWeaponStyle(PChar, equipSlotID, nullptr);
                }
                break;
                case SLOT_RANGED:
                {
                    if (PChar->equip[SLOT_RANGED] == 0)
                    {
                        PChar->look.ranged = 0;
                    }
                    PChar->m_Weapons[SLOT_RANGED] = nullptr;
                    if (((CItemWeapon*)PItem)->getSkillType() != SKILL_STRING_INSTRUMENT && ((CItemWeapon*)PItem)->getSkillType() != SKILL_WIND_INSTRUMENT)
                    {
                        PChar->health.tp = 0;
                    }
                    PChar->StatusEffectContainer->DelStatusEffect(EFFECT_AFTERMATH);
                    BuildingCharWeaponSkills(PChar);
                    UpdateWeaponStyle(PChar, equipSlotID, nullptr);
                }
                break;
                case SLOT_MAIN:
                {
                    if (PItem->isType(ITEM_WEAPON))
                    {
                        if (((CItemWeapon*)PItem)->getSkillType() == SKILL_HAND_TO_HAND)
                        {
                            PChar->look.sub = 0;
                        }
                    }

                    if (PChar->PAI->IsEngaged())
                    {
                        auto* state = dynamic_cast<CAttackState*>(PChar->PAI->GetCurrentState());
                        if (state)
                        {
                            state->ResetAttackTimer();
                        }
                    }

                    // If main hand is empty, figure out which UnarmedItem to give the player.
                    if (!PChar->getEquip(SLOT_MAIN) || !PChar->getEquip(SLOT_MAIN)->isType(ITEM_EQUIPMENT))
                    {
                        CheckUnarmedWeapon(PChar);
                    }

                    PChar->health.tp = 0;
                    PChar->StatusEffectContainer->DelStatusEffect(EFFECT_AFTERMATH);
                    BuildingCharWeaponSkills(PChar);
                    UpdateWeaponStyle(PChar, equipSlotID, nullptr);
                }
                break;
            }

            if (update)
            {
                charutils::BuildingCharSkillsTable(PChar);
                PChar->UpdateHealth();
                PChar->m_EquipSwap = true;
                PChar->updatemask |= UPDATE_LOOK;
            }
        }
    }

    void RemoveSub(CCharEntity* PChar)
    {
        CItemEquipment* PItem = (CItemEquipment*)PChar->getEquip(SLOT_SUB);

        if (PItem != nullptr && PItem->isType(ITEM_EQUIPMENT))
        {
            UnequipItem(PChar, SLOT_SUB);
        }
    }

    /************************************************************************
     *                                                                       *
     *  Пытаемся экипировать предмет с соблюдением всех условий              *
     *                                                                       *
     ************************************************************************/

    bool EquipArmor(CCharEntity* PChar, uint8 slotID, uint8 equipSlotID, uint8 containerID)
    {
        CItemEquipment* PItem   = (CItemEquipment*)PChar->getStorage(containerID)->GetItem(slotID);
        CItemEquipment* oldItem = PChar->getEquip((SLOTTYPE)equipSlotID);

        if (PItem == nullptr)
        {
            ShowDebug("No item in inventory slot %u", slotID);
            return false;
        }

        if ((PChar->m_EquipBlock & (1 << equipSlotID)) || !(PItem->getJobs() & (1 << (PChar->GetMJob() - 1))) ||
            (PItem->getReqLvl() > (map_config.disable_gear_scaling ? PChar->GetMLevel() : PChar->jobs.job[PChar->GetMJob()])))
        {
            return false;
        }

        if (equipSlotID == SLOT_MAIN)
        {
            if (!(slotID == PItem->getSlotID() && oldItem && (oldItem->isType(ITEM_WEAPON) && PItem->isType(ITEM_WEAPON)) &&
                  ((((CItemWeapon*)PItem)->isTwoHanded()) && (((CItemWeapon*)oldItem)->isTwoHanded()))))
            {
                CItemEquipment* PSubItem = PChar->getEquip(SLOT_SUB);

                if (PSubItem != nullptr && PSubItem->isType(ITEM_EQUIPMENT) && (!PSubItem->IsShield()))
                {
                    RemoveSub(PChar);
                }
            }
        }

        UnequipItem(PChar, equipSlotID, false);

        if (PItem->getEquipSlotId() & (1 << equipSlotID))
        {
            auto removeSlotID = PItem->getRemoveSlotId();

            for (auto i = 0u; i < sizeof(removeSlotID) * 8; ++i)
            {
                if (removeSlotID & (1 << i))
                {
                    UnequipItem(PChar, i, false);
                    if (i >= SLOT_HEAD && i <= SLOT_FEET)
                    {
                        switch (i)
                        {
                            case SLOT_HEAD:
                                PChar->look.head = PItem->getModelId();
                                break;
                            case SLOT_BODY:
                                PChar->look.body = PItem->getModelId();
                                break;
                            case SLOT_HANDS:
                                PChar->look.hands = PItem->getModelId();
                                break;
                            case SLOT_LEGS:
                                PChar->look.legs = PItem->getModelId();
                                break;
                            case SLOT_FEET:
                                PChar->look.feet = PItem->getModelId();
                                break;
                        }
                    }
                }
            }

            for (uint8 i = 0; i < SLOT_BACK; ++i)
            {
                CItemEquipment* armor = PChar->getEquip((SLOTTYPE)i);
                if (armor && armor->isType(ITEM_EQUIPMENT) && armor->getRemoveSlotId() & PItem->getEquipSlotId())
                {
                    UnequipItem(PChar, i, false);
                }
            }

            switch (equipSlotID)
            {
                case SLOT_MAIN:
                {
                    if (PItem->isType(ITEM_WEAPON))
                    {
                        switch (((CItemWeapon*)PItem)->getSkillType())
                        {
                            case SKILL_HAND_TO_HAND:
                            case SKILL_GREAT_SWORD:
                            case SKILL_GREAT_AXE:
                            case SKILL_SCYTHE:
                            case SKILL_POLEARM:
                            case SKILL_GREAT_KATANA:
                            case SKILL_STAFF:
                            {
                                CItemEquipment* armor = (CItemEquipment*)PChar->getEquip(SLOT_SUB);
                                if ((armor != nullptr) && armor->isType(ITEM_EQUIPMENT))
                                {
                                    if (armor->isType(ITEM_WEAPON))
                                    {
                                        CItemWeapon* PWeapon = (CItemWeapon*)armor;
                                        if (PWeapon->getSkillType() != SKILL_NONE || ((CItemWeapon*)PItem)->getSkillType() == SKILL_HAND_TO_HAND)
                                        {
                                            UnequipItem(PChar, SLOT_SUB, false);
                                        }
                                    }
                                    else
                                    {
                                        UnequipItem(PChar, SLOT_SUB, false);
                                    }
                                }
                                if (((CItemWeapon*)PItem)->getSkillType() == SKILL_HAND_TO_HAND)
                                {
                                    PChar->look.sub = PItem->getModelId() + 0x1000;
                                }
                            }
                            break;
                        }
                        if (PChar->PAI->IsEngaged())
                        {
                            auto* state = dynamic_cast<CAttackState*>(PChar->PAI->GetCurrentState());
                            if (state)
                            {
                                state->ResetAttackTimer();
                            }
                        }
                        PChar->m_Weapons[SLOT_MAIN] = PItem;

                        if (!((CItemWeapon*)PChar->m_Weapons[SLOT_MAIN])->isTwoHanded())
                        {
                            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_HASSO);
                            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_SEIGAN);
                        }
                    }
                    PChar->look.main = PItem->getModelId();
                    UpdateWeaponStyle(PChar, equipSlotID, (CItemWeapon*)PItem);
                }
                break;
                case SLOT_SUB:
                {
                    CItemWeapon* weapon = (CItemWeapon*)PChar->getEquip(SLOT_MAIN);
                    if (weapon == nullptr || !weapon->isType(ITEM_WEAPON))
                    {
                        if (PItem->isType(ITEM_WEAPON))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        switch (weapon->getSkillType())
                        {
                            case SKILL_HAND_TO_HAND:
                            {
                                if (!PItem->isType(ITEM_WEAPON))
                                {
                                    UnequipItem(PChar, SLOT_MAIN, false);
                                }
                            }
                            case SKILL_DAGGER:
                            case SKILL_SWORD:
                            case SKILL_AXE:
                            case SKILL_KATANA:
                            case SKILL_CLUB:
                            {
                                if (PItem->isType(ITEM_WEAPON) &&
                                    (!charutils::hasTrait(PChar, TRAIT_DUAL_WIELD) || ((CItemWeapon*)PItem)->getSkillType() == SKILL_NONE))
                                {
                                    return false;
                                }
                                PChar->m_Weapons[SLOT_SUB] = (CItemWeapon*)PItem;
                                PChar->m_dualWield         = true;
                            }
                            break;
                            default:
                            {
                                if (!PItem->isType(ITEM_WEAPON))
                                {
                                    UnequipItem(PChar, SLOT_MAIN, false);
                                }
                                else if (!(((CItemWeapon*)PItem)->getSkillType() == SKILL_NONE))
                                {
                                    // allow Grips to be equipped
                                    return false;
                                }
                            }
                        }
                    }
                    PChar->look.sub = PItem->getModelId();
                    UpdateWeaponStyle(PChar, equipSlotID, (CItemWeapon*)PItem);
                }
                break;
                case SLOT_RANGED:
                {
                    if (PItem->isType(ITEM_WEAPON))
                    {
                        CItemWeapon* weapon = (CItemWeapon*)PChar->getEquip(SLOT_AMMO);
                        if ((weapon != nullptr) && weapon->isType(ITEM_WEAPON))
                        {
                            if (((CItemWeapon*)PItem)->getSkillType() != weapon->getSkillType() ||
                                ((CItemWeapon*)PItem)->getSubSkillType() != weapon->getSubSkillType())
                            {
                                UnequipItem(PChar, SLOT_AMMO, false);
                            }
                        }
                        PChar->m_Weapons[SLOT_RANGED] = (CItemWeapon*)PItem;
                    }
                    PChar->look.ranged = PItem->getModelId();
                    UpdateWeaponStyle(PChar, equipSlotID, (CItemWeapon*)PItem);
                }
                break;
                case SLOT_AMMO:
                {
                    if (PItem->isType(ITEM_WEAPON))
                    {
                        CItemWeapon* weapon = (CItemWeapon*)PChar->getEquip(SLOT_RANGED);
                        if ((weapon != nullptr) && weapon->isType(ITEM_WEAPON))
                        {
                            if (((CItemWeapon*)PItem)->getSkillType() != weapon->getSkillType() ||
                                ((CItemWeapon*)PItem)->getSubSkillType() != weapon->getSubSkillType())
                            {
                                UnequipItem(PChar, SLOT_RANGED, false);
                            }
                        }
                        if (PChar->equip[SLOT_RANGED] == 0)
                        {
                            PChar->look.ranged = PItem->getModelId();
                        }
                        PChar->m_Weapons[SLOT_AMMO] = (CItemWeapon*)PItem;
                        UpdateWeaponStyle(PChar, equipSlotID, (CItemWeapon*)PItem);
                    }
                }
                break;
                case SLOT_HEAD:
                {
                    PChar->look.head = PItem->getModelId();
                }
                break;
                case SLOT_BODY:
                {
                    PChar->look.body = PItem->getModelId();
                }
                break;
                case SLOT_HANDS:
                {
                    PChar->look.hands = PItem->getModelId();
                }
                break;
                case SLOT_LEGS:
                {
                    PChar->look.legs = PItem->getModelId();
                }
                break;
                case SLOT_FEET:
                {
                    PChar->look.feet = PItem->getModelId();
                }
                break;
            }
            PChar->equip[equipSlotID]    = slotID;
            PChar->equipLoc[equipSlotID] = containerID;
        }
        else
        {
            ShowWarning("Item %i is not equipable in equip slot %i", PItem->getID(), equipSlotID);
            return false;
        }
        return true;
    }

    bool canEquipItemOnAnyJob(CCharEntity* PChar, CItemEquipment* PItem)
    {
        if (PItem == nullptr)
        {
            return true;
        }

        for (uint8 i = 1; i < MAX_JOBTYPE; i++)
        {
            if (PItem->getJobs() & (1 << (i - 1)) && PItem->getReqLvl() <= PChar->jobs.job[i])
            {
                return true;
            }
        }
        return false;
    }

    bool hasValidStyle(CCharEntity* PChar, CItemEquipment* PItem, CItemEquipment* AItem)
    {
        return (PItem != nullptr && AItem != nullptr && (((CItemWeapon*)AItem)->getSkillType() == ((CItemWeapon*)PItem)->getSkillType()) &&
                HasItem(PChar, AItem->getID()) && canEquipItemOnAnyJob(PChar, AItem));
    }

    void SetStyleLock(CCharEntity* PChar, bool isStyleLocked)
    {
        if (isStyleLocked)
        {
            for (uint8 i = 0; i < SLOT_LINK1; i++)
            {
                auto* PItem          = PChar->getEquip((SLOTTYPE)i);
                PChar->styleItems[i] = (PItem == nullptr) ? 0 : PItem->getID();
            }
            memcpy(&PChar->mainlook, &PChar->look, sizeof(PChar->look));
        }
        else
        {
            for (unsigned short& styleItem : PChar->styleItems)
            {
                styleItem = 0;
            }
        }

        if (PChar->getStyleLocked() != isStyleLocked)
        {
            PChar->pushPacket(new CMessageStandardPacket(isStyleLocked ? MsgStd::StyleLockOn : MsgStd::StyleLockOff));
        }
        PChar->setStyleLocked(isStyleLocked);
    }

    void UpdateWeaponStyle(CCharEntity* PChar, uint8 equipSlotID, CItemWeapon* PItem)
    {
        if (!PChar->getStyleLocked())
        {
            return;
        }

        auto* appearance      = (CItemEquipment*)itemutils::GetItem(PChar->styleItems[equipSlotID]);
        auto  appearanceModel = (appearance == nullptr) ? 0 : appearance->getModelId();

        switch (equipSlotID)
        {
            case SLOT_MAIN:
                if (hasValidStyle(PChar, PItem, appearance))
                {
                    PChar->mainlook.main = appearanceModel;
                }
                else
                {
                    PChar->mainlook.main = PChar->look.main;
                }

                if (PItem == nullptr)
                {
                    PChar->mainlook.sub = PChar->look.sub;
                }
                else
                {
                    switch (((CItemWeapon*)PItem)->getSkillType())
                    {
                        case SKILL_HAND_TO_HAND:
                            PChar->mainlook.sub = appearanceModel + 0x1000;
                            break;
                        case SKILL_GREAT_SWORD:
                        case SKILL_GREAT_AXE:
                        case SKILL_SCYTHE:
                        case SKILL_POLEARM:
                        case SKILL_GREAT_KATANA:
                        case SKILL_STAFF:
                            PChar->mainlook.sub = PChar->look.sub;
                            break;
                    }
                }
                break;
            case SLOT_SUB:
                if (hasValidStyle(PChar, PItem, appearance))
                {
                    PChar->mainlook.sub = appearanceModel;
                }
                else
                {
                    PChar->mainlook.sub = PChar->look.sub;
                }
                break;
            case SLOT_RANGED:
            case SLOT_AMMO:
                // Appears as though these aren't implemented by SE.
                break;
        }
    }

    void UpdateArmorStyle(CCharEntity* PChar, uint8 equipSlotID)
    {
        if (!PChar->getStyleLocked())
        {
            return;
        }

        auto  itemID          = PChar->styleItems[equipSlotID];
        auto* appearance      = (CItemEquipment*)itemutils::GetItem(itemID);
        auto  appearanceModel = (appearance == nullptr || !HasItem(PChar, itemID)) ? 0 : appearance->getModelId();

        if (!canEquipItemOnAnyJob(PChar, appearance))
        {
            return;
        }

        switch (equipSlotID)
        {
            case SLOT_HEAD:
                PChar->mainlook.head = appearanceModel;
                break;
            case SLOT_BODY:
                PChar->mainlook.body = appearanceModel;
                break;
            case SLOT_HANDS:
                PChar->mainlook.hands = appearanceModel;
                break;
            case SLOT_LEGS:
                PChar->mainlook.legs = appearanceModel;
                break;
            case SLOT_FEET:
                PChar->mainlook.feet = appearanceModel;
                break;
        }
    }

    /************************************************************************
     *                                                                       *
     *                                                                       *
     *                                                                       *
     ************************************************************************/

    void EquipItem(CCharEntity* PChar, uint8 slotID, uint8 equipSlotID, uint8 containerID)
    {
        CItemEquipment* PItem = (CItemEquipment*)PChar->getStorage(containerID)->GetItem(slotID);

        if (PItem && PItem == PChar->getEquip((SLOTTYPE)equipSlotID))
        {
            return;
        }

        if (equipSlotID == SLOT_SUB && PItem && !PItem->IsShield() && ((CItemWeapon*)PItem)->getSkillType() == SKILL_NONE)
        {
            CItemEquipment* PMainItem = PChar->getEquip(SLOT_MAIN);
            if (!PMainItem || !((CItemWeapon*)PMainItem)->isTwoHanded())
            {
                PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, 0, 0, 0x200));
                return;
            }
        }

        if (slotID == 0)
        {
            CItemEquipment* PSubItem = PChar->getEquip(SLOT_SUB);

            UnequipItem(PChar, equipSlotID);

            if (equipSlotID == 0 && PSubItem && !PSubItem->IsShield())
            {
                RemoveSub(PChar);
            }

            PChar->pushPacket(new CEquipPacket(slotID, equipSlotID, containerID));
        }
        else
        {
            if ((PItem != nullptr) && PItem->isType(ITEM_EQUIPMENT))
            {
                if (!PItem->isSubType(ITEM_LOCKED) && EquipArmor(PChar, slotID, equipSlotID, containerID))
                {
                    if (PItem->getScriptType() & SCRIPT_EQUIP)
                    {
                        luautils::OnItemCheck(PChar, PItem, ITEMCHECK::EQUIP, nullptr);
                        PChar->m_EquipFlag |= PItem->getScriptType();
                    }
                    if (PItem->isType(ITEM_USABLE) && ((CItemUsable*)PItem)->getCurrentCharges() != 0)
                    {
                        PItem->setAssignTime(CVanaTime::getInstance()->getVanaTime());
                        PChar->PRecastContainer->Add(RECAST_ITEM, slotID << 8 | containerID,
                                                     PItem->getReuseTime() / 1000); // add recast timer to Recast List from any bag

                        // не забываем обновить таймер при экипировке предмета

                        PChar->pushPacket(new CInventoryItemPacket(PItem, containerID, slotID));
                        PChar->pushPacket(new CInventoryFinishPacket());
                    }
                    PItem->setSubType(ITEM_LOCKED);

                    if (equipSlotID == SLOT_SUB)
                    {
                        // If main hand is empty, check which UnarmedItem to use.
                        if (!PChar->getEquip(SLOT_MAIN) || !PChar->getEquip(SLOT_MAIN)->isType(ITEM_EQUIPMENT))
                        {
                            CheckUnarmedWeapon(PChar);
                        }
                    }

                    PChar->addEquipModifiers(&PItem->modList, ((CItemEquipment*)PItem)->getReqLvl(), equipSlotID);
                    PChar->PLatentEffectContainer->AddLatentEffects(PItem->latentList, ((CItemEquipment*)PItem)->getReqLvl(), equipSlotID);
                    PChar->PLatentEffectContainer->CheckLatentsEquip(equipSlotID);
                    PChar->addPetModifiers(&PItem->petModList);

                    PChar->pushPacket(new CEquipPacket(slotID, equipSlotID, containerID));
                    PChar->pushPacket(new CInventoryAssignPacket(PItem, INV_NODROP));
                }
            }
        }
        if (equipSlotID == SLOT_MAIN || equipSlotID == SLOT_RANGED || equipSlotID == SLOT_SUB)
        {
            if (!PItem || !PItem->isType(ITEM_EQUIPMENT) ||
                (((CItemWeapon*)PItem)->getSkillType() != SKILL_STRING_INSTRUMENT && ((CItemWeapon*)PItem)->getSkillType() != SKILL_WIND_INSTRUMENT))
            {
                // If the weapon ISN'T a wind based instrument or a string based instrument
                PChar->health.tp = 0;
            }

            /*// fixes logging in with no h2h
            if(PChar->m_Weapons[SLOT_MAIN]->getDmgType() == DAMAGE_NONE && PChar->GetMJob() == JOB_MNK)
            {
                PChar->m_Weapons[SLOT_MAIN] = itemutils::GetUnarmedH2HItem();
            }
            else if(PChar->m_Weapons[SLOT_MAIN] == itemutils::GetUnarmedH2HItem() && PChar->GetMJob() != JOB_MNK)
            {
                // return back to normal if changed jobs
                PChar->m_Weapons[SLOT_MAIN] = itemutils::GetUnarmedItem();
            }*/

            if (!PChar->getEquip(SLOT_MAIN) || !PChar->getEquip(SLOT_MAIN)->isType(ITEM_EQUIPMENT) ||
                PChar->m_Weapons[SLOT_MAIN] == itemutils::GetUnarmedH2HItem())
            {
                CheckUnarmedWeapon(PChar);
            }

            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_AFTERMATH);
            BuildingCharWeaponSkills(PChar);
            PChar->pushPacket(new CCharAbilitiesPacket(PChar));
        }

        charutils::BuildingCharSkillsTable(PChar);

        PChar->UpdateHealth();
        PChar->m_EquipSwap = true;
        PChar->updatemask |= UPDATE_LOOK;
    }

    /************************************************************************
     *                                                                       *
     *  Проверяем возможность персонажа носить экипированные на нем предметы *
     *                                                                       *
     ************************************************************************/

    void CheckValidEquipment(CCharEntity* PChar)
    {
        CItemEquipment* PItem = nullptr;

        for (uint8 slotID = 0; slotID < 16; ++slotID)
        {
            PItem = PChar->getEquip((SLOTTYPE)slotID);
            if (PItem == nullptr || !PItem->isType(ITEM_EQUIPMENT))
            {
                continue;
            }

            if (PItem->getReqLvl() > (map_config.disable_gear_scaling ? PChar->GetMLevel() : PChar->jobs.job[PChar->GetMJob()]))
            {
                UnequipItem(PChar, slotID);
                continue;
            }

            if (slotID == SLOT_SUB && !PItem->IsShield())
            {
                // Unequip if no main weapon or a non-grip subslot without DW
                if (!PChar->getEquip(SLOT_MAIN) || (!charutils::hasTrait(PChar, TRAIT_DUAL_WIELD) && !(((CItemWeapon*)PItem)->getSkillType() == SKILL_NONE)))
                {
                    UnequipItem(PChar, SLOT_SUB);
                    continue;
                }
            }

            if ((PItem->getJobs() & (1 << (PChar->GetMJob() - 1))) && (PItem->getEquipSlotId() & (1 << slotID)))
            {
                continue;
            }

            UnequipItem(PChar, slotID);
        }
        // Unarmed H2H weapon check
        if (!PChar->getEquip(SLOT_MAIN) || !PChar->getEquip(SLOT_MAIN)->isType(ITEM_EQUIPMENT) || PChar->m_Weapons[SLOT_MAIN] == itemutils::GetUnarmedH2HItem())
        {
            CheckUnarmedWeapon(PChar);
        }

        PChar->pushPacket(new CCharAppearancePacket(PChar));

        BuildingCharWeaponSkills(PChar);
        SaveCharEquip(PChar);
        SaveCharLook(PChar);
    }

    void RemoveAllEquipment(CCharEntity* PChar)
    {
        CItemEquipment* PItem = nullptr;

        for (uint8 slotID = 0; slotID < 16; ++slotID)
        {
            PItem = PChar->getEquip((SLOTTYPE)slotID);

            if ((PItem != nullptr) && PItem->isType(ITEM_EQUIPMENT))
            {
                UnequipItem(PChar, slotID);
            }
        }
        // Determines the UnarmedItem to use, since all slots are empty now.
        CheckUnarmedWeapon(PChar);

        BuildingCharWeaponSkills(PChar);
        SaveCharEquip(PChar);
        SaveCharLook(PChar);
    }

    /************************************************************************
     *                                                                       *
     *  Проверяем логику всей экипировки персонажа                           *
     *                                                                       *
     ************************************************************************/

    // позднее нужно будет сделать экипировку в структуре,
    // куда добавить битовое поле, указывающее, в какой ячейке находится экипировка с условием
    // для начала это поле избавит нас от проверки ячеек у персонажей без экипировки с условием

    void CheckEquipLogic(CCharEntity* PChar, SCRIPTTYPE ScriptType, uint32 param)
    {
        if (!(PChar->m_EquipFlag & ScriptType))
        {
            return;
        }

        for (uint8 slotID = 0; slotID < 16; ++slotID)
        {
            CItem* PItem = PChar->getEquip((SLOTTYPE)slotID);

            if ((PItem != nullptr) && PItem->isType(ITEM_EQUIPMENT))
            {
                if (((CItemEquipment*)PItem)->getScriptType() & ScriptType)
                {
                    luautils::OnItemCheck(PChar, PItem, static_cast<ITEMCHECK>(param), nullptr);
                }
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  Load the Characters weapon skill list                                *
     *                                                                       *
     ************************************************************************/

    void BuildingCharWeaponSkills(CCharEntity* PChar)
    {
        memset(&PChar->m_WeaponSkills, 0, sizeof(PChar->m_WeaponSkills));

        CItemWeapon* PItem;
        int          main_ws      = 0;
        int          range_ws     = 0;
        int          main_ws_dyn  = 0;
        int          range_ws_dyn = 0;

        bool isInDynamis = PChar->isInDynamis();

        for (auto&& slot :
             { std::make_tuple(SLOT_MAIN, std::ref(main_ws), std::ref(main_ws_dyn)), std::make_tuple(SLOT_RANGED, std::ref(range_ws), std::ref(range_ws_dyn)) })
        {
            if (PChar->m_Weapons[std::get<0>(slot)])
            {
                PItem = dynamic_cast<CItemWeapon*>(PChar->m_Weapons[std::get<0>(slot)]);

                std::get<1>(slot) = battleutils::GetScaledItemModifier(PChar, PItem, Mod::ADDS_WEAPONSKILL);
                std::get<2>(slot) = battleutils::GetScaledItemModifier(PChar, PItem, Mod::ADDS_WEAPONSKILL_DYN);
            }
        }

        // add in melee ws
        PItem                       = dynamic_cast<CItemWeapon*>(PChar->getEquip(SLOT_MAIN));
        uint8       skill           = PItem ? PItem->getSkillType() : SKILL_HAND_TO_HAND;
        const auto& WeaponSkillList = battleutils::GetWeaponSkills(skill);
        for (auto&& PSkill : WeaponSkillList)
        {
            if (battleutils::CanUseWeaponskill(PChar, PSkill) || PSkill->getID() == main_ws || (isInDynamis && (PSkill->getID() == main_ws_dyn)))
            {
                addWeaponSkill(PChar, PSkill->getID());
            }
        }

        // add in ranged ws
        PItem = dynamic_cast<CItemWeapon*>(PChar->getEquip(SLOT_RANGED));
        if (PItem != nullptr && PItem->isType(ITEM_WEAPON) && PItem->getSkillType() != SKILL_THROWING)
        {
            skill                       = PItem ? PItem->getSkillType() : 0;
            const auto& WeaponSkillList = battleutils::GetWeaponSkills(skill);
            for (auto&& PSkill : WeaponSkillList)
            {
                if ((battleutils::CanUseWeaponskill(PChar, PSkill)) || PSkill->getID() == range_ws || (isInDynamis && (PSkill->getID() == range_ws_dyn)))
                {
                    addWeaponSkill(PChar, PSkill->getID());
                }
            }
        }
    }

    void BuildingCharPetAbilityTable(CCharEntity* PChar, CPetEntity* PPet, uint32 PetID)
    {
        XI_DEBUG_BREAK_IF(PPet == nullptr || PChar == nullptr);

        memset(&PChar->m_PetCommands, 0, sizeof(PChar->m_PetCommands));

        if (PetID == 0)
        { // technically Fire Spirit but we're using this to null the abilities shown
            PChar->pushPacket(new CCharAbilitiesPacket(PChar));
            return;
        }

        if (PChar->GetMJob() == JOB_SMN || PChar->GetSJob() == JOB_SMN)
        {
            std::vector<CAbility*> AbilitiesList = ability::GetAbilities(JOB_SMN);

            for (auto PAbility : AbilitiesList)
            {
                if (PPet->GetMLevel() >= PAbility->getLevel() && PetID >= 8 && PetID <= 20 && CheckAbilityAddtype(PChar, PAbility))
                {
                    if (PetID == 8)
                    {
                        if (PAbility->getID() >= ABILITY_HEALING_RUBY && PAbility->getID() <= ABILITY_SOOTHING_RUBY)
                        {
                            addPetAbility(PChar, PAbility->getID() - ABILITY_HEALING_RUBY);
                        }
                    }
                    else if (PetID >= 9 && PetID <= 15)
                    {
                        if (PAbility->getID() >= (ABILITY_HEALING_RUBY + ((PetID - 8) * 16)) && PAbility->getID() < (ABILITY_HEALING_RUBY + ((PetID - 7) * 16)))
                        {
                            addPetAbility(PChar, PAbility->getID() - ABILITY_HEALING_RUBY);
                        }
                    }
                    else if (PetID == 16)
                    {
                        if (PAbility->getID() >= ABILITY_CAMISADO && PAbility->getID() <= ABILITY_PERFECT_DEFENSE)
                        {
                            addPetAbility(PChar, PAbility->getID() - ABILITY_HEALING_RUBY);
                        }
                    }
                    else if (PetID == 20)
                    {
                        if (PAbility->getID() > ABILITY_SOOTHING_RUBY && PAbility->getID() <= ABILITY_MOONLIT_CHARGE)
                        {
                            addPetAbility(PChar, PAbility->getID() - ABILITY_HEALING_RUBY);
                        }
                    }
                }
            }
        }
        if (PPet->getPetType() == PET_TYPE::JUG_PET)
        {
            auto skillList{ battleutils::GetMobSkillList(PPet->m_MobSkillList) };
            for (auto&& abilityid : skillList)
            {
                addPetAbility(PChar, abilityid - ABILITY_HEALING_RUBY);
            }
        }
        PChar->pushPacket(new CCharAbilitiesPacket(PChar));
    }

    /************************************************************************
     *                                                                       *
     *  Собираем рабочую таблицу способностей персонажа. С нулевым уровнем   *
     *  должны быть 2h способности. По этому условию отсеиваем их для sjob   *
     *                                                                       *
     ************************************************************************/

    void BuildingCharAbilityTable(CCharEntity* PChar)
    {
        std::vector<CAbility*> AbilitiesList;

        memset(&PChar->m_Abilities, 0, sizeof(PChar->m_Abilities));

        AbilitiesList = ability::GetAbilities(PChar->GetMJob());

        for (auto PAbility : AbilitiesList)
        {
            if (PAbility == nullptr)
            {
                continue;
            }

            if (PChar->GetMLevel() >= PAbility->getLevel())
            {
                if (PAbility->getID() < ABILITY_HEALING_RUBY && PAbility->getID() != ABILITY_PET_COMMANDS && CheckAbilityAddtype(PChar, PAbility))
                {
                    addAbility(PChar, PAbility->getID());
                    Charge_t* charge     = ability::GetCharge(PChar, PAbility->getRecastId());
                    auto      chargeTime = 0;
                    auto      maxCharges = 0;
                    if (charge)
                    {
                        chargeTime = charge->chargeTime - PChar->PMeritPoints->GetMeritValue((MERIT_TYPE)charge->merit, PChar);
                        maxCharges = charge->maxCharges;
                    }
                    if (!PChar->PRecastContainer->Has(RECAST_ABILITY, PAbility->getRecastId()))
                    {
                        PChar->PRecastContainer->Add(RECAST_ABILITY, PAbility->getRecastId(), 0, chargeTime, maxCharges);
                    }
                }
            }
            else
            {
                break;
            }
        }

        // To stop a character with no SJob to receive the traits with job = 0 in the DB.
        if (PChar->GetSJob() == JOB_NON)
        {
            return;
        }

        AbilitiesList = ability::GetAbilities(PChar->GetSJob());

        for (auto PAbility : AbilitiesList)
        {
            if (PChar->GetSLevel() >= PAbility->getLevel())
            {
                if (PAbility == nullptr)
                {
                    continue;
                }

                if (PAbility->getLevel() != 0 && PAbility->getID() < ABILITY_HEALING_RUBY)
                {
                    if (PAbility->getID() != ABILITY_PET_COMMANDS && CheckAbilityAddtype(PChar, PAbility) && !(PAbility->getAddType() & ADDTYPE_MAIN_ONLY))
                    {
                        addAbility(PChar, PAbility->getID());
                        Charge_t* charge     = ability::GetCharge(PChar, PAbility->getRecastId());
                        auto      chargeTime = 0;
                        auto      maxCharges = 0;
                        if (charge)
                        {
                            chargeTime = charge->chargeTime - PChar->PMeritPoints->GetMeritValue((MERIT_TYPE)charge->merit, PChar);
                            maxCharges = charge->maxCharges;
                        }
                        if (!PChar->PRecastContainer->Has(RECAST_ABILITY, PAbility->getRecastId()))
                        {
                            PChar->PRecastContainer->Add(RECAST_ABILITY, PAbility->getRecastId(), 0, chargeTime, maxCharges);
                        }
                    }
                }
            }
            else
            {
                break;
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  Собираем рабочую таблицу умений персонажа на основе реальной.        *
     *  Добавляем ограничения, отмечаем умения основной профессии (rank != 0)*
     *                                                                       *
     ************************************************************************/

    void BuildingCharSkillsTable(CCharEntity* PChar)
    {
        MERIT_TYPE skillMerit[] = { MERIT_H2H,
                                    MERIT_DAGGER,
                                    MERIT_SWORD,
                                    MERIT_GSWORD,
                                    MERIT_AXE,
                                    MERIT_GAXE,
                                    MERIT_SCYTHE,
                                    MERIT_POLEARM,
                                    MERIT_KATANA,
                                    MERIT_GKATANA,
                                    MERIT_CLUB,
                                    MERIT_STAFF,
                                    MERIT_AUTOMATON_SKILLS,
                                    MERIT_AUTOMATON_SKILLS,
                                    MERIT_AUTOMATON_SKILLS,
                                    MERIT_ARCHERY,
                                    MERIT_MARKSMANSHIP,
                                    MERIT_THROWING,
                                    MERIT_GUARDING,
                                    MERIT_EVASION,
                                    MERIT_SHIELD,
                                    MERIT_PARRYING,
                                    MERIT_DIVINE,
                                    MERIT_HEALING,
                                    MERIT_ENHANCING,
                                    MERIT_ENFEEBLING,
                                    MERIT_ELEMENTAL,
                                    MERIT_DARK,
                                    MERIT_SUMMONING,
                                    MERIT_NINJITSU,
                                    MERIT_SINGING,
                                    MERIT_STRING,
                                    MERIT_WIND,
                                    MERIT_BLUE,
                                    MERIT_GEO,
                                    MERIT_HANDBELL };

        uint8 meritIndex = 0;

        for (int32 i = 1; i < 48; ++i)
        {
            // ignore unused skills
            if ((i >= 13 && i <= 21) || (i >= 46 && i <= 47))
            {
                PChar->WorkingSkills.skill[i] = 0x8000;
                continue;
            }
            uint16 MaxMSkill  = battleutils::GetMaxSkill((SKILLTYPE)i, PChar->GetMJob(), PChar->GetMLevel());
            uint16 MaxSSkill  = battleutils::GetMaxSkill((SKILLTYPE)i, PChar->GetSJob(), PChar->GetSLevel());
            uint16 skillBonus = 0;

            // apply arts bonuses
            if ((i >= 32 && i <= 35 && PChar->StatusEffectContainer->HasStatusEffect({ EFFECT_LIGHT_ARTS, EFFECT_ADDENDUM_WHITE })) ||
                (i >= 35 && i <= 37 && PChar->StatusEffectContainer->HasStatusEffect({ EFFECT_DARK_ARTS, EFFECT_ADDENDUM_BLACK })))
            {
                uint16 artsSkill    = battleutils::GetMaxSkill(SKILL_ENHANCING_MAGIC, JOB_RDM, PChar->GetMLevel());             // B+ skill
                uint16 skillCapD    = battleutils::GetMaxSkill((SKILLTYPE)i, JOB_SCH, PChar->GetMLevel());                      // D skill cap
                uint16 skillCapE    = battleutils::GetMaxSkill(SKILL_DARK_MAGIC, JOB_RDM, PChar->GetMLevel());                  // E skill cap
                auto   currentSkill = std::clamp<uint16>((PChar->RealSkills.skill[i] / 10), 0, std::max(MaxMSkill, MaxSSkill)); // working skill before bonuses
                uint16 artsBaseline = 0;                                                                                        // Level based baseline to which to raise skills
                uint8  mLevel       = PChar->GetMLevel();
                if (mLevel < 51)
                {
                    artsBaseline = (uint16)(5 + 2.7 * (mLevel - 1));
                }
                else if ((mLevel > 50) && (mLevel < 61))
                {
                    artsBaseline = (uint16)(137 + 4.7 * (mLevel - 50));
                }
                else if ((mLevel > 60) && (mLevel < 71))
                {
                    artsBaseline = (uint16)(184 + 3.7 * (mLevel - 60));
                }
                else if ((mLevel > 70) && (mLevel < 75))
                {
                    artsBaseline = (uint16)(221 + 5.0 * (mLevel - 70));
                }
                else if (mLevel >= 75)
                {
                    artsBaseline = skillCapD + 36;
                }
                if (currentSkill < skillCapE)
                {
                    // If the player's skill is below the E cap
                    // give enough bonus points to raise it to the arts baseline
                    skillBonus += std::max(artsBaseline - currentSkill, 0);
                }
                else if (currentSkill < skillCapD)
                {
                    // if the skill is at or above the E cap but below the D cap
                    // raise it up to the B+ skill cap minus the difference between the current skill rank and the scholar base skill cap (D)
                    // i.e. give a bonus of the difference between the B+ skill cap and the D skill cap
                    skillBonus += std::max((artsSkill - skillCapD), 0);
                }
                else if (currentSkill < artsSkill)
                {
                    // If the player's skill is at or above the D cap but below the B+ cap
                    // give enough bonus points to raise it to the B+ cap
                    skillBonus += std::max(artsSkill - currentSkill, 0);
                }

                if (PChar->StatusEffectContainer->HasStatusEffect({ EFFECT_LIGHT_ARTS, EFFECT_ADDENDUM_WHITE }))
                {
                    skillBonus += PChar->getMod(Mod::LIGHT_ARTS_SKILL);
                }
                else
                {
                    skillBonus += PChar->getMod(Mod::DARK_ARTS_SKILL);
                }
            }
            else if (i >= 22 && i <= 24)
            {
                if (PChar->PAutomaton)
                {
                    MaxMSkill = battleutils::GetMaxSkill(1, PChar->GetMLevel()); // A+ capped down to the Automaton's rating
                }
            }

            skillBonus += PChar->PMeritPoints->GetMeritValue(skillMerit[meritIndex], PChar);
            meritIndex++;

            skillBonus += PChar->getMod(static_cast<Mod>(i + 79));

            PChar->WorkingSkills.rank[i] = battleutils::GetSkillRank((SKILLTYPE)i, PChar->GetMJob());

            if (MaxMSkill != 0)
            {
                auto cap{ PChar->RealSkills.skill[i] / 10 >= MaxMSkill };
                PChar->WorkingSkills.skill[i] = std::max(0, cap ? skillBonus + MaxMSkill : skillBonus + PChar->RealSkills.skill[i] / 10);
                if (cap)
                {
                    PChar->WorkingSkills.skill[i] |= 0x8000;
                }
            }
            else if (MaxSSkill != 0)
            {
                auto cap{ PChar->RealSkills.skill[i] / 10 >= MaxSSkill };
                PChar->WorkingSkills.skill[i] = std::max(0, cap ? skillBonus + MaxSSkill : skillBonus + PChar->RealSkills.skill[i] / 10);
                if (cap)
                {
                    PChar->WorkingSkills.skill[i] |= 0x8000;
                }
            }
            else
            {
                PChar->WorkingSkills.skill[i] = std::max<uint16>(0, skillBonus) | 0x8000;
            }
        }

        for (int32 i = 48; i < 58; ++i)
        {
            PChar->WorkingSkills.skill[i] = (PChar->RealSkills.skill[i] / 10) * 0x20 + PChar->RealSkills.rank[i];

            if ((PChar->RealSkills.rank[i] + 1) * 100 <= PChar->RealSkills.skill[i])
            {
                PChar->WorkingSkills.skill[i] += 0x8000;
            }
        }

        for (int32 i = 58; i < 64; ++i)
        {
            PChar->WorkingSkills.skill[i] = 0xFFFF;
        }
    }

    /************************************************************************
     *                                                                       *
     *                                                                       *
     *                                                                       *
     ************************************************************************/

    void BuildingCharTraitsTable(CCharEntity* PChar)
    {
        for (uint8 i = 0; i < PChar->TraitList.size(); ++i)
        {
            CTrait* PTrait = PChar->TraitList.at(i);
            PChar->delModifier(PTrait->getMod(), PTrait->getValue());
        }
        PChar->TraitList.clear();
        memset(&PChar->m_TraitList, 0, sizeof(PChar->m_TraitList));

        battleutils::AddTraits(PChar, traits::GetTraits(PChar->GetMJob()), PChar->GetMLevel());
        battleutils::AddTraits(PChar, traits::GetTraits(PChar->GetSJob()), PChar->GetSLevel());

        if (PChar->GetMJob() == JOB_BLU || PChar->GetSJob() == JOB_BLU)
        {
            blueutils::CalculateTraits(PChar);
        }

        PChar->delModifier(Mod::MEVA, PChar->m_magicEvasion);

        PChar->m_magicEvasion = battleutils::GetMaxSkill(SKILL_ELEMENTAL_MAGIC, JOB_RDM, PChar->GetMLevel());
        PChar->addModifier(Mod::MEVA, PChar->m_magicEvasion);
    }

    /************************************************************************
     *                                                                       *
     *  Пытаемся увеличить значение умения                                   *
     *                                                                       *
     ************************************************************************/

    void TrySkillUP(CCharEntity* PChar, SKILLTYPE SkillID, uint8 lvl, bool forceSkillUp, bool useSubSkill)
    {
        // This usually happens after a crash
        XI_DEBUG_BREAK_IF(SkillID >= MAX_SKILLTYPE); // выход за пределы допустимых умений

        if (((PChar->WorkingSkills.rank[SkillID] != 0) && !(PChar->WorkingSkills.skill[SkillID] & 0x8000)) || useSubSkill)
        {
            uint16 CurSkill     = PChar->RealSkills.skill[SkillID];
            uint16 MainCapSkill = battleutils::GetMaxSkill(SkillID, PChar->GetMJob(), PChar->GetMLevel());
            uint16 SubCapSkill  = battleutils::GetMaxSkill(SkillID, PChar->GetSJob(), PChar->GetSLevel());
            uint16 MainMaxSkill = battleutils::GetMaxSkill(SkillID, PChar->GetMJob(), std::min(PChar->GetMLevel(), lvl));
            uint16 SubMaxSkill  = battleutils::GetMaxSkill(SkillID, PChar->GetSJob(), std::min(PChar->GetSLevel(), lvl));
            uint16 MaxSkill     = 0;
            uint16 CapSkill     = 0;

            if (useSubSkill)
            {
                if (MainCapSkill > SubCapSkill)
                {
                    CapSkill = MainCapSkill;
                    MaxSkill = MainMaxSkill;
                }
                else
                {
                    CapSkill = SubCapSkill;
                    MaxSkill = SubMaxSkill;
                }
            }
            else
            {
                CapSkill = MainCapSkill;
                MaxSkill = MainMaxSkill;
            }
            // Max skill this victim level will allow.
            // Note this is no longer retail accurate, since now 'decent challenge' mobs allow to cap any skill.

            int16  Diff          = MaxSkill - CurSkill / 10;
            double SkillUpChance = Diff / 5.0 + map_config.skillup_chance_multiplier * (2.0 - log10(1.0 + CurSkill / 100));

            double random = xirand::GetRandomNumber(1.);

            if (SkillUpChance > 0.5)
            {
                SkillUpChance = 0.5;
            }

            // Check for skillup% bonus. https://www.bg-wiki.com/bg/Category:Skill_Up_Food
            // Assuming multiplicative even though rate is already a % because 0.5 + 0.8 would be > 1.
            if ((SkillID >= 1 && SkillID <= 12) || (SkillID >= 25 && SkillID <= 31))
            // if should effect automaton replace the above with: (SkillID >= 1 && SkillID <= 31)
            {
                SkillUpChance *= ((100.f + PChar->getMod(Mod::COMBAT_SKILLUP_RATE)) / 100.f);
            }
            else if (SkillID >= 32 && SkillID <= 44)
            {
                SkillUpChance *= ((100.f + PChar->getMod(Mod::MAGIC_SKILLUP_RATE)) / 100.f);
            }

            if (Diff > 0 && (random < SkillUpChance || forceSkillUp))
            {
                double chance      = 0;
                uint8  SkillAmount = 1;
                uint8  tier        = std::min(1 + (Diff / 5), 5);

                for (uint8 i = 0; i < 4; ++i) // 1 + 4 возможных дополнительных (максимум 5)
                {
                    random = xirand::GetRandomNumber(1.);

                    switch (tier)
                    {
                        case 5:
                            chance = 0.900;
                            break;
                        case 4:
                            chance = 0.700;
                            break;
                        case 3:
                            chance = 0.500;
                            break;
                        case 2:
                            chance = 0.300;
                            break;
                        case 1:
                            chance = 0.200;
                            break;
                        default:
                            chance = 0.000;
                            break;
                    }

                    if (chance < random || SkillAmount == 5)
                    {
                        break;
                    }

                    tier -= 1;
                    SkillAmount += 1;
                }
                // convert to 10th units
                CapSkill = CapSkill * 10;

                int16 rovBonus = 1;
                for (auto i = 2884; i <= 2890; i += 3) // RHAPSODY KI
                {
                    if (hasKeyItem(PChar, i))
                    {
                        rovBonus += 1;
                    }
                    else
                    {
                        break; // No need to check further as you can't get KI out of order, so break out.
                    }
                }
                SkillAmount *= rovBonus;
                if (SkillAmount > 9)
                {
                    SkillAmount = 9;
                }

                // Do skill amount multiplier (Will only be applied if default setting is changed)
                if (map_config.skillup_amount_multiplier > 1)
                {
                    SkillAmount += (uint8)(SkillAmount * map_config.skillup_amount_multiplier);
                    if (SkillAmount > 9)
                    {
                        SkillAmount = 9;
                    }
                }

                if (SkillAmount + CurSkill >= CapSkill)
                {
                    // skill is capped. set blue flag
                    SkillAmount = CapSkill - CurSkill;
                    PChar->WorkingSkills.skill[SkillID] |= 0x8000;
                }

                PChar->RealSkills.skill[SkillID] += SkillAmount;
                PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, SkillID, SkillAmount, 38));

                if ((CurSkill / 10) < (CurSkill + SkillAmount) / 10) // if gone up a level
                {
                    PChar->WorkingSkills.skill[SkillID] += 1;
                    PChar->pushPacket(new CCharSkillsPacket(PChar));
                    PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, SkillID, (CurSkill + SkillAmount) / 10, 53));

                    CheckWeaponSkill(PChar, SkillID);
                    /* ignoring this for now
                    if (SkillID >= 1 && SkillID <= 12)
                    {
                    PChar->addModifier(Mod::ATT, 1);
                    PChar->addModifier(Mod::ACC, 1);
                    }
                    */
                }
                SaveCharSkills(PChar, SkillID);
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  When skill level gained check for weapon skill                       *
     *                                                                       *
     ************************************************************************/

    void CheckWeaponSkill(CCharEntity* PChar, uint8 skill)
    {
        auto* weapon = dynamic_cast<CItemWeapon*>(PChar->m_Weapons[SLOT_MAIN]);
        if (!weapon || weapon->getSkillType() != skill)
        {
            return;
        }
        const auto& WeaponSkillList = battleutils::GetWeaponSkills(skill);
        uint16      curSkill        = PChar->RealSkills.skill[skill] / 10;

        for (auto&& PSkill : WeaponSkillList)
        {
            if (curSkill == PSkill->getSkillLevel() && (battleutils::CanUseWeaponskill(PChar, PSkill)))
            {
                addWeaponSkill(PChar, PSkill->getID());
                PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, PSkill->getID(), PSkill->getID(), 45));
                PChar->pushPacket(new CCharAbilitiesPacket(PChar));
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  Методы для работы с ключевыми предметами                             *
     *                                                                       *
     ************************************************************************/

    bool hasKeyItem(CCharEntity* PChar, uint16 KeyItemID)
    {
        auto table = KeyItemID / 512;
        return PChar->keys.tables[table].keyList[KeyItemID % 512];
    }

    bool seenKeyItem(CCharEntity* PChar, uint16 KeyItemID)
    {
        auto table = KeyItemID / 512;
        return PChar->keys.tables[table].seenList[KeyItemID % 512];
    }

    void unseenKeyItem(CCharEntity* PChar, uint16 KeyItemID)
    {
        auto table                                          = KeyItemID / 512;
        PChar->keys.tables[table].seenList[KeyItemID % 512] = false;
    }

    void addKeyItem(CCharEntity* PChar, uint16 KeyItemID)
    {
        auto table                                         = KeyItemID / 512;
        PChar->keys.tables[table].keyList[KeyItemID % 512] = true;
    }

    void delKeyItem(CCharEntity* PChar, uint16 KeyItemID)
    {
        auto table                                         = KeyItemID / 512;
        PChar->keys.tables[table].keyList[KeyItemID % 512] = false;
    }

    /************************************************************************
     *                                                                       *
     *  Методы для работы с заклинаниями                                     *
     *                                                                       *
     ************************************************************************/

    int32 hasSpell(CCharEntity* PChar, uint16 SpellID)
    {
        return PChar->m_SpellList[SpellID];
    }

    int32 addSpell(CCharEntity* PChar, uint16 SpellID)
    {
        if (!hasSpell(PChar, SpellID))
        {
            PChar->m_SpellList[SpellID] = true;
            return 1;
        }
        return 0;
    }

    int32 delSpell(CCharEntity* PChar, uint16 SpellID)
    {
        if (hasSpell(PChar, SpellID))
        {
            PChar->m_SpellList[SpellID] = false;
            return 1;
        }
        return 0;
    }

    /************************************************************************
     *                                                                       *
     *  Learned abilities (corsair rolls)                                    *
     *                                                                       *
     ************************************************************************/

    int32 hasLearnedAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return hasBit(AbilityID, PChar->m_LearnedAbilities, sizeof(PChar->m_LearnedAbilities));
    }

    int32 addLearnedAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return addBit(AbilityID, PChar->m_LearnedAbilities, sizeof(PChar->m_LearnedAbilities));
    }

    int32 delLearnedAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return delBit(AbilityID, PChar->m_LearnedAbilities, sizeof(PChar->m_LearnedAbilities));
    }

    /************************************************************************
     *                                                                       *
     *  Learned weaponskills                                                 *
     *                                                                       *
     ************************************************************************/

    bool hasLearnedWeaponskill(CCharEntity* PChar, uint8 wsid)
    {
        return PChar->m_LearnedWeaponskills[wsid];
    }

    void addLearnedWeaponskill(CCharEntity* PChar, uint8 wsid)
    {
        PChar->m_LearnedWeaponskills[wsid] = true;
    }

    void delLearnedWeaponskill(CCharEntity* PChar, uint8 wsid)
    {
        PChar->m_LearnedWeaponskills[wsid] = false;
    }

    /************************************************************************
     *                                                                       *
     *  Методы для работы со званиями                                        *
     *                                                                       *
     ************************************************************************/

    int32 hasTitle(CCharEntity* PChar, uint16 Title)
    {
        return hasBit(Title, PChar->m_TitleList, sizeof(PChar->m_TitleList));
    }

    int32 addTitle(CCharEntity* PChar, uint16 Title)
    {
        return addBit(Title, PChar->m_TitleList, sizeof(PChar->m_TitleList));
    }

    int32 delTitle(CCharEntity* PChar, uint16 Title)
    {
        return delBit(Title, PChar->m_TitleList, sizeof(PChar->m_TitleList));
    }

    void setTitle(CCharEntity* PChar, uint16 Title)
    {
        PChar->profile.title = Title;
        PChar->pushPacket(new CCharStatsPacket(PChar));

        addTitle(PChar, Title);
        SaveTitles(PChar);
    }

    /************************************************************************
     *                                                                       *
     *  Методы для работы с основными способностями                          *
     *                                                                       *
     ************************************************************************/

    int32 hasAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return hasBit(AbilityID, PChar->m_Abilities, sizeof(PChar->m_Abilities));
    }

    int32 addAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return addBit(AbilityID, PChar->m_Abilities, sizeof(PChar->m_Abilities));
    }

    int32 delAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return delBit(AbilityID, PChar->m_Abilities, sizeof(PChar->m_Abilities));
    }

    /************************************************************************
     *                                                                       *
     *  Weapon Skill functions                                               *
     *                                                                       *
     ************************************************************************/

    int32 hasWeaponSkill(CCharEntity* PChar, uint16 WeaponSkillID)
    {
        return hasBit(WeaponSkillID, PChar->m_WeaponSkills, sizeof(PChar->m_WeaponSkills));
    }

    int32 addWeaponSkill(CCharEntity* PChar, uint16 WeaponSkillID)
    {
        return addBit(WeaponSkillID, PChar->m_WeaponSkills, sizeof(PChar->m_WeaponSkills));
    }

    int32 delWeaponSkill(CCharEntity* PChar, uint16 WeaponSkillID)
    {
        return delBit(WeaponSkillID, PChar->m_WeaponSkills, sizeof(PChar->m_WeaponSkills));
    }

    /************************************************************************
     *                                                                       *
     *  Trait Functions                                                      *
     *                                                                       *
     ************************************************************************/

    int32 hasTrait(CCharEntity* PChar, uint8 TraitID)
    {
        if (PChar->objtype != TYPE_PC)
        {
            ShowError("charutils::hasTrait Attempt to reference a trait from a non-character entity: %s %i", PChar->name.c_str(), PChar->id);
            return 0;
        }
        return hasBit(TraitID, PChar->m_TraitList, sizeof(PChar->m_TraitList));
    }

    int32 addTrait(CCharEntity* PChar, uint8 TraitID)
    {
        if (PChar->objtype != TYPE_PC)
        {
            ShowError("charutils::addTrait Attempt to reference a trait from a non-character entity: %s %i", PChar->name.c_str(), PChar->id);
            return 0;
        }
        return addBit(TraitID, PChar->m_TraitList, sizeof(PChar->m_TraitList));
    }

    int32 delTrait(CCharEntity* PChar, uint8 TraitID)
    {
        if (PChar->objtype != TYPE_PC)
        {
            ShowError("charutils::delTrait Attempt to reference a trait from a non-character entity: %s %i", PChar->name.c_str(), PChar->id);
            return 0;
        }
        return delBit(TraitID, PChar->m_TraitList, sizeof(PChar->m_TraitList));
    }

    /************************************************************************
     *
     *       Pet Command Functions
     *
     *************************************************************************/
    int32 hasPetAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return hasBit(AbilityID, PChar->m_PetCommands, sizeof(PChar->m_PetCommands));
    }

    int32 addPetAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return addBit(AbilityID, PChar->m_PetCommands, sizeof(PChar->m_PetCommands));
    }

    int32 delPetAbility(CCharEntity* PChar, uint16 AbilityID)
    {
        return delBit(AbilityID, PChar->m_PetCommands, sizeof(PChar->m_PetCommands));
    }

    /************************************************************************
     *                                                                       *
     *  Инициализируем таблицу опыта                                         *
     *                                                                       *
     ************************************************************************/

    void LoadExpTable()
    {
        const char* fmtQuery = "SELECT r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20 "
                               "FROM exp_table "
                               "ORDER BY level ASC "
                               "LIMIT %u";

        int32 ret = Sql_Query(SqlHandle, fmtQuery, ExpTableRowCount);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
        {
            for (uint32 x = 0; x < ExpTableRowCount && Sql_NextRow(SqlHandle) == SQL_SUCCESS; ++x)
            {
                for (uint32 y = 0; y < 20; ++y)
                {
                    g_ExpTable[x][y] = (uint16)Sql_GetIntData(SqlHandle, y);
                }
            }
        }

        ret = Sql_Query(SqlHandle, "SELECT level, exp FROM exp_base LIMIT 100;");

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0)
        {
            while (Sql_NextRow(SqlHandle) == SQL_SUCCESS)
            {
                uint8 level = (uint8)Sql_GetIntData(SqlHandle, 0) - 1;

                if (level < 100)
                {
                    g_ExpPerLevel[level] = (uint16)Sql_GetIntData(SqlHandle, 1);
                }
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  Returns mob difficulty according to level difference                 *
     *                                                                       *
     ************************************************************************/
    EMobDifficulty CheckMob(uint8 charlvl, uint8 moblvl)
    {
        uint32 baseExp = GetRealExp(charlvl, moblvl);

        if (baseExp >= 400)
        {
            return EMobDifficulty::IncrediblyTough;
        }
        if (baseExp >= 350)
        {
            return EMobDifficulty::VeryTough;
        }
        if (baseExp >= 220)
        {
            return EMobDifficulty::Tough;
        }
        if (baseExp >= 200)
        {
            return EMobDifficulty::EvenMatch;
        }
        if (baseExp >= 160)
        {
            return EMobDifficulty::DecentChallenge;
        }
        if (baseExp >= 60)
        {
            return EMobDifficulty::EasyPrey;
        }
        if (baseExp >= 14)
        {
            return EMobDifficulty::IncrediblyEasyPrey;
        }

        return EMobDifficulty::TooWeak;
    }

    /************************************************************************
     *                                                                       *
     *  Узнаем реальное количество опыта, который персонаж получит с цели    *
     *                                                                       *
     ************************************************************************/

    uint32 GetRealExp(uint8 charlvl, uint8 moblvl)
    {
        const int32 levelDif = moblvl - charlvl + 44;

        if ((charlvl > 0) && (charlvl < 100))
        {
            return g_ExpTable[std::clamp(levelDif, 0, ExpTableRowCount - 1)][(charlvl - 1) / 5];
        }

        return 0;
    }

    /************************************************************************
     *                                                                       *
     *  Узнаем количество опыта, необходимое для получения следующего уровня *
     *                                                                       *
     ************************************************************************/

    uint32 GetExpNEXTLevel(uint8 charlvl)
    {
        if ((charlvl > 0) && (charlvl < 100))
        {
            return g_ExpPerLevel[charlvl];
        }
        return 0;
    }

    /************************************************************************
     *                                                                       *
     *  Distributes gil to party members.                                    *
     *                                                                       *
     ************************************************************************/

    // TODO: REALISATION MUST BE IN TREASUREPOOL

    void DistributeGil(CCharEntity* PChar, CMobEntity* PMob)
    {
        // work out the amount of gil to give (guessed; replace with testing)
        uint32 gil    = PMob->GetRandomGil();
        uint32 gBonus = 0;

        if (gil && map_config.mob_gil_multiplier >= 0)
        {
            gil = static_cast<uint32>(gil * map_config.mob_gil_multiplier);
        }

        if (map_config.all_mobs_gil_bonus > 0)
        {
            gBonus = map_config.all_mobs_gil_bonus * PMob->GetMLevel();
            gil += std::clamp<uint32>(gBonus, 1, map_config.max_gil_bonus);
        }

        // Distribute gil to player/party/alliance
        if (PChar->PParty != nullptr)
        {
            std::vector<CCharEntity*> members;

            // First gather all valid party members
            PChar->ForAlliance([PMob, &members](CBattleEntity* PPartyMember) {
                if (PPartyMember->getZone() == PMob->getZone() && distanceSquared(PPartyMember->loc.p, PMob->loc.p) < square(100.f))
                {
                    members.push_back((CCharEntity*)PPartyMember);
                }
            });

            // all members might not be in range
            if (!members.empty())
            {
                // distribute gil
                int32 gilPerPerson = static_cast<int32>(gil / members.size());
                for (auto* PMember : members)
                {
                    // Check for gilfinder
                    gilPerPerson += gilPerPerson * PMember->getMod(Mod::GILFINDER) / 100;
                    UpdateItem(PMember, LOC_INVENTORY, 0, gilPerPerson);
                    PMember->pushPacket(new CMessageBasicPacket(PMember, PMember, gilPerPerson, 0, 565));
                }
            }
        }
        else if (distanceSquared(PChar->loc.p, PMob->loc.p) < square(100.f))
        {
            // Check for gilfinder
            gil += gil * PChar->getMod(Mod::GILFINDER) / 100;
            UpdateItem(PChar, LOC_INVENTORY, 0, static_cast<int32>(gil));
            PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, static_cast<int32>(gil), 0, 565));
        }
    }

    void DistributeItem(CCharEntity* PChar, CBaseEntity* PEntity, uint16 itemid, uint16 droprate)
    {
        uint8 tries    = 0;
        uint8 maxTries = 1;
        uint8 bonus    = 0;
        if (auto* PMob = dynamic_cast<CMobEntity*>(PEntity))
        {
            // THLvl is the number of 'extra chances' at an item. If the item is obtained, then break out.
            tries    = 0;
            maxTries = 1 + (PMob->m_THLvl > 2 ? 2 : PMob->m_THLvl);
            bonus    = (PMob->m_THLvl > 2 ? (PMob->m_THLvl - 2) * 10 : 0);
        }
        while (tries < maxTries)
        {
            if (droprate > 0 && xirand::GetRandomNumber(1000) < droprate * map_config.drop_rate_multiplier + bonus)
            {
                PChar->PTreasurePool->AddItem(itemid, PEntity);
                break;
            }
            tries++;
        }
    }

    /************************************************************************
     *                                                                       *
     *  Allocate experience points                                           *
     *                                                                       *
     ************************************************************************/

    void DistributeExperiencePoints(CCharEntity* PChar, CMobEntity* PMob)
    {
        uint8       pcinzone = 0;
        uint8       minlevel = 0;
        uint8       maxlevel = PChar->GetMLevel();
        REGION_TYPE region   = PChar->loc.zone->GetRegionID();

        if (PChar->PParty)
        {
            if (PChar->PParty->GetSyncTarget())
            {
                if (distance(PMob->loc.p, PChar->PParty->GetSyncTarget()->loc.p) >= 100 || PChar->PParty->GetSyncTarget()->health.hp == 0)
                {
                    PChar->ForParty([&PMob](CBattleEntity* PMember) {
                        if (PMember->getZone() == PMob->getZone() && distance(PMember->loc.p, PMob->loc.p) < 100)
                        {
                            if (CCharEntity* PChar = dynamic_cast<CCharEntity*>(PMember))
                            {
                                PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, 0, 0, 545));
                            }
                        }
                    });

                    return;
                }
            }
        }

        PChar->ForAlliance([&pcinzone, &PMob, &minlevel, &maxlevel](CBattleEntity* PMember) {
            if (PMember->getZone() == PMob->getZone() && distance(PMember->loc.p, PMob->loc.p) < 100)
            {
                if (PMember->PPet != nullptr && PMember->PPet->GetMLevel() > maxlevel && PMember->PPet->objtype != TYPE_PET)
                {
                    maxlevel = PMember->PPet->GetMLevel();
                }
                if (PMember->GetMLevel() > maxlevel)
                {
                    maxlevel = PMember->GetMLevel();
                }
                else if (PMember->GetMLevel() < minlevel)
                {
                    minlevel = PMember->GetMLevel();
                }
                pcinzone++;
            }
        });
        pcinzone            = std::max(pcinzone, PMob->m_HiPartySize);
        maxlevel            = std::max(maxlevel, PMob->m_HiPCLvl);
        PMob->m_HiPartySize = pcinzone;
        PMob->m_HiPCLvl     = maxlevel;

        PChar->ForAlliance([&PMob, &region, &minlevel, &maxlevel, &pcinzone](CBattleEntity* PPartyMember) {
            CCharEntity* PMember = dynamic_cast<CCharEntity*>(PPartyMember);
            if (!PMember || PMember->isDead())
            {
                return;
            }

            bool chainactive = false;

            const uint8 moblevel    = PMob->GetMLevel();
            const uint8 memberlevel = PMember->GetMLevel();

            EMobDifficulty mobCheck = CheckMob(maxlevel, moblevel);
            float          exp      = (float)GetRealExp(maxlevel, moblevel);

            if (mobCheck > EMobDifficulty::TooWeak)
            {
                if (PMember->getZone() == PMob->getZone())
                {
                    if (map_config.exp_party_gap_penalties == 1)
                    {
                        if (maxlevel > 50 || maxlevel > (memberlevel + 7))
                        {
                            exp *= memberlevel / (float)maxlevel;
                        }
                        else
                        {
                            exp *= GetExpNEXTLevel(memberlevel) / (float)GetExpNEXTLevel(maxlevel);
                        }
                    }

                    if (PMember->StatusEffectContainer->HasStatusEffect(EFFECT_SIGNET) && region >= REGION_TYPE::RONFAURE && region <= REGION_TYPE::JEUNO)
                    {
                        switch (pcinzone)
                        {
                            case 1:
                                exp *= 1.00f;
                                break;
                            case 2:
                                exp *= 0.75f;
                                break;
                            case 3:
                                exp *= 0.55f;
                                break;
                            case 4:
                                exp *= 0.45f;
                                break;
                            case 5:
                                exp *= 0.39f;
                                break;
                            case 6:
                                exp *= 0.35f;
                                break;
                            default:
                                exp *= (1.8f / pcinzone);
                                break;
                        }
                    }
                    else if (PMember->StatusEffectContainer->HasStatusEffect(EFFECT_SANCTION) && region >= REGION_TYPE::WEST_AHT_URHGAN &&
                             region <= REGION_TYPE::ALZADAAL)
                    {
                        switch (pcinzone)
                        {
                            case 1:
                                exp *= 1.00f;
                                break;
                            case 2:
                                exp *= 0.75f;
                                break;
                            case 3:
                                exp *= 0.55f;
                                break;
                            case 4:
                                exp *= 0.45f;
                                break;
                            case 5:
                                exp *= 0.39f;
                                break;
                            case 6:
                                exp *= 0.35f;
                                break;
                            default:
                                exp *= (1.8f / pcinzone);
                                break;
                        }
                    }
                    else
                    {
                        switch (pcinzone)
                        {
                            case 1:
                                exp *= 1.00f;
                                break;
                            case 2:
                                exp *= 0.60f;
                                break;
                            case 3:
                                exp *= 0.45f;
                                break;
                            case 4:
                                exp *= 0.40f;
                                break;
                            case 5:
                                exp *= 0.37f;
                                break;
                            case 6:
                                exp *= 0.35f;
                                break;
                            default:
                                exp *= (1.8f / pcinzone);
                                break;
                        }
                    }

                    if (PMob->getMobMod(MOBMOD_EXP_BONUS))
                    {
                        const float monsterbonus = 1.f + PMob->getMobMod(MOBMOD_EXP_BONUS) / 100.f;
                        exp *= monsterbonus;
                    }

                    // Per monster caps pulled from: https://ffxiclopedia.fandom.com/wiki/Experience_Points
                    if (PMember->GetMLevel() <= 50)
                    {
                        exp = std::fmin(exp, 400.f);
                    }
                    else if (PMember->GetMLevel() <= 60)
                    {
                        exp = std::fmin(exp, 500.f);
                    }
                    else
                    {
                        exp = std::fmin(exp, 600.f);
                    }

                    if (mobCheck > EMobDifficulty::DecentChallenge)
                    {
                        if (PMember->expChain.chainTime > gettick() || PMember->expChain.chainTime == 0)
                        {
                            chainactive = true;
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    exp *= 1.0f;
                                    break;
                                case 1:
                                    exp *= 1.2f;
                                    break;
                                case 2:
                                    exp *= 1.25f;
                                    break;
                                case 3:
                                    exp *= 1.3f;
                                    break;
                                case 4:
                                    exp *= 1.4f;
                                    break;
                                case 5:
                                    exp *= 1.5f;
                                    break;
                                default:
                                    exp *= 1.55f;
                                    break;
                            }
                        }
                        else
                        {
                            if (PMember->GetMLevel() <= 10)
                            {
                                PMember->expChain.chainTime = gettick() + 50000;
                            }
                            else if (PMember->GetMLevel() <= 20)
                            {
                                PMember->expChain.chainTime = gettick() + 100000;
                            }
                            else if (PMember->GetMLevel() <= 30)
                            {
                                PMember->expChain.chainTime = gettick() + 150000;
                            }
                            else if (PMember->GetMLevel() <= 40)
                            {
                                PMember->expChain.chainTime = gettick() + 200000;
                            }
                            else if (PMember->GetMLevel() <= 50)
                            {
                                PMember->expChain.chainTime = gettick() + 250000;
                            }
                            else if (PMember->GetMLevel() <= 60)
                            {
                                PMember->expChain.chainTime = gettick() + 300000;
                            }
                            else
                            {
                                PMember->expChain.chainTime = gettick() + 360000;
                            }
                            PMember->expChain.chainNumber = 1;
                        }

                        if (chainactive && PMember->GetMLevel() <= 10)
                        {
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    PMember->expChain.chainTime = gettick() + 50000;
                                    break;
                                case 1:
                                    PMember->expChain.chainTime = gettick() + 40000;
                                    break;
                                case 2:
                                    PMember->expChain.chainTime = gettick() + 30000;
                                    break;
                                case 3:
                                    PMember->expChain.chainTime = gettick() + 20000;
                                    break;
                                case 4:
                                    PMember->expChain.chainTime = gettick() + 10000;
                                    break;
                                case 5:
                                    PMember->expChain.chainTime = gettick() + 6000;
                                    break;
                                default:
                                    PMember->expChain.chainTime = gettick() + 2000;
                                    break;
                            }
                        }
                        else if (chainactive && PMember->GetMLevel() <= 20)
                        {
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    PMember->expChain.chainTime = gettick() + 100000;
                                    break;
                                case 1:
                                    PMember->expChain.chainTime = gettick() + 80000;
                                    break;
                                case 2:
                                    PMember->expChain.chainTime = gettick() + 60000;
                                    break;
                                case 3:
                                    PMember->expChain.chainTime = gettick() + 40000;
                                    break;
                                case 4:
                                    PMember->expChain.chainTime = gettick() + 20000;
                                    break;
                                case 5:
                                    PMember->expChain.chainTime = gettick() + 8000;
                                    break;
                                default:
                                    PMember->expChain.chainTime = gettick() + 4000;
                                    break;
                            }
                        }
                        else if (chainactive && PMember->GetMLevel() <= 30)
                        {
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    PMember->expChain.chainTime = gettick() + 150000;
                                    break;
                                case 1:
                                    PMember->expChain.chainTime = gettick() + 120000;
                                    break;
                                case 2:
                                    PMember->expChain.chainTime = gettick() + 90000;
                                    break;
                                case 3:
                                    PMember->expChain.chainTime = gettick() + 60000;
                                    break;
                                case 4:
                                    PMember->expChain.chainTime = gettick() + 30000;
                                    break;
                                case 5:
                                    PMember->expChain.chainTime = gettick() + 10000;
                                    break;
                                default:
                                    PMember->expChain.chainTime = gettick() + 5000;
                                    break;
                            }
                        }
                        else if (chainactive && PMember->GetMLevel() <= 40)
                        {
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    PMember->expChain.chainTime = gettick() + 200000;
                                    break;
                                case 1:
                                    PMember->expChain.chainTime = gettick() + 160000;
                                    break;
                                case 2:
                                    PMember->expChain.chainTime = gettick() + 120000;
                                    break;
                                case 3:
                                    PMember->expChain.chainTime = gettick() + 80000;
                                    break;
                                case 4:
                                    PMember->expChain.chainTime = gettick() + 40000;
                                    break;
                                case 5:
                                    PMember->expChain.chainTime = gettick() + 40000;
                                    break;
                                default:
                                    PMember->expChain.chainTime = gettick() + 30000;
                                    break;
                            }
                        }
                        else if (chainactive && PMember->GetMLevel() <= 50)
                        {
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    PMember->expChain.chainTime = gettick() + 250000;
                                    break;
                                case 1:
                                    PMember->expChain.chainTime = gettick() + 200000;
                                    break;
                                case 2:
                                    PMember->expChain.chainTime = gettick() + 150000;
                                    break;
                                case 3:
                                    PMember->expChain.chainTime = gettick() + 100000;
                                    break;
                                case 4:
                                    PMember->expChain.chainTime = gettick() + 50000;
                                    break;
                                case 5:
                                    PMember->expChain.chainTime = gettick() + 50000;
                                    break;
                                default:
                                    PMember->expChain.chainTime = gettick() + 50000;
                                    break;
                            }
                        }
                        else if (chainactive && PMember->GetMLevel() <= 60)
                        {
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    PMember->expChain.chainTime = gettick() + 300000;
                                    break;
                                case 1:
                                    PMember->expChain.chainTime = gettick() + 240000;
                                    break;
                                case 2:
                                    PMember->expChain.chainTime = gettick() + 180000;
                                    break;
                                case 3:
                                    PMember->expChain.chainTime = gettick() + 120000;
                                    break;
                                case 4:
                                    PMember->expChain.chainTime = gettick() + 90000;
                                    break;
                                case 5:
                                    PMember->expChain.chainTime = gettick() + 60000;
                                    break;
                                default:
                                    PMember->expChain.chainTime = gettick() + 60000;
                                    break;
                            }
                        }
                        else if (chainactive)
                        {
                            switch (PMember->expChain.chainNumber)
                            {
                                case 0:
                                    PMember->expChain.chainTime = gettick() + 360000;
                                    break;
                                case 1:
                                    PMember->expChain.chainTime = gettick() + 300000;
                                    break;
                                case 2:
                                    PMember->expChain.chainTime = gettick() + 240000;
                                    break;
                                case 3:
                                    PMember->expChain.chainTime = gettick() + 165000;
                                    break;
                                case 4:
                                    PMember->expChain.chainTime = gettick() + 105000;
                                    break;
                                case 5:
                                    PMember->expChain.chainTime = gettick() + 60000;
                                    break;
                                default:
                                    PMember->expChain.chainTime = gettick() + 60000;
                                    break;
                            }
                        }
                    }
                    // pet or companion exp penalty needs to be added here
                    if (distance(PMember->loc.p, PMob->loc.p) > 100)
                    {
                        PMember->pushPacket(new CMessageBasicPacket(PMember, PMember, 0, 0, 37));
                        return;
                    }

                    exp = charutils::AddExpBonus(PMember, exp);

                    charutils::AddExperiencePoints(false, PMember, PMob, (uint32)exp, mobCheck, chainactive);
                }
            }
        });
    }

    /************************************************************************
     *                                                                       *
     *  Allocate capacity points                                             *
     *                                                                       *
     ************************************************************************/

    void DistributeCapacityPoints(CCharEntity* PChar, CMobEntity* PMob)
    {
        // TODO: Capacity Points cannot be gained in Abyssea or Reives.  In addition, Gates areas,
        //       Ra'Kaznar, Escha, and Reisenjima reduce party penalty for capacity points earned.
        ZONEID zone     = PChar->loc.zone->GetID();
        uint8  mobLevel = PMob->GetMLevel();

        PChar->ForAlliance([&PMob, &zone, &mobLevel](CBattleEntity* PPartyMember) {
            CCharEntity* PMember = dynamic_cast<CCharEntity*>(PPartyMember);

            if (!PMember || PMember->isDead() || (PMember->loc.zone->GetID() != zone))
            {
                // Do not grant Capacity points if null, Dead, or in a different area
                return;
            }

            if (!hasKeyItem(PMember, 2544) || PMember->GetMLevel() < 99)
            {
                // Do not grant Capacity points without Job Breaker or Level 99
                return;
            }

            bool  chainActive = false;
            int16 levelDiff   = mobLevel - 99; // Passed previous 99 check, no need to calculate

            // Capacity Chains are only granted for Mobs level 100+
            // Ref: https://www.bg-wiki.com/ffxi/Job_Points
            float capacityPoints = 0;

            if (mobLevel > 99)
            {
                // Base Capacity Point formula derived from the table located at:
                // https://ffxiclopedia.fandom.com/wiki/Job_Points#Capacity_Points
                capacityPoints = 0.0089 * (levelDiff ^ 3) + 0.0533 * (levelDiff ^ 2) + 3.7439 * levelDiff + 89.7;

                if (PMember->capacityChain.chainTime > gettick() || PMember->capacityChain.chainTime == 0)
                {
                    chainActive = true;

                    // TODO: Needs verification, pulled from: https://www.bluegartr.com/threads/120445-Job-Points-discussion?p=6138288&viewfull=1#post6138288
                    // Assumption: Chain0 is no bonus, Chains 10+ capped at 1.5 value, f(chain) = 1 + 0.05 * chain
                    float chainModifier = std::min(1 + 0.05 * PMember->capacityChain.chainNumber, 1.5);
                    capacityPoints *= chainModifier;
                }
                else
                {
                    // TODO: Capacity Chain Timer is reduced after Chain 30
                    PMember->capacityChain.chainTime   = gettick() + 30000;
                    PMember->capacityChain.chainNumber = 1;
                }

                if (chainActive)
                {
                    PMember->capacityChain.chainTime = gettick() + 30000;
                }

                capacityPoints = AddCapacityBonus(PMember, capacityPoints);
                AddCapacityPoints(PMember, PMob, capacityPoints, levelDiff, chainActive);
            }
        });
    }

    /************************************************************************
    *                                                                       *
    *  Return adjusted Capacity point value based on bonuses                *
    *  Note: rawBonus uses whole number percentage values until returning   *
    *                                                                       *
    ************************************************************************/

    uint16 AddCapacityBonus(CCharEntity* PChar, uint16 capacityPoints)
    {
        float rawBonus = 0;

        // Mod::CAPACITY_BONUS is currently used for JP Gifts, and can easily be used elsewhere
        // This value is stored as uint, as a whole number percentage value
        rawBonus += PChar->getMod(Mod::CAPACITY_BONUS);

        // Unity Concord Ranking: 2 * (Unity Ranking - 1)
        uint8 unity = PChar->profile.unity_leader;
        if (unity >= 1 && unity <= 11)
        {
            rawBonus += 2 * (roeutils::RoeSystem.unityLeaderRank[unity - 1] - 1);
        }

        // RoE Objectives (There might be a formulaic way to do some of these)
        // Nation Mission Completion (10%)
        for (uint16 nationRecord = 1332; nationRecord <= 1372; nationRecord += 20)
        {
            if (roeutils::GetEminenceRecordCompletion(PChar, nationRecord))
            {
                rawBonus += 10;
            }
        }

        // RoV Key Items - Fuchsia, Puce, Ochre (30%)
        for (uint16 rovKeyItem = 2890; rovKeyItem <= 2892; rovKeyItem++)
        {
            if (hasKeyItem(PChar, rovKeyItem))
            {
                rawBonus += 30;
            }
        }

        capacityPoints *= 1.f + rawBonus / 100;
        return capacityPoints;
    }

    /************************************************************************
    *                                                                       *
    *  Add Capacity Points to an individual player                          *
    *                                                                       *
    ************************************************************************/

    void AddCapacityPoints(CCharEntity* PChar, CBaseEntity* PMob, uint32 capacityPoints, int16 levelDiff, bool isCapacityChain)
    {
        if (PChar->isDead())
        {
            return;
        }

        capacityPoints = (uint32)(capacityPoints * map_config.exp_rate);

        uint16 currentCapacity = PChar->PJobPoints->GetCapacityPoints();

        if (capacityPoints > 0)
        {
            // Capacity Chains start at lv100 mobs
            if (levelDiff >= 1 && isCapacityChain)
            {
                if (PChar->capacityChain.chainNumber != 0)
                {
                    PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, capacityPoints, PChar->capacityChain.chainNumber, 735));
                }
                else
                {
                    PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, capacityPoints, 0, 718));
                }
                PChar->capacityChain.chainNumber++;
            }
            else
            {
                PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, capacityPoints, 0, 718));
            }

            // Add capacity points
            if (PChar->PJobPoints->AddCapacityPoints(capacityPoints))
            {
                PChar->loc.zone->PushPacket(PChar, CHAR_INRANGE_SELF, new CMessageCombatPacket(PChar, PMob, PChar->PJobPoints->GetJobPoints(), 0, 719));
            }
            PChar->pushPacket(new CMenuJobPointsPacket(PChar));

            if (PMob != PChar) // Only mob kills count for gain EXP records
            {
                roeutils::event(ROE_EXPGAIN, PChar, RoeDatagram("capacity", capacityPoints));
            }
        }
    }

    /************************************************************************
     *                                                                       *
     *  Losing exp on death. retainPercent is the amount of exp to be        *
     *  saved on death, e.g. 0.05 = retain 5% of lost exp. A value of        *
     *  1 means no exp loss. A value of 0 means full exp loss.               *
     *                                                                       *
     ************************************************************************/
    void DelExperiencePoints(CCharEntity* PChar, float retainPercent, uint16 forcedXpLoss)
    {
        XI_DEBUG_BREAK_IF(retainPercent > 1.0f || retainPercent < 0.0f);
        XI_DEBUG_BREAK_IF(map_config.exp_loss_level > 99 || map_config.exp_loss_level < 1);

        if (PChar->GetMLevel() < map_config.exp_loss_level && forcedXpLoss == 0)
        {
            return;
        }

        uint8  mLevel  = (PChar->m_LevelRestriction != 0 && PChar->m_LevelRestriction < PChar->GetMLevel()) ? PChar->m_LevelRestriction : PChar->GetMLevel();
        uint16 exploss = mLevel <= 67 ? (GetExpNEXTLevel(mLevel) * 8) / 100 : 2400;

        if (forcedXpLoss > 0)
        {
            // Override normal XP loss with specified value.
            exploss = forcedXpLoss;
        }
        else
        {
            // Apply retention percent
            exploss = (uint16)(exploss * (1 - retainPercent));
            exploss = (uint16)(exploss * map_config.exp_loss_rate);
        }

        if (PChar->jobs.exp[PChar->GetMJob()] < exploss)
        {
            if (PChar->jobs.job[PChar->GetMJob()] > 1)
            {
                // de-level!
                int32 lowerLevelMaxExp = GetExpNEXTLevel(PChar->jobs.job[PChar->GetMJob()] - 1);
                exploss -= PChar->jobs.exp[PChar->GetMJob()];
                PChar->jobs.exp[PChar->GetMJob()] = std::max(0, lowerLevelMaxExp - exploss);
                PChar->jobs.job[PChar->GetMJob()] -= 1;

                if (PChar->m_LevelRestriction == 0 || PChar->jobs.job[PChar->GetMJob()] < PChar->m_LevelRestriction)
                {
                    PChar->SetMLevel(PChar->jobs.job[PChar->GetMJob()]);
                    PChar->SetSLevel(PChar->jobs.job[PChar->GetSJob()]);
                }

                jobpointutils::RefreshGiftMods(PChar);
                BuildingCharSkillsTable(PChar);
                CalculateStats(PChar);
                CheckValidEquipment(PChar);

                BuildingCharAbilityTable(PChar);
                BuildingCharTraitsTable(PChar);
                BuildingCharWeaponSkills(PChar);

                PChar->pushPacket(new CCharJobsPacket(PChar));
                PChar->pushPacket(new CCharUpdatePacket(PChar));
                PChar->pushPacket(new CCharSkillsPacket(PChar));
                PChar->pushPacket(new CCharRecastPacket(PChar));
                PChar->pushPacket(new CCharAbilitiesPacket(PChar));
                PChar->pushPacket(new CMenuMeritPacket(PChar));
                PChar->pushPacket(new CCharJobExtraPacket(PChar, true));
                PChar->pushPacket(new CCharJobExtraPacket(PChar, false));
                PChar->pushPacket(new CCharSyncPacket(PChar));

                PChar->UpdateHealth();

                SaveCharStats(PChar);
                SaveCharJob(PChar, PChar->GetMJob());

                if (PChar->PParty != nullptr)
                {
                    if (PChar->PParty->GetSyncTarget() == PChar)
                    {
                        PChar->PParty->RefreshSync();
                    }
                    PChar->PParty->ReloadParty();
                }

                PChar->loc.zone->PushPacket(PChar, CHAR_INRANGE_SELF, new CMessageCombatPacket(PChar, PChar, PChar->jobs.job[PChar->GetMJob()], 0, 11));
                luautils::OnPlayerLevelDown(PChar);
                PChar->updatemask |= UPDATE_HP;
            }
            else
            {
                PChar->jobs.exp[PChar->GetMJob()] = 0;
            }
        }
        else
        {
            PChar->jobs.exp[PChar->GetMJob()] -= exploss;
        }

        SaveCharExp(PChar, PChar->GetMJob());
        PChar->pushPacket(new CCharStatsPacket(PChar));
    }

    /************************************************************************
     *                                                                       *
     *  Добавляем очки опытка указанному персонажу                           *
     *                                                                       *
     ************************************************************************/

    void AddExperiencePoints(bool expFromRaise, CCharEntity* PChar, CBaseEntity* PMob, uint32 exp, EMobDifficulty mobCheck, bool isexpchain)
    {
        if (PChar->isDead())
        {
            return;
        }

        if (!expFromRaise)
        {
            exp = (uint32)(exp * map_config.exp_rate);
        }
        uint16 currentExp  = PChar->jobs.exp[PChar->GetMJob()];
        bool   onLimitMode = false;

        // Incase player de-levels to 74 on the field
        if (PChar->MeritMode && PChar->jobs.job[PChar->GetMJob()] > 74 && !expFromRaise)
        {
            onLimitMode = true;
        }

        // we check if the player is level capped and max exp..
        if (PChar->jobs.job[PChar->GetMJob()] > 74 && PChar->jobs.job[PChar->GetMJob()] >= PChar->jobs.genkai &&
            PChar->jobs.exp[PChar->GetMJob()] == GetExpNEXTLevel(PChar->jobs.job[PChar->GetMJob()]) - 1)
        {
            onLimitMode = true;
        }

        // exp added from raise shouldn't display a message. Don't need a message for zero exp either
        if (!expFromRaise && exp > 0)
        {
            if (mobCheck >= EMobDifficulty::EvenMatch && isexpchain)
            {
                if (PChar->expChain.chainNumber != 0)
                {
                    if (onLimitMode)
                    {
                        PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, exp, PChar->expChain.chainNumber, 372));
                    }
                    else
                    {
                        PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, exp, PChar->expChain.chainNumber, 253));
                    }
                }
                else
                {
                    if (onLimitMode)
                    {
                        PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, exp, 0, 371));
                    }
                    else
                    {
                        PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, exp, 0, 8));
                    }
                }
                PChar->expChain.chainNumber++;
            }
            else if (exp > 0)
            {
                if (onLimitMode)
                {
                    PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, exp, 0, 371));
                }
                else
                {
                    PChar->pushPacket(new CMessageCombatPacket(PChar, PChar, exp, 0, 8));
                }
            }
        }

        if (onLimitMode)
        {
            // add limit points
            if (PChar->PMeritPoints->AddLimitPoints(exp))
            {
                PChar->loc.zone->PushPacket(PChar, CHAR_INRANGE_SELF, new CMessageCombatPacket(PChar, PMob, PChar->PMeritPoints->GetMeritPoints(), 0, 50));
            }
        }
        else
        {
            // add normal exp
            PChar->jobs.exp[PChar->GetMJob()] += exp;
        }

        if (!expFromRaise)
        {
            REGION_TYPE region = PChar->loc.zone->GetRegionID();

            // Should this user be awarded conquest points..
            if (PChar->StatusEffectContainer->HasStatusEffect(EFFECT_SIGNET) && (region >= REGION_TYPE::RONFAURE && region <= REGION_TYPE::JEUNO))
            {
                // Add influence for the players region..
                conquest::AddConquestPoints(PChar, exp);
            }

            // Should this user be awarded imperial standing..
            if (PChar->StatusEffectContainer->HasStatusEffect(EFFECT_SANCTION) && (region >= REGION_TYPE::WEST_AHT_URHGAN && region <= REGION_TYPE::ALZADAAL))
            {
                charutils::AddPoints(PChar, "imperial_standing", (int32)(exp * 0.1f));
                PChar->pushPacket(new CConquestPacket(PChar));
            }

            // Cruor Drops in Abyssea zones.
            uint16 Pzone = PChar->getZone();
            if (zoneutils::GetCurrentRegion(Pzone) == REGION_TYPE::ABYSSEA)
            {
                uint16 TextID = luautils::GetTextIDVariable(Pzone, "CRUOR_OBTAINED");
                uint32 Total  = charutils::GetPoints(PChar, "cruor");
                uint32 Cruor  = 0; // Need to work out how to do cruor chains, until then no cruor will drop unless this line is customized for non retail play.

                if (TextID == 0)
                {
                    ShowWarning("Failed to fetch Cruor Message ID for zone: %i", Pzone);
                }

                if (Cruor >= 1)
                {
                    PChar->pushPacket(new CMessageSpecialPacket(PChar, TextID, Cruor, Total + Cruor, 0, 0));
                    charutils::AddPoints(PChar, "cruor", Cruor);
                }
            }
        }

        PChar->PAI->EventHandler.triggerListener("EXPERIENCE_POINTS", CLuaBaseEntity(PChar), exp);

        // Player levels up
        if ((currentExp + exp) >= GetExpNEXTLevel(PChar->jobs.job[PChar->GetMJob()]) && !onLimitMode)
        {
            if (PChar->jobs.job[PChar->GetMJob()] >= PChar->jobs.genkai)
            {
                PChar->jobs.exp[PChar->GetMJob()] = GetExpNEXTLevel(PChar->jobs.job[PChar->GetMJob()]) - 1;
                if (PChar->PParty && PChar->PParty->GetSyncTarget() == PChar)
                {
                    PChar->PParty->SetSyncTarget(nullptr, 556);
                }
            }
            else
            {
                PChar->jobs.exp[PChar->GetMJob()] -= GetExpNEXTLevel(PChar->jobs.job[PChar->GetMJob()]);
                if (PChar->jobs.exp[PChar->GetMJob()] >= GetExpNEXTLevel(PChar->jobs.job[PChar->GetMJob()] + 1))
                {
                    PChar->jobs.exp[PChar->GetMJob()] = GetExpNEXTLevel(PChar->jobs.job[PChar->GetMJob()] + 1) - 1;
                }
                PChar->jobs.job[PChar->GetMJob()] += 1;

                if (PChar->m_LevelRestriction == 0 || PChar->m_LevelRestriction > PChar->GetMLevel())
                {
                    PChar->SetMLevel(PChar->jobs.job[PChar->GetMJob()]);
                    PChar->SetSLevel(PChar->jobs.job[PChar->GetSJob()]);

                    jobpointutils::RefreshGiftMods(PChar);
                    BuildingCharSkillsTable(PChar);
                    CalculateStats(PChar);
                    BuildingCharAbilityTable(PChar);
                    BuildingCharTraitsTable(PChar);
                    BuildingCharWeaponSkills(PChar);
                    if (PChar->PAutomaton != nullptr && PChar->PAutomaton != PChar->PPet)
                    {
                        puppetutils::LoadAutomatonStats(PChar);
                    }
                }
                PChar->PLatentEffectContainer->CheckLatentsJobLevel();

                if (PChar->PParty != nullptr)
                {
                    if (PChar->PParty->GetSyncTarget() == PChar)
                    {
                        PChar->PParty->RefreshSync();
                    }
                    PChar->PParty->ReloadParty();
                }

                PChar->UpdateHealth();

                PChar->health.hp = PChar->GetMaxHP();
                PChar->health.mp = PChar->GetMaxMP();

                SaveCharStats(PChar);
                SaveCharJob(PChar, PChar->GetMJob());
                SaveCharExp(PChar, PChar->GetMJob());

                PChar->pushPacket(new CCharJobsPacket(PChar));
                PChar->pushPacket(new CCharUpdatePacket(PChar));
                PChar->pushPacket(new CCharSkillsPacket(PChar));
                PChar->pushPacket(new CCharRecastPacket(PChar));
                PChar->pushPacket(new CCharAbilitiesPacket(PChar));
                PChar->pushPacket(new CMenuMeritPacket(PChar));
                PChar->pushPacket(new CCharJobExtraPacket(PChar, true));
                PChar->pushPacket(new CCharJobExtraPacket(PChar, true));
                PChar->pushPacket(new CCharSyncPacket(PChar));

                PChar->loc.zone->PushPacket(PChar, CHAR_INRANGE_SELF, new CMessageCombatPacket(PChar, PMob, PChar->jobs.job[PChar->GetMJob()], 0, 9));
                PChar->pushPacket(new CCharStatsPacket(PChar));

                luautils::OnPlayerLevelUp(PChar);
                roeutils::event(ROE_EVENT::ROE_LEVELUP, PChar, RoeDatagramList{});
                PChar->updatemask |= UPDATE_HP;
                return;
            }
        }

        SaveCharStats(PChar);
        SaveCharJob(PChar, PChar->GetMJob());
        SaveCharExp(PChar, PChar->GetMJob());
        PChar->pushPacket(new CCharStatsPacket(PChar));

        if (onLimitMode)
        {
            PChar->pushPacket(new CMenuMeritPacket(PChar));
        }

        if (PMob != PChar) // Only mob kills count for gain EXP records
        {
            roeutils::event(ROE_EXPGAIN, PChar, RoeDatagram("exp", exp));
        }
    }

    /************************************************************************
     *                                                                       *
     *  Устанавливаем ограничение уровня персонажа                           *
     *                                                                       *
     ************************************************************************/

    void SetLevelRestriction(CCharEntity* PChar, uint8 lvl)
    {
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем позицию                                                    *
     *                                                                       *
     ************************************************************************/

    void SaveCharPosition(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars "
                            "SET "
                            "pos_rot = %u,"
                            "pos_x = %.3f,"
                            "pos_y = %.3f,"
                            "pos_z = %.3f,"
                            "boundary = %u "
                            "WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, PChar->loc.p.rotation, PChar->loc.p.x, PChar->loc.p.y, PChar->loc.p.z, PChar->loc.boundary, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем список квестов                                             *
     *                                                                       *
     ************************************************************************/

    void SaveQuestsList(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars "
                            "SET "
                            "quests = '%s' "
                            "WHERE charid = %u;";

        char questslist[sizeof(PChar->m_questLog) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, questslist, (const char*)PChar->m_questLog, sizeof(PChar->m_questLog));

        Sql_Query(SqlHandle, Query, questslist, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем список квестов                                             *
     *                                                                       *
     ************************************************************************/

    void SaveFame(CCharEntity* PChar)
    {
        const char* Query = "UPDATE char_profile "
                            "SET "
                            "fame_sandoria = %u,"
                            "fame_bastok = %u,"
                            "fame_windurst = %u,"
                            "fame_norg = %u,"
                            "fame_jeuno = %u,"
                            "fame_aby_konschtat = %u,"
                            "fame_aby_tahrongi = %u,"
                            "fame_aby_latheine = %u,"
                            "fame_aby_misareaux = %u,"
                            "fame_aby_vunkerl = %u,"
                            "fame_aby_attohwa = %u,"
                            "fame_aby_altepa = %u,"
                            "fame_aby_grauberg = %u,"
                            "fame_aby_uleguerand = %u,"
                            "fame_adoulin = %u "
                            "WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, PChar->profile.fame[0], PChar->profile.fame[1], PChar->profile.fame[2], PChar->profile.fame[3], PChar->profile.fame[4],
                  PChar->profile.fame[5], PChar->profile.fame[6], PChar->profile.fame[7], PChar->profile.fame[8], PChar->profile.fame[9],
                  PChar->profile.fame[10], PChar->profile.fame[11], PChar->profile.fame[12], PChar->profile.fame[13], PChar->profile.fame[14], PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Save Character Missions                                              *
     *                                                                       *
     ************************************************************************/

    void SaveMissionsList(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars "
                            "LEFT JOIN char_profile USING(charid) "
                            "SET "
                            "missions = '%s',"
                            "assault = '%s',"
                            "campaign = '%s',"
                            "rank_points = %u,"
                            "rank_sandoria = %u,"
                            "rank_bastok = %u,"
                            "rank_windurst = %u "
                            "WHERE charid = %u;";

        char missionslist[sizeof(PChar->m_missionLog) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, missionslist, (const char*)PChar->m_missionLog, sizeof(PChar->m_missionLog));

        char assaultList[sizeof(PChar->m_assaultLog) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, assaultList, (const char*)&PChar->m_assaultLog, sizeof(PChar->m_assaultLog));

        char campaignList[sizeof(PChar->m_campaignLog) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, campaignList, (const char*)&PChar->m_campaignLog, sizeof(PChar->m_campaignLog));

        Sql_Query(SqlHandle, Query, missionslist, assaultList, campaignList, PChar->profile.rankpoints, PChar->profile.rank[0], PChar->profile.rank[1],
                  PChar->profile.rank[2], PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Save Eminence Records                                                *
     *                                                                       *
     ************************************************************************/

    void SaveEminenceData(CCharEntity* PChar)
    {
        if (!roeutils::RoeSystem.RoeEnabled)
        {
            return;
        }

        const char* Query = "UPDATE chars "
                            "SET "
                            "eminence = '%s' "
                            "WHERE charid = %u;";

        char eminenceList[sizeof(PChar->m_eminenceLog) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, eminenceList, (const char*)&PChar->m_eminenceLog, sizeof(PChar->m_eminenceLog));

        Sql_Query(SqlHandle, Query, eminenceList, PChar->id);
        PChar->m_eminenceCache.lastWriteout = static_cast<uint32>(time(nullptr));
    }

    /************************************************************************
     *                                                                       *
     *  Cохраняем список колючевых предметов                                 *
     *                                                                       *
     ************************************************************************/

    void SaveCharInventoryCapacity(CCharEntity* PChar)
    {
        const char* Query = "UPDATE char_storage "
                            "SET "
                            "inventory = %u,"
                            "safe = %u,"
                            "locker = %u,"
                            "satchel = %u,"
                            "sack = %u, "
                            "`case` = %u, "
                            "wardrobe = %u, "
                            "wardrobe2 = %u, "
                            "wardrobe3 = %u, "
                            "wardrobe4 = %u "
                            "WHERE charid = %u";

        Sql_Query(SqlHandle, Query, PChar->getStorage(LOC_INVENTORY)->GetSize(), PChar->getStorage(LOC_MOGSAFE)->GetSize(),
                  PChar->getStorage(LOC_MOGLOCKER)->GetSize(), PChar->getStorage(LOC_MOGSATCHEL)->GetSize(), PChar->getStorage(LOC_MOGSACK)->GetSize(),
                  PChar->getStorage(LOC_MOGCASE)->GetSize(), PChar->getStorage(LOC_WARDROBE)->GetSize(), PChar->getStorage(LOC_WARDROBE2)->GetSize(),
                  PChar->getStorage(LOC_WARDROBE3)->GetSize(), PChar->getStorage(LOC_WARDROBE4)->GetSize(), PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Cохраняем список колючевых предметов                                 *
     *                                                                       *
     ************************************************************************/

    void SaveKeyItems(CCharEntity* PChar)
    {
        const char* fmtQuery = "UPDATE chars SET keyitems = '%s' WHERE charid = %u;";

        char keyitems[sizeof(PChar->keys) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, keyitems, (const char*)&PChar->keys, sizeof(PChar->keys));

        Sql_Query(SqlHandle, fmtQuery, keyitems, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем список выученных заклинаний                                *
     *                                                                       *
     ************************************************************************/

    void SaveSpell(CCharEntity* PChar, uint16 spellID)
    {
        const char* Query = "INSERT IGNORE INTO char_spells "
                            "VALUES (%u, %u);";

        Sql_Query(SqlHandle, Query, PChar->id, spellID);
    }

    void DeleteSpell(CCharEntity* PChar, uint16 spellID)
    {
        const char* Query = "DELETE FROM char_spells "
                            "WHERE charid = %u AND spellid = %u;";

        Sql_Query(SqlHandle, Query, PChar->id, spellID);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем список выученных заклинаний                                *
     *                                                                       *
     ************************************************************************/

    void SaveLearnedAbilities(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars SET "
                            "abilities = '%s', "
                            "weaponskills = '%s' "
                            "WHERE charid = %u;";

        char abilities[sizeof(PChar->m_LearnedAbilities) * 2 + 1];
        char weaponskills[sizeof(PChar->m_LearnedWeaponskills) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, abilities, (const char*)PChar->m_LearnedAbilities, sizeof(PChar->m_LearnedAbilities));
        Sql_EscapeStringLen(SqlHandle, weaponskills, (const char*)&PChar->m_LearnedWeaponskills, sizeof(PChar->m_LearnedWeaponskills));

        Sql_Query(SqlHandle, Query, abilities, weaponskills, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем список званий                                              *
     *                                                                       *
     ************************************************************************/

    void SaveTitles(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars "
                            "LEFT JOIN char_stats USING(charid) "
                            "SET "
                            "titles = '%s',"
                            "title = %u "
                            "WHERE charid = %u";

        char titles[sizeof(PChar->m_TitleList) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, titles, (const char*)PChar->m_TitleList, sizeof(PChar->m_TitleList));

        Sql_Query(SqlHandle, Query, titles, PChar->profile.title, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем список посещенных зон                                      *
     *                                                                       *
     ************************************************************************/

    void SaveZonesVisited(CCharEntity* PChar)
    {
        const char* fmtQuery = "UPDATE chars SET zones = '%s' WHERE charid = %u;";

        char zones[sizeof(PChar->m_ZonesList) * 2 + 1];
        Sql_EscapeStringLen(SqlHandle, zones, (const char*)PChar->m_ZonesList, sizeof(PChar->m_ZonesList));

        Sql_Query(SqlHandle, fmtQuery, zones, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем экипировку и внешний вид персонажа.                        *
     *                                                                       *
     ************************************************************************/

    void SaveCharEquip(CCharEntity* PChar)
    {
        for (uint8 i = 0; i < 18; ++i)
        {
            if (PChar->equip[i] == 0)
            {
                Sql_Query(SqlHandle, "DELETE FROM char_equip WHERE charid = %u AND  equipslotid = %u LIMIT 1;", PChar->id, i);
            }
            else
            {
                const char* fmtQuery = "INSERT INTO char_equip SET charid = %u, equipslotid = %u , slotid  = %u, containerid = %u ON DUPLICATE KEY UPDATE "
                                       "slotid  = %u, containerid = %u;";
                Sql_Query(SqlHandle, fmtQuery, PChar->id, i, PChar->equip[i], PChar->equipLoc[i], PChar->equip[i], PChar->equipLoc[i]);
            }
        }
    }

    void SaveCharLook(CCharEntity* PChar)
    {
        const char* Query = "UPDATE char_look "
                            "SET head = %u, body = %u, hands = %u, legs = %u, feet = %u, main = %u, sub = %u, ranged = %u "
                            "WHERE charid = %u;";

        look_t* look = (PChar->getStyleLocked() ? &PChar->mainlook : &PChar->look);
        Sql_Query(SqlHandle, Query, look->head, look->body, look->hands, look->legs, look->feet, look->main, look->sub, look->ranged, PChar->id);

        Sql_Query(SqlHandle, "UPDATE chars SET isstylelocked = %u WHERE charid = %u;", PChar->getStyleLocked() ? 1 : 0, PChar->id);

        Query = "INSERT INTO char_style (charid, head, body, hands, legs, feet, main, sub, ranged) "
                "VALUES (%u, %u, %u, %u, %u, %u, %u, %u, %u) ON DUPLICATE KEY UPDATE "
                "charid = VALUES(charid), head = VALUES(head), body = VALUES(body), "
                "hands = VALUES(hands), legs = VALUES(legs), feet = VALUES(feet), "
                "main = VALUES(main), sub = VALUES(sub), ranged = VALUES(ranged);";

        Sql_Query(SqlHandle, Query, PChar->id, PChar->styleItems[SLOT_HEAD], PChar->styleItems[SLOT_BODY], PChar->styleItems[SLOT_HANDS],
                  PChar->styleItems[SLOT_LEGS], PChar->styleItems[SLOT_FEET], PChar->styleItems[SLOT_MAIN], PChar->styleItems[SLOT_SUB],
                  PChar->styleItems[SLOT_RANGED]);
    }

    /************************************************************************
     *                                                                       *
     *  Save some of the current characteristics of the character            *
     *                                                                       *
     ************************************************************************/

    void SaveCharStats(CCharEntity* PChar)
    {
        const char* Query = "UPDATE char_stats "
                            "SET hp = %u, mp = %u, nameflags = %u, mhflag = %u, mjob = %u, sjob = %u, "
                            "pet_id = %u, pet_type = %u, pet_hp = %u, pet_mp = %u "
                            "WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, PChar->health.hp, PChar->health.mp, PChar->nameflags.flags, PChar->profile.mhflag, PChar->GetMJob(), PChar->GetSJob(),
                  PChar->petZoningInfo.petID, static_cast<uint8>(PChar->petZoningInfo.petType), PChar->petZoningInfo.petHP, PChar->petZoningInfo.petMP, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Save the char's GM level and nameflags                               *
     *                                                                       *
     ************************************************************************/

    void SaveCharGMLevel(CCharEntity* PChar)
    {
        const char* Query = "UPDATE %s SET %s %u WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, "chars", "gmlevel =", PChar->m_GMlevel, PChar->id);
        Sql_Query(SqlHandle, Query, "char_stats", "nameflags =", PChar->nameflags.flags, PChar->id);
    }

    void SaveMentorFlag(CCharEntity* PChar)
    {
        const char* Query = "UPDATE %s SET %s %u WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, "chars", "mentor =", PChar->m_mentorUnlocked, PChar->id);
    }

    void SaveJobMasterDisplay(CCharEntity* PChar)
    {
        const char* Query = "UPDATE %s SET %s %u WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, "chars", "job_master =", PChar->m_jobMasterDisplay, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Save the char's menu config flags                                    *
     *                                                                       *
     ************************************************************************/
    void SaveMenuConfigFlags(CCharEntity* PChar)
    {
        const char* Query = "UPDATE %s SET %s %u WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, "chars", "nnameflags =", PChar->menuConfigFlags.flags, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Saves character nation changes                                       *
     *                                                                       *
     ************************************************************************/

    void SaveCharNation(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars "
                            "SET nation = %u "
                            "WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, PChar->profile.nation, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Saves characters current campaign allegiance                         *
     *                                                                       *
     ************************************************************************/

    void SaveCampaignAllegiance(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars "
                            "SET campaign_allegiance = %u "
                            "WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, PChar->profile.campaign_allegiance, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Saves character's current moghancement
     *                                                                       *
     ************************************************************************/

    void SaveCharMoghancement(CCharEntity* PChar)
    {
        const char* Query = "UPDATE chars "
                            "SET moghancement = %u "
                            "WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, PChar->m_moghancementID, PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем текущие уровни профессий персонажа                         *
     *                                                                       *
     ************************************************************************/

    void SaveCharJob(CCharEntity* PChar, JOBTYPE job)
    {
        XI_DEBUG_BREAK_IF(job == JOB_NON || job >= MAX_JOBTYPE);

        const char* fmtQuery;

        switch (job)
        {
            case JOB_WAR:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, war = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_MNK:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, mnk = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_WHM:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, whm = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_BLM:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, blm = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_RDM:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, rdm = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_THF:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, thf = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_PLD:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, pld = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_DRK:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, drk = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_BST:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, bst = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_BRD:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, brd = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_RNG:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, rng = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_SAM:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, sam = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_NIN:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, nin = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_DRG:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, drg = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_SMN:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, smn = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_BLU:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, blu = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_COR:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, cor = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_PUP:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, pup = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_DNC:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, dnc = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_SCH:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, sch = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_GEO:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, geo = %u WHERE charid = %u LIMIT 1";
                break;
            case JOB_RUN:
                fmtQuery = "UPDATE char_jobs SET unlocked = %u, run = %u WHERE charid = %u LIMIT 1";
                break;
            default:
                fmtQuery = "";
                break;
        }
        Sql_Query(SqlHandle, fmtQuery, PChar->jobs.unlocked, PChar->jobs.job[job], PChar->id);

        if (PChar->isNewPlayer() && PChar->jobs.job[job] >= 5)
        {
            PChar->menuConfigFlags.flags &= ~NFLAG_NEWPLAYER;
            PChar->updatemask |= UPDATE_HP;
            SaveMenuConfigFlags(PChar);
        }
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем текущий опыт персонажа                                     *
     *                                                                       *
     ************************************************************************/

    void SaveCharExp(CCharEntity* PChar, JOBTYPE job)
    {
        XI_DEBUG_BREAK_IF(job == JOB_NON || job >= MAX_JOBTYPE);

        const char* Query;

        switch (job)
        {
            case JOB_WAR:
                Query = "UPDATE char_exp SET war = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_MNK:
                Query = "UPDATE char_exp SET mnk = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_WHM:
                Query = "UPDATE char_exp SET whm = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_BLM:
                Query = "UPDATE char_exp SET blm = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_RDM:
                Query = "UPDATE char_exp SET rdm = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_THF:
                Query = "UPDATE char_exp SET thf = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_PLD:
                Query = "UPDATE char_exp SET pld = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_DRK:
                Query = "UPDATE char_exp SET drk = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_BST:
                Query = "UPDATE char_exp SET bst = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_BRD:
                Query = "UPDATE char_exp SET brd = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_RNG:
                Query = "UPDATE char_exp SET rng = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_SAM:
                Query = "UPDATE char_exp SET sam = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_NIN:
                Query = "UPDATE char_exp SET nin = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_DRG:
                Query = "UPDATE char_exp SET drg = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_SMN:
                Query = "UPDATE char_exp SET smn = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_BLU:
                Query = "UPDATE char_exp SET blu = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_COR:
                Query = "UPDATE char_exp SET cor = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_PUP:
                Query = "UPDATE char_exp SET pup = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_DNC:
                Query = "UPDATE char_exp SET dnc = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_SCH:
                Query = "UPDATE char_exp SET sch = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_GEO:
                Query = "UPDATE char_exp SET geo = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            case JOB_RUN:
                Query = "UPDATE char_exp SET run = %u, merits = %u, limits = %u WHERE charid = %u";
                break;
            default:
                Query = "";
                break;
        }
        Sql_Query(SqlHandle, Query, PChar->jobs.exp[job], PChar->PMeritPoints->GetMeritPoints(), PChar->PMeritPoints->GetLimitPoints(), PChar->id);
    }

    /************************************************************************
     *                                                                       *
     *  Сохраняем значение выбранного умения персонажа                       *
     *                                                                       *
     ************************************************************************/

    void SaveCharSkills(CCharEntity* PChar, uint8 SkillID)
    {
        XI_DEBUG_BREAK_IF(SkillID >= MAX_SKILLTYPE);

        const char* Query = "INSERT INTO char_skills "
                            "SET "
                            "charid = %u,"
                            "skillid = %u,"
                            "value = %u,"
                            "rank = %u "
                            "ON DUPLICATE KEY UPDATE value = %u, rank = %u;";

        Sql_Query(SqlHandle, Query, PChar->id, SkillID, PChar->RealSkills.skill[SkillID], PChar->RealSkills.rank[SkillID], PChar->RealSkills.skill[SkillID],
                  PChar->RealSkills.rank[SkillID]);
    }

    /************************************************************************
     *                                                                       *
     *  Save Teleports - Homepoints, outposts, maws, etc                     *
     *                                                                       *
     ************************************************************************/

    void SaveTeleport(CCharEntity* PChar, TELEPORT_TYPE type)
    {
        const char* column = "";
        uint32      value  = 0;

        switch (type)
        {
            case TELEPORT_TYPE::OUTPOST_SANDY:
                column = "outpost_sandy";
                value  = PChar->teleport.outpostSandy;
                break;
            case TELEPORT_TYPE::OUTPOST_BASTOK:
                column = "outpost_bastok";
                value  = PChar->teleport.outpostBastok;
                break;
            case TELEPORT_TYPE::OUTPOST_WINDY:
                column = "outpost_windy";
                value  = PChar->teleport.outpostWindy;
                break;
            case TELEPORT_TYPE::RUNIC_PORTAL:
                column = "runic_portal";
                value  = PChar->teleport.runicPortal;
                break;
            case TELEPORT_TYPE::PAST_MAW:
                column = "maw";
                value  = PChar->teleport.pastMaw;
                break;
            case TELEPORT_TYPE::CAMPAIGN_SANDY:
                column = "campaign_sandy";
                value  = PChar->teleport.campaignSandy;
                break;
            case TELEPORT_TYPE::CAMPAIGN_BASTOK:
                column = "campaign_bastok";
                value  = PChar->teleport.campaignBastok;
                break;
            case TELEPORT_TYPE::CAMPAIGN_WINDY:
                column = "campaign_windy";
                value  = PChar->teleport.campaignWindy;
                break;
            case TELEPORT_TYPE::HOMEPOINT:
            {
                char buf[sizeof(PChar->teleport.homepoint) * 2 + 1];
                Sql_EscapeStringLen(SqlHandle, buf, (const char*)&PChar->teleport.homepoint, sizeof(PChar->teleport.homepoint));
                const char* query = "UPDATE char_unlocks SET homepoints = '%s' WHERE charid = %u;";
                Sql_Query(SqlHandle, query, buf, PChar->id);
                return;
            }
            case TELEPORT_TYPE::SURVIVAL:
            {
                char buf[sizeof(PChar->teleport.survival) * 2 + 1];
                Sql_EscapeStringLen(SqlHandle, buf, (const char*)&PChar->teleport.survival, sizeof(PChar->teleport.survival));
                const char* query = "UPDATE char_unlocks SET survivals = '%s' WHERE charid = %u;";
                Sql_Query(SqlHandle, query, buf, PChar->id);
                return;
            }
            default:
                ShowError("charutils:SaveTeleport : Unknown type parameter.");
                return;
        }

        const char* query = "UPDATE char_unlocks SET %s = %u WHERE charid = %u;";
        Sql_Query(SqlHandle, query, column, value, PChar->id);
    }

    float AddExpBonus(CCharEntity* PChar, float exp)
    {
        int32 bonus = 0;
        if (PChar->StatusEffectContainer->GetStatusEffect(EFFECT_DEDICATION))
        {
            CStatusEffect* dedication = PChar->StatusEffectContainer->GetStatusEffect(EFFECT_DEDICATION);
            int16          percentage = dedication->GetPower();
            int16          cap        = dedication->GetSubPower();
            bonus += std::clamp<int32>((int32)((exp * percentage) / 100), 0, cap);
            dedication->SetSubPower(cap -= bonus);

            if (cap <= 0)
            {
                PChar->StatusEffectContainer->DelStatusEffect(EFFECT_DEDICATION);
            }
        }

        int16 rovBonus = 0;
        for (auto i = 2884; i <= 2889; ++i) // RHAPSODY KI are sequential, so start at WHITE and end at MAUVE, last 3 are CP
        {
            if (hasKeyItem(PChar, i))
            {
                rovBonus += 30;
            }
            else
            {
                break; // No need to check further as you can't get KI out of order, so break out.
            }
        }

        bonus += (int32)(exp * ((PChar->getMod(Mod::EXP_BONUS) + rovBonus) / 100.0f));

        if (bonus + (int32)exp < 0)
        {
            exp = 0;
        }
        else
        {
            exp = exp + bonus;
        }

        return exp;
    }

    bool hasMogLockerAccess(CCharEntity* PChar)
    {
        char fmtQuery[] = "SELECT value FROM char_vars WHERE charid = %u AND varname = '%s' ";
        Sql_Query(SqlHandle, fmtQuery, PChar->id, "mog-locker-expiry-timestamp");

        if (Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            auto tstamp = (uint32)Sql_GetIntData(SqlHandle, 0);
            if (CVanaTime::getInstance()->getVanaTime() < tstamp)
            {
                return true;
            }
        }
        return false;
    }

    /************************************************************************
     *                                                                       *
     *                                                                       *
     *                                                                       *
     ************************************************************************/

    uint16 AvatarPerpetuationReduction(CCharEntity* PChar)
    {
        uint16 reduction = PChar->getMod(Mod::PERPETUATION_REDUCTION);

        static const Mod strong[8] = { Mod::FIRE_AFFINITY_PERP, Mod::ICE_AFFINITY_PERP, Mod::WIND_AFFINITY_PERP, Mod::EARTH_AFFINITY_PERP,
                                       Mod::THUNDER_AFFINITY_PERP, Mod::WATER_AFFINITY_PERP, Mod::LIGHT_AFFINITY_PERP, Mod::DARK_AFFINITY_PERP };

        static const WEATHER weatherStrong[8] = { WEATHER_HOT_SPELL, WEATHER_SNOW, WEATHER_WIND, WEATHER_DUST_STORM,
                                                  WEATHER_THUNDER, WEATHER_RAIN, WEATHER_AURORAS, WEATHER_GLOOM };

        uint8 element = ((CPetEntity*)(PChar->PPet))->m_Element - 1;

        XI_DEBUG_BREAK_IF(element > 7);

        reduction = reduction + PChar->getMod(strong[element]);

        if (battleutils::GetDayElement() == element)
        {
            reduction = reduction + PChar->getMod(Mod::DAY_REDUCTION);
        }

        WEATHER weather = battleutils::GetWeather(PChar, false);

        if (weather == weatherStrong[element] || weather == weatherStrong[element] + 1)
        {
            reduction = reduction + PChar->getMod(Mod::WEATHER_REDUCTION);
        }

        return reduction;
    }

    /************************************************************************
     *                                                                       *
     *  Record now as when the character has died and save it to the db.     *
     *                                                                       *
     ************************************************************************/

    void SaveDeathTime(CCharEntity* PChar)
    {
        const char* fmtQuery = "UPDATE char_stats SET death = %u WHERE charid = %u LIMIT 1;";
        Sql_Query(SqlHandle, fmtQuery, PChar->GetSecondsElapsedSinceDeath(), PChar->id);
    }

    void SavePlayTime(CCharEntity* PChar)
    {
        uint32 playtime = PChar->GetPlayTime();

        Sql_Query(SqlHandle, "UPDATE chars SET playtime = '%u' WHERE charid = '%u' LIMIT 1;", playtime, PChar->id);

        if (PChar->isNewPlayer() && playtime >= 36000)
        {
            PChar->menuConfigFlags.flags &= ~NFLAG_NEWPLAYER;
            PChar->updatemask |= UPDATE_HP;

            SaveMenuConfigFlags(PChar);
        }
    }

    /************************************************************************
     *                                                                       *
     *  Checks which UnarmedItem to grant when SLOT_MAIN is empty.           *
     *                                                                       *
     ************************************************************************/

    void CheckUnarmedWeapon(CCharEntity* PChar)
    {
        CItem* PSubslot = PChar->getEquip(SLOT_SUB);

        // Main or sub job provides H2H skill, and sub slot is empty.
        if ((battleutils::GetSkillRank(SKILL_HAND_TO_HAND, PChar->GetMJob()) > 0 || battleutils::GetSkillRank(SKILL_HAND_TO_HAND, PChar->GetSJob()) > 0) &&
            (!PSubslot || !PSubslot->isType(ITEM_EQUIPMENT)))
        {
            PChar->m_Weapons[SLOT_MAIN] = itemutils::GetUnarmedH2HItem();
            PChar->look.main            = 21; // The secret to H2H animations.  setModelId for UnarmedH2H didn't work.
        }
        else
        {
            PChar->m_Weapons[SLOT_MAIN] = itemutils::GetUnarmedItem();
            PChar->look.main            = 0;
        }
        BuildingCharWeaponSkills(PChar);
    }

    /************************************************************************
     *                                                                       *
     *  Opens the characters send box                                        *
     *                                                                       *
     ************************************************************************/

    void OpenSendBox(CCharEntity* PChar)
    {
        PChar->UContainer->Clean();
        PChar->UContainer->SetType(UCONTAINER_DELIVERYBOX);

        PChar->pushPacket(new CDeliveryBoxPacket(0x0D, 2, 0, 0x01));
    }

    bool CheckAbilityAddtype(CCharEntity* PChar, CAbility* PAbility)
    {
        if (PAbility->getAddType() & ADDTYPE_MERIT)
        {
            if (!PChar->PMeritPoints->GetMerit((MERIT_TYPE)PAbility->getMeritModID()))
            {
                ShowWarning("charutils::CheckAbilityAddtype: Attempt to add invalid Merit Ability (%d).", PAbility->getMeritModID());
                return false;
            }

            if (!(PChar->PMeritPoints->GetMerit((MERIT_TYPE)PAbility->getMeritModID())->count > 0))
            {
                return false;
            }
        }
        if (PAbility->getAddType() & ADDTYPE_ASTRAL_FLOW)
        {
            if (!PChar->StatusEffectContainer->HasStatusEffect(EFFECT_ASTRAL_FLOW))
            {
                return false;
            }
        }
        if (PAbility->getAddType() & ADDTYPE_LEARNED)
        {
            if (!hasLearnedAbility(PChar, PAbility->getID()))
            {
                return false;
            }
        }
        if (PAbility->getAddType() & ADDTYPE_LIGHT_ARTS)
        {
            if (!PChar->StatusEffectContainer->HasStatusEffect({ EFFECT_LIGHT_ARTS, EFFECT_ADDENDUM_WHITE }))
            {
                return false;
            }
        }
        if (PAbility->getAddType() & ADDTYPE_DARK_ARTS)
        {
            if (!PChar->StatusEffectContainer->HasStatusEffect({ EFFECT_DARK_ARTS, EFFECT_ADDENDUM_BLACK }))
            {
                return false;
            }
        }
        if ((PAbility->getAddType() & (ADDTYPE_JUGPET | ADDTYPE_CHARMPET)) == (ADDTYPE_JUGPET | ADDTYPE_CHARMPET))
        {
            if (!PChar->PPet || !(PChar->PPet->objtype == TYPE_MOB ||
                                  (PChar->PPet->objtype == TYPE_PET && static_cast<CPetEntity*>(PChar->PPet)->getPetType() == PET_TYPE::JUG_PET)))
            {
                return false;
            }
        }
        if ((PAbility->getAddType() & (ADDTYPE_JUGPET | ADDTYPE_CHARMPET)) == ADDTYPE_JUGPET)
        {
            if (!PChar->PPet || PChar->PPet->objtype != TYPE_PET || static_cast<CPetEntity*>(PChar->PPet)->getPetType() != PET_TYPE::JUG_PET)
            {
                return false;
            }
        }
        if ((PAbility->getAddType() & (ADDTYPE_JUGPET | ADDTYPE_CHARMPET)) == ADDTYPE_CHARMPET)
        {
            if (!PChar->PPet || PChar->PPet->objtype != TYPE_MOB)
            {
                return false;
            }
        }
        if (PAbility->getAddType() & ADDTYPE_AVATAR)
        {
            if (!PChar->PPet || PChar->PPet->objtype != TYPE_PET || static_cast<CPetEntity*>(PChar->PPet)->getPetType() != PET_TYPE::AVATAR)
            {
                return false;
            }
        }
        if (PAbility->getAddType() & ADDTYPE_AUTOMATON)
        {
            if (!PChar->PPet || PChar->PPet->objtype != TYPE_PET || static_cast<CPetEntity*>(PChar->PPet)->getPetType() != PET_TYPE::AUTOMATON)
            {
                return false;
            }
        }
        return true;
    }

    void RemoveStratagems(CCharEntity* PChar, CSpell* PSpell)
    {
        if (PSpell->getSpellGroup() == SPELLGROUP_WHITE)
        {
            // rapture to be deleted in applicable scripts
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_PENURY);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_CELERITY);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_ENLIGHTENMENT);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_ALTRUISM);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_TRANQUILITY);
            if (PSpell->getAOE() == SPELLAOE_RADIAL_ACCE)
            {
                PChar->StatusEffectContainer->DelStatusEffect(EFFECT_ACCESSION);
            }
        }
        else if (PSpell->getSpellGroup() == SPELLGROUP_BLACK)
        {
            // ebullience to be deleted in applicable scripts
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_PARSIMONY);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_ALACRITY);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_ENLIGHTENMENT);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_FOCALIZATION);
            PChar->StatusEffectContainer->DelStatusEffect(EFFECT_EQUANIMITY);
            if (PSpell->getAOE() == SPELLAOE_RADIAL_MANI)
            {
                PChar->StatusEffectContainer->DelStatusEffect(EFFECT_MANIFESTATION);
            }
        }
    }

    void RemoveAllEquipMods(CCharEntity* PChar)
    {
        for (uint8 slotID = 0; slotID < 16; ++slotID)
        {
            CItemEquipment* PItem = PChar->getEquip((SLOTTYPE)slotID);
            if (PItem)
            {
                PChar->delEquipModifiers(&PItem->modList, PItem->getReqLvl(), slotID);
                if (PItem->getReqLvl() <= PChar->GetMLevel())
                {
                    PChar->PLatentEffectContainer->DelLatentEffects(PItem->getReqLvl(), slotID);
                    PChar->PLatentEffectContainer->CheckLatentsEquip(slotID);
                }
            }
        }
    }

    void ApplyAllEquipMods(CCharEntity* PChar)
    {
        for (uint8 slotID = 0; slotID < 16; ++slotID)
        {
            CItemEquipment* PItem = (CItemEquipment*)PChar->getEquip((SLOTTYPE)slotID);
            if (PItem)
            {
                PChar->addEquipModifiers(&PItem->modList, PItem->getReqLvl(), slotID);
                if (PItem->getReqLvl() <= PChar->GetMLevel())
                {
                    PChar->PLatentEffectContainer->AddLatentEffects(PItem->latentList, PItem->getReqLvl(), slotID);
                    PChar->PLatentEffectContainer->CheckLatentsEquip(slotID);
                }
            }
        }
    }

    void ClearTempItems(CCharEntity* PChar)
    {
        CItemContainer* Temp = PChar->getStorage(LOC_TEMPITEMS);

        const char* Query = "DELETE FROM char_inventory WHERE charid = %u AND location = 3;";

        if (Sql_Query(SqlHandle, Query, PChar->id) != SQL_ERROR)
        {
            Temp->Clear();
        }
    }

    void ReloadParty(CCharEntity* PChar)
    {
        int ret = Sql_Query(SqlHandle,
                            "SELECT partyid, allianceid, partyflag & %d FROM accounts_sessions s JOIN accounts_parties p ON "
                            "s.charid = p.charid WHERE p.charid = %u;",
                            (PARTY_SECOND | PARTY_THIRD), PChar->id);
        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            uint32 partyid     = Sql_GetUIntData(SqlHandle, 0);
            uint32 allianceid  = Sql_GetUIntData(SqlHandle, 1);
            uint32 partynumber = Sql_GetUIntData(SqlHandle, 2);

            // first, parties and alliances must be created or linked if the character's current party has changed
            // for example, joining a party from another server
            if (PChar->PParty)
            {
                if (PChar->PParty->GetPartyID() != partyid)
                {
                    PChar->PParty->SetPartyID(partyid);
                }
            }
            else
            {
                // find if party exists on this server already
                CParty* PParty = nullptr;
                zoneutils::ForEachZone([partyid, &PParty](CZone* PZone) {
                    PZone->ForEachChar([partyid, &PParty](CCharEntity* PChar) {
                        if (PChar->PParty && PChar->PParty->GetPartyID() == partyid)
                        {
                            PParty = PChar->PParty;
                        }
                    });
                });

                // create new party if it doesn't exist already
                if (!PParty)
                {
                    PParty = new CParty(partyid);
                }

                PParty->PushMember(PChar);
            }

            CBattleEntity* PSyncTarget = PChar->PParty->GetSyncTarget();
            if (PSyncTarget && PChar->getZone() == PSyncTarget->getZone() && !(PChar->StatusEffectContainer->HasStatusEffect(EFFECT_LEVEL_SYNC)) &&
                PSyncTarget->StatusEffectContainer->HasStatusEffect(EFFECT_LEVEL_SYNC) &&
                PSyncTarget->StatusEffectContainer->GetStatusEffect(EFFECT_LEVEL_SYNC)->GetDuration() == 0)
            {
                PChar->pushPacket(new CMessageBasicPacket(PChar, PChar, 0, PSyncTarget->GetMLevel(), 540));
                PChar->StatusEffectContainer->AddStatusEffect(new CStatusEffect(EFFECT_LEVEL_SYNC, EFFECT_LEVEL_SYNC, PSyncTarget->GetMLevel(), 0, 0), true);
                PChar->StatusEffectContainer->DelStatusEffectsByFlag(EFFECTFLAG_DISPELABLE);
            }

            if (allianceid != 0)
            {
                if (PChar->PParty->m_PAlliance)
                {
                    if (PChar->PParty->m_PAlliance->m_AllianceID != allianceid)
                    {
                        PChar->PParty->m_PAlliance->m_AllianceID = allianceid;
                    }
                }
                else
                {
                    // find if the alliance exists on this server already
                    CAlliance* PAlliance = nullptr;
                    zoneutils::ForEachZone([allianceid, &PAlliance](CZone* PZone) {
                        PZone->ForEachChar([allianceid, &PAlliance](CCharEntity* PChar) {
                            if (PChar->PParty && PChar->PParty->m_PAlliance && PChar->PParty->m_PAlliance->m_AllianceID == allianceid)
                            {
                                PAlliance = PChar->PParty->m_PAlliance;
                            }
                        });
                    });

                    // create new alliance if it doesn't exist on this server already
                    if (!PAlliance)
                    {
                        PAlliance = new CAlliance(allianceid);
                    }

                    PAlliance->pushParty(PChar->PParty, partynumber);
                }
            }
            else if (PChar->PParty->m_PAlliance)
            {
                PChar->PParty->m_PAlliance->delParty(PChar->PParty);
            }

            // once parties and alliances have been reassembled, reload the party/parties
            if (PChar->PParty->m_PAlliance)
            {
                for (auto* party : PChar->PParty->m_PAlliance->partyList)
                {
                    party->ReloadParty();
                }
            }
            else
            {
                PChar->PParty->ReloadParty();
            }
        }
        else
        {
            if (PChar->PParty)
            {
                PChar->PParty->DelMember(PChar);
            }
            PChar->ReloadPartyDec();
        }
    }

    bool IsAidBlocked(CCharEntity* PInitiator, CCharEntity* PTarget)
    {
        if (PTarget->getBlockingAid())
        {
            bool inAlliance = false;
            PTarget->ForAlliance([&PInitiator, &inAlliance](CBattleEntity* PEntity) {
                if (PEntity->id == PInitiator->id)
                {
                    inAlliance = true;
                }
            });

            if (!inAlliance)
            {
                return true;
            }
        }
        return false;
    }

    // char_points manipulation
    void AddPoints(CCharEntity* PChar, const char* type, int32 amount, int32 max)
    {
        const char* Query = "UPDATE char_points SET %s = GREATEST(LEAST(%s+%d, %d), 0) WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, type, type, amount, max, PChar->id);

        if (strcmp(type, "unity_accolades") == 0 && amount > 0)
        {
            float       evalPoints   = static_cast<float>(amount) / 1000;
            const char* rankingQuery = "UPDATE unity_system SET points_current = points_current+%f WHERE leader=%d;";

            AddPoints(PChar, "current_accolades", amount, std::numeric_limits<int32>::max()); // Do not cap current_accolades
            Sql_Query(SqlHandle, rankingQuery, evalPoints, PChar->profile.unity_leader);
            roeutils::UpdateUnityTrust(PChar, true);

            PChar->pushPacket(new CCharStatsPacket(PChar));
        }
        else if (strcmp(type, "spark_of_eminence") == 0)
        {
            PChar->pushPacket(new CRoeSparkUpdatePacket(PChar));
        }
    }

    void SetPoints(CCharEntity* PChar, const char* type, int32 amount)
    {
        const char* Query = "UPDATE char_points SET %s = %d WHERE charid = %u;";

        Sql_Query(SqlHandle, Query, type, amount, PChar->id);

        if (strcmp(type, "spark_of_eminence") == 0)
        {
            PChar->pushPacket(new CRoeSparkUpdatePacket(PChar));
        }
    }

    int32 GetPoints(CCharEntity* PChar, const char* type)
    {
        const char* Query = "SELECT %s FROM char_points WHERE charid = %u;";

        int ret = Sql_Query(SqlHandle, Query, type, PChar->id);

        if (ret != SQL_ERROR && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            return Sql_GetIntData(SqlHandle, 0);
        }
        return 0;
    }

    void SetUnityLeader(CCharEntity* PChar, uint8 leaderID)
    {
        const char* leaderQuery = "UPDATE char_profile SET unity_leader=%d WHERE charid = %u;";

        if (leaderID < 1 || leaderID > 11)
        {
            return;
        }

        PChar->profile.unity_leader = leaderID;
        if (PChar->PUnityChat)
        {
            unitychat::DelOnlineMember(PChar, PChar->PUnityChat->getLeader());
        }
        unitychat::AddOnlineMember(PChar, PChar->profile.unity_leader);
        Sql_Query(SqlHandle, leaderQuery, PChar->profile.unity_leader, PChar->id);
    }

    std::string GetConquestPointsName(CCharEntity* PChar)
    {
        switch (PChar->profile.nation)
        {
            case 0:
                return "sandoria_cp";
            case 1:
                return "bastok_cp";
            case 2:
                return "windurst_cp";
            default:
                XI_DEBUG_BREAK_IF(true);
                return nullptr;
        }
    }

    void SendToZone(CCharEntity* PChar, uint8 type, uint64 ipp)
    {
        if (type == 2)
        {
            Sql_Query(SqlHandle, "UPDATE accounts_sessions SET server_addr = %u, server_port = %u WHERE charid = %u;", (uint32)ipp, (uint32)(ipp >> 32),
                      PChar->id);

            const char* Query = "UPDATE chars "
                                "SET "
                                "pos_zone = %u,"
                                "pos_prevzone = %u,"
                                "pos_rot = %u,"
                                "pos_x = %.3f,"
                                "pos_y = %.3f,"
                                "pos_z = %.3f,"
                                "moghouse = %u,"
                                "boundary = %u "
                                "WHERE charid = %u;";

            Sql_Query(SqlHandle, Query, PChar->loc.destination,
                      (PChar->m_moghouseID || PChar->loc.destination == PChar->getZone()) ? PChar->loc.prevzone : PChar->getZone(), PChar->loc.p.rotation,
                      PChar->loc.p.x, PChar->loc.p.y, PChar->loc.p.z, PChar->m_moghouseID, PChar->loc.boundary, PChar->id);
        }
        else
        {
            SaveCharPosition(PChar);
        }

        PChar->pushPacket(new CServerIPPacket(PChar, type, ipp));
    }

    void HomePoint(CCharEntity* PChar)
    {
        // remove weakness on homepoint
        PChar->StatusEffectContainer->DelStatusEffectSilent(EFFECT_WEAKNESS);
        PChar->StatusEffectContainer->DelStatusEffectSilent(EFFECT_LEVEL_SYNC);

        PChar->SetDeathTimestamp(0);

        PChar->health.hp = PChar->GetMaxHP();
        PChar->health.mp = PChar->GetMaxMP();

        PChar->loc.boundary    = 0;
        PChar->loc.p           = PChar->profile.home_point.p;
        PChar->loc.destination = PChar->profile.home_point.destination;

        PChar->status    = STATUS_TYPE::DISAPPEAR;
        PChar->animation = ANIMATION_NONE;
        PChar->updatemask |= UPDATE_HP;

        PChar->clearPacketList();
        SendToZone(PChar, 2, zoneutils::GetZoneIPP(PChar->loc.destination));
    }

    bool AddWeaponSkillPoints(CCharEntity* PChar, SLOTTYPE slotid, int wspoints)
    {
        CItemWeapon* PWeapon = (CItemWeapon*)PChar->m_Weapons[slotid];

        if (PWeapon && PWeapon->isUnlockable() && !PWeapon->isUnlocked())
        {
            if (PWeapon->addWsPoints(wspoints))
            {
                // weapon is now broken
                PChar->PLatentEffectContainer->CheckLatentsWeaponBreak(slotid);
                PChar->pushPacket(new CCharStatsPacket(PChar));
            }
            char extra[sizeof(PWeapon->m_extra) * 2 + 1];
            Sql_EscapeStringLen(SqlHandle, extra, (const char*)PWeapon->m_extra, sizeof(PWeapon->m_extra));

            const char* Query = "UPDATE char_inventory SET extra = '%s' WHERE charid = %u AND location = %u AND slot = %u LIMIT 1";
            Sql_Query(SqlHandle, Query, extra, PChar->id, PWeapon->getLocationID(), PWeapon->getSlotID());
            return true;
        }
        return false;
    }

    int32 GetCharVar(CCharEntity* PChar, const char* var)
    {
        if (PChar == nullptr)
        {
            ShowError("GetCharVar was requested for a nullptr PChar");
            return 0;
        }

        const char* fmtQuery = "SELECT value FROM char_vars WHERE charid = %u AND varname = '%s' LIMIT 1;";

        int32 ret = Sql_Query(SqlHandle, fmtQuery, PChar->id, var);

        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            return Sql_GetIntData(SqlHandle, 0);
        }
        return 0;
    }

    void SetCharVar(CCharEntity* PChar, const char* var, int32 value)
    {
        if (PChar == nullptr)
        {
            ShowError("SetCharVar was requested for a nullptr PChar");
            return;
        }

        if (value == 0)
        {
            Sql_Query(SqlHandle, "DELETE FROM char_vars WHERE charid = %u AND varname = '%s' LIMIT 1;", PChar->id, var);
        }
        else
        {
            const char* fmtQuery = "INSERT INTO char_vars SET charid = %u, varname = '%s', value = %i ON DUPLICATE KEY UPDATE value = %i;";
            Sql_Query(SqlHandle, fmtQuery, PChar->id, var, value, value);
        }
    }

    void ClearCharVarsWithPrefix(CCharEntity* PChar, std::string prefix)
    {
        if (PChar == nullptr)
        {
            return;
        }

        // Validate that prefix is not too short, since we don't want it to
        // accidentally clear a lot of variables it shouldn't.
        if (prefix.size() < 5)
        {
            ShowError("Prefix too short to clear with: '%s'", prefix);
            return;
        }

        Sql_Query(SqlHandle, "DELETE FROM char_vars WHERE charid = %u AND varname LIKE '%s%%';", PChar->id, prefix.c_str());
    }

    uint16 getWideScanRange(JOBTYPE job, uint8 level)
    {
        // Set Widescan range
        // Distances need verified, based current values off what we had in traits.sql and data at http://wiki.ffxiclopedia.org/wiki/Wide_Scan
        // NOTE: Widescan was formerly piggy backed onto traits (resist slow) but is not a real trait, any attempt to give it a trait will place a dot on
        // characters trait menu.

        // Limit to BST and RNG, and try to use old distance values for tiers
        if (job == JOB_RNG)
        {
            // Range for RNG >=80 needs verification.
            if (level >= 80)
            {
                return 350;
            }
            else if (level >= 60)
            {
                return 300;
            }
            else if (level >= 40)
            {
                return 250;
            }
            else if (level >= 20)
            {
                return 200;
            }
            else
            {
                return 150;
            }
        }
        else if (job == JOB_BST)
        {
            if (level >= 80)
            {
                return 300;
            }
            else if (level >= 60)
            {
                return 250;
            }
            else if (level >= 40)
            {
                return 200;
            }
            else if (level >= 20 || map_config.all_jobs_widescan == 1)
            {
                return 150;
            }
            else
            {
                return 50;
            }
        }

        // Default to base widescan if not RNG or BST
        if (map_config.all_jobs_widescan == 1)
        {
            return 150;
        }
        else
        {
            return 0;
        }
    }

    uint16 getWideScanRange(CCharEntity* PChar)
    {
        // Get maximum widescan range from main job or sub job
        return std::max(getWideScanRange(PChar->GetMJob(), PChar->GetMLevel()), getWideScanRange(PChar->GetSJob(), PChar->GetSLevel()));
    }

    void SendTimerPacket(CCharEntity* PChar, uint32 seconds)
    {
        auto* timerPacket = new CTimerBarUtilPacket();
        timerPacket->addCountdown(seconds);
        PChar->pushPacket(timerPacket);
    }

    void SendTimerPacket(CCharEntity* PChar, duration dur)
    {
        auto timeLimitSeconds = static_cast<uint32>(std::chrono::duration_cast<std::chrono::seconds>(dur).count());
        SendTimerPacket(PChar, timeLimitSeconds);
    }

    void SendClearTimerPacket(CCharEntity* PChar)
    {
        auto* timerPacket = new CTimerBarUtilPacket();
        PChar->pushPacket(timerPacket);
    }

    void ReadHistory(CCharEntity* PChar)
    {
        if (PChar == nullptr)
        {
            return;
        }

        auto fmtQuery = "SELECT "
                        "enemies_defeated, "  // 0
                        "times_knocked_out, " // 1
                        "mh_entrances, "      // 2
                        "joined_parties, "    // 3
                        "joined_alliances, "  // 4
                        "spells_cast, "       // 5
                        "abilities_used, "    // 6
                        "ws_used, "           // 7
                        "items_used, "        // 8
                        "chats_sent, "        // 9
                        "npc_interactions, "  // 10
                        "battles_fought, "    // 11
                        "gm_calls, "          // 12
                        "distance_travelled " // 13
                        "FROM char_history "
                        "WHERE charid = %u;";

        auto ret = Sql_Query(SqlHandle, fmtQuery, PChar->id);
        if (ret != SQL_ERROR && Sql_NumRows(SqlHandle) != 0 && Sql_NextRow(SqlHandle) == SQL_SUCCESS)
        {
            PChar->m_charHistory.enemiesDefeated   = Sql_GetUIntData(SqlHandle, 0);
            PChar->m_charHistory.timesKnockedOut   = Sql_GetUIntData(SqlHandle, 1);
            PChar->m_charHistory.mhEntrances       = Sql_GetUIntData(SqlHandle, 2);
            PChar->m_charHistory.joinedParties     = Sql_GetUIntData(SqlHandle, 3);
            PChar->m_charHistory.joinedAlliances   = Sql_GetUIntData(SqlHandle, 4);
            PChar->m_charHistory.spellsCast        = Sql_GetUIntData(SqlHandle, 5);
            PChar->m_charHistory.abilitiesUsed     = Sql_GetUIntData(SqlHandle, 6);
            PChar->m_charHistory.wsUsed            = Sql_GetUIntData(SqlHandle, 7);
            PChar->m_charHistory.itemsUsed         = Sql_GetUIntData(SqlHandle, 8);
            PChar->m_charHistory.chatsSent         = Sql_GetUIntData(SqlHandle, 9);
            PChar->m_charHistory.npcInteractions   = Sql_GetUIntData(SqlHandle, 10);
            PChar->m_charHistory.battlesFought     = Sql_GetUIntData(SqlHandle, 11);
            PChar->m_charHistory.gmCalls           = Sql_GetUIntData(SqlHandle, 12);
            PChar->m_charHistory.distanceTravelled = Sql_GetUIntData(SqlHandle, 13);
        }
    }

    void WriteHistory(CCharEntity* PChar)
    {
        if (PChar == nullptr)
        {
            return;
        }

        // Replace will also handle insert if it doesn't exist
        auto fmtQuery = "REPLACE INTO char_history "
                        "(charid, enemies_defeated, times_knocked_out, mh_entrances, joined_parties, joined_alliances, spells_cast, "
                        "abilities_used, ws_used, items_used, chats_sent, npc_interactions, battles_fought, gm_calls, distance_travelled) "
                        "VALUES("
                        "%u, " // charid
                        "%u, " // 0 enemies_defeated
                        "%u, " // 1 times_knocked_out
                        "%u, " // 2 mh_entrances
                        "%u, " // 3 joined_parties
                        "%u, " // 4 joined_alliances
                        "%u, " // 5 spells_cast
                        "%u, " // 6 abilities_used
                        "%u, " // 7 ws_used
                        "%u, " // 8 items_used
                        "%u, " // 9 chats_sent
                        "%u, " // 10 npc_interactions
                        "%u, " // 11 battles_fought
                        "%u, " // 12 gm_calls
                        "%u"   // 13 distance_travelled
                        ");";

        auto ret = Sql_Query(SqlHandle, fmtQuery,
                        PChar->id,
                        PChar->m_charHistory.enemiesDefeated,
                        PChar->m_charHistory.timesKnockedOut,
                        PChar->m_charHistory.mhEntrances,
                        PChar->m_charHistory.joinedParties,
                        PChar->m_charHistory.joinedAlliances,
                        PChar->m_charHistory.spellsCast,
                        PChar->m_charHistory.abilitiesUsed,
                        PChar->m_charHistory.wsUsed,
                        PChar->m_charHistory.itemsUsed,
                        PChar->m_charHistory.chatsSent,
                        PChar->m_charHistory.npcInteractions,
                        PChar->m_charHistory.battlesFought,
                        PChar->m_charHistory.gmCalls,
                        PChar->m_charHistory.distanceTravelled);

        if (ret == SQL_ERROR)
        {
            ShowError("Error writing char history for: '%s'", PChar->name.c_str());
        }
    }
}; // namespace charutils
