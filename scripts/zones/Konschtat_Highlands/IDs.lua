-----------------------------------
-- Area: Konschtat_Highlands
-----------------------------------
require("scripts/globals/zone")
-----------------------------------

zones = zones or {}

zones[xi.zone.KONSCHTAT_HIGHLANDS] =
{
    text =
    {
        NOTHING_HAPPENS              = 119,   -- Nothing happens...
        ITEM_CANNOT_BE_OBTAINED      = 6383,  -- You cannot obtain the <item>. Come back after sorting your inventory.
        ITEM_OBTAINED                = 6389,  -- Obtained: <item>.
        GIL_OBTAINED                 = 6390,  -- Obtained <number> gil.
        KEYITEM_OBTAINED             = 6392,  -- Obtained key item: <keyitem>.
        NOTHING_OUT_OF_ORDINARY      = 6403,  -- There is nothing out of the ordinary here.
        FELLOW_MESSAGE_OFFSET        = 6418,  -- I'm ready. I suppose.
        CARRIED_OVER_POINTS          = 7000,  -- You have carried over <number> login point[/s].
        LOGIN_CAMPAIGN_UNDERWAY      = 7001,  -- The [/January/February/March/April/May/June/July/August/September/October/November/December] <number> Login Campaign is currently underway!<space>
        LOGIN_NUMBER                 = 7002,  -- In celebration of your most recent login (login no. <number>), we have provided you with <number> points! You currently have a total of <number> points.
        CONQUEST_BASE                = 7053,  -- Tallying conquest results...
        ALREADY_OBTAINED_TELE        = 7212,  -- You already possess the gate crystal for this telepoint.
        DIG_THROW_AWAY               = 7229,  -- You dig up <item>, but your inventory is full. You regretfully throw the <item> away.
        FIND_NOTHING                 = 7231,  -- You dig and you dig, but find nothing.
        SIGNPOST3                    = 7387,  -- North: Valkurm Dunes South: North Gustaberg East: Gusgen Mines, Pashhow Marshlands
        SIGNPOST2                    = 7388,  -- North: Pashhow Marshlands West: Valkurm Dunes, North Gustaberg Southeast: Gusgen Mines
        SIGNPOST_DIALOG_1            = 7389,  -- North: Valkurm Dunes South: To Gustaberg
        SIGNPOST_DIALOG_2            = 7390,  -- You see something stuck behind the signpost.
        SOMETHING_BURIED_HERE        = 7391,  -- Something has been buried here.
        BLACKENED_SPOT_ON_GROUND     = 7458,  -- There is a blackened spot on the ground...
        BLACKENED_SHOULD_PLACE       = 7459,  -- This is the blackened spot you were told about. You should place <item> here.
        PLACE_BLACKENED_SPOT         = 7460,  -- You place <item> on the blackened spot.
        BLACKENED_NOTHING_HAPPENS    = 7461,  -- You place <item> on the blackened spot, but nothing happens.
        BLACKENED_MUST_BE_CLOSER     = 7462,  -- You have to be closer to place anything on the blackened spot.
        NOT_THE_TIME_FOR_THAT        = 7471,  -- This is not the time for that!
        TELEPOINT_HAS_BEEN_SHATTERED = 7498,  -- The telepoint has been shattered into a thousand pieces...
        PLAYER_OBTAINS_ITEM          = 7616,  -- <name> obtains <item>!
        UNABLE_TO_OBTAIN_ITEM        = 7617,  -- You were unable to obtain the item.
        PLAYER_OBTAINS_TEMP_ITEM     = 7618,  -- <name> obtains the temporary item: <item>!
        ALREADY_POSSESS_TEMP         = 7619,  -- You already possess that temporary item.
        NO_COMBINATION               = 7624,  -- You were unable to enter a combination.
        VOIDWALKER_DESPAWN           = 7655,  -- The monster fades before your eyes, a look of disappointment on its face.
        REGIME_REGISTERED            = 9802,  -- New training regime registered!
        VOIDWALKER_NO_MOB            = 10975, -- The <keyitem> quivers ever so slightly, but emits no light. There seem to be no monsters in the area.
        VOIDWALKER_MOB_TOO_FAR       = 10976, -- The <keyitem> quivers ever so slightly and emits a faint light. There seem to be no monsters in the immediate vicinity.
        VOIDWALKER_MOB_HINT          = 10977, -- The <keyitem> resonates [feebly/softly/solidly/strongly/very strongly/furiously], sending a radiant beam of light lancing towards a spot roughly <number> [yalm/yalms] [east/southeast/south/southwest/west/northwest/north/northeast] of here.
        VOIDWALKER_SPAWN_MOB         = 10978, -- A monster materializes out of nowhere!
        VOIDWALKER_UPGRADE_KI_1      = 10980, -- The <keyitem> takes on a slightly deeper hue and becomes <keyitem>!
        VOIDWALKER_UPGRADE_KI_2      = 10981, -- The <keyitem> takes on a deeper, richer hue and becomes <keyitem>!
        VOIDWALKER_BREAK_KI          = 10982, -- The <keyitem> shatters into tiny fragments.
        VOIDWALKER_OBTAIN_KI         = 10983, -- Obtained key item: <keyitem>!
        COMMON_SENSE_SURVIVAL        = 11928, -- It appears that you have arrived at a new survival guide provided by the Adventurers' Mutual Aid Network. Common sense dictates that you should now be able to teleport here from similar tomes throughout the world.
    },
    mob =
    {
        STRAY_MARY_PH  =
        {
            [17219791] = 17219795, -- -305.204 -11.695 -96.078
            [17219928] = 17219933, -- -293.900  33.393 342.710
        },
        RAMPAGING_RAM_PH =
        {
            [17219885] = 17219886, -- 21 40 514
            [17219987] = 17219886, -- -163.198 62.392 568.282
            [17219886] = 17219887, -- Rampaging can't spawn if Steelfleece is up
        },
        STEELFLEECE_PH =
        {
            [17219885] = 17219887, -- 21 40 514
            [17219886] = 17219887, -- 160 24 121
            [17219987] = 17219887, -- -163.198 62.392 568.282
            [17219887] = 17219886, -- Steelfleece can't spawn if Rampaging is up
        },
        FORGER         = 17219999,
        HATY           = 17220000,
        BENDIGEIT_VRAN = 17220001,
        VOIDWALKER  =
        {
            [xi.keyItem.CLEAR_ABYSSITE] = {
                17220019, -- Prickly Sheep
                17220018, -- Prickly Sheep
                17220017, -- Prickly Sheep
                17220016, -- Prickly Sheep
                17220015,  -- Void Hare
                17220014,  -- Void Hare
                17220013,  -- Void Hare
                17220012,  -- Void Hare
            },
            [xi.keyItem.COLORFUL_ABYSSITE] = {
                17220011,  -- Chesma
                17220010, -- Tammuz
            },
            [xi.keyItem.GREY_ABYSSITE] = {
                17220009  -- Dawon
            },
            [xi.keyItem.BLACK_ABYSSITE] = {
                17220008  -- Yilbegan
            }
        }
    },
    npc =
    {
        CASKET_BASE = 17220110,
    },
}

return zones[xi.zone.KONSCHTAT_HIGHLANDS]
