-----------------------------------
-- Area: Beadeaux
-----------------------------------
require("scripts/globals/zone")
-----------------------------------

zones = zones or {}

zones[xi.zone.BEADEAUX] =
{
    text =
    {
        ITEM_CANNOT_BE_OBTAINED          = 6383, -- You cannot obtain the <item>. Come back after sorting your inventory.
        ITEM_OBTAINED                    = 6389, -- Obtained: <item>.
        GIL_OBTAINED                     = 6390, -- Obtained <number> gil.
        KEYITEM_OBTAINED                 = 6392, -- Obtained key item: <keyitem>.
        NOT_ENOUGH_GIL                   = 6394, -- You do not have enough gil.
        ITEMS_OBTAINED                   = 6398, -- You obtain <number> <item>!
        NOTHING_OUT_OF_ORDINARY          = 6403, -- There is nothing out of the ordinary here.
        FELLOW_MESSAGE_OFFSET            = 6418, -- I'm ready. I suppose.
        CARRIED_OVER_POINTS              = 7000, -- You have carried over <number> login point[/s].
        LOGIN_CAMPAIGN_UNDERWAY          = 7001, -- The [/January/February/March/April/May/June/July/August/September/October/November/December] <number> Login Campaign is currently underway!<space>
        LOGIN_NUMBER                     = 7002, -- In celebration of your most recent login (login no. <number>), we have provided you with <number> points! You currently have a total of <number> points.
        CONQUEST_BASE                    = 7053, -- Tallying conquest results...
        LOCKED_DOOR_QUADAV_HAS_KEY       = 7216, -- It is locked tight, but has what looks like a keyhole. Maybe one of the Quadav here has the key.
        YOU_CAN_NOW_BECOME_A_DARK_KNIGHT = 7353, -- You can now become a dark knight!
        CHEST_UNLOCKED                   = 7364, -- You unlock the chest!
        COMMON_SENSE_SURVIVAL            = 7791, -- It appears that you have arrived at a new survival guide provided by the Adventurers' Mutual Aid Network. Common sense dictates that you should now be able to teleport here from similar tomes throughout the world.
    },
    mob =
    {
        MIMIC                  = 17379783,
        BI_GHO_HEADTAKER_PH    =
        {
            [17379350] = 17379351, -- -98.611 0.498 71.212
        },
        DA_DHA_HUNDREDMASK_PH  =
        {
            [17379426] = 17379427, -- -71.480 0.490 -62.882
        },
        GE_DHA_EVILEYE_PH      =
        {
            [17379447] = 17379450, -- -242.709 0.5 -188.01
        },
        ZO_KHU_BLACKCLOUD_PH   =
        {
            [17379562] = 17379564, -- -294.223 -3.504 -206.657
        },
        GA_BHU_UNVANQUISHED_PH =
        {
            [17379625] = 17379626, -- 139.642 -2.445 161.557
        },
    },
    npc =
    {
        TREASURE_CHEST  = 17379842,
        TREASURE_COFFER = 17379843,
    },
}

return zones[xi.zone.BEADEAUX]
