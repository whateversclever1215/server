--[[
    Helper functions for common NPC tasks.

    npcUtil.popFromQM(player, qm, mobId, params)
    npcUtil.pickNewPosition(npc, positionTable, allowCurrentPosition)
    npcUtil.giveCurrency(player, currency, amount)
    npcUtil.giveItem(player, items, params)
    npcUtil.giveKeyItem(player, keyitems)
    npcUtil.completeMission(player, logId, missionId, params)
    npcUtil.completeQuest(player, area, quest, params)
    npcUtil.tradeHas(trade, items)
    npcUtil.tradeHasExactly(trade, items)
    npcUtil.queueMove(npc, point, delay)
    npcUtil.UpdateNPCSpawnPoint(id, minTime, maxTime, posTable, serverVar)
    npcUtil.castingAnimation(npc, magicType, phaseDuration, func)
    npcUtil.fishingAnimation(npc, phaseDuration, func)
--]]
require("scripts/globals/settings")
require("scripts/globals/status")
require("scripts/globals/msg")

npcUtil = {}

--[[ *******************************************************************************
    Pop mob(s) from question mark NPC.
    If any mob is already spawned, return false.
    Params (table) can contain the following parameters:

    radius (number)
        if set, spawn mobs randomly within radius of NPC
    claim (boolean, default true)
        do spawned mobs automatically aggro the player
    hide (number, default xi.settings.FORCE_SPAWN_QM_RESET_TIME)
        how long to hide the QM for after mobs die

******************************************************************************* --]]
function npcUtil.popFromQM(player, qm, mobId, params)
    local qmId = qm:getID()

    -- default params
    if not params then
        params = {}
    end
    if params.claim == nil or type(params.claim) ~= "boolean" then
        params.claim = true
    end
    if params.hide == nil or type(params.hide) ~= "number" then
        params.hide = xi.settings.FORCE_SPAWN_QM_RESET_TIME
    end

    -- get list of mobs to pop
    local mobs = {}
    if type(mobId) == "number" then
        table.insert(mobs, mobId)
    elseif type(mobId) == "table" then
        for _, v in pairs(mobId) do
            if type(v) == "number" then
                table.insert(mobs, v)
            end
        end
    end

    -- make sure none are spawned
    for k, v in pairs(mobs) do
        local mob = GetMobByID(v)
        if mob == nil or mob:isSpawned() then
            return false
        else
            mobs[k] = mob
        end
    end

    -- hide qm
    if params.hide > 0 then
        qm:setStatus(xi.status.DISAPPEAR)
    end

    -- spawn mobs and give each a listener that will show QM after they are all dead
    for _, mob in pairs(mobs) do
        -- choose random position uniformly from within radius
        if params.radius and type(params.radius) == "number" then
            local r = params.radius * math.sqrt(math.random())
            local theta = math.random() * 2 * math.pi
            local x = r * math.cos(theta)
            local z = r * math.sin(theta)
            mob:setSpawn(qm:getXPos() + x, qm:getYPos(), qm:getZPos() + z)
        end

        -- spawn
        mob:spawn()

        -- claim
        if params.claim then
            mob:updateClaim(player)
        end

        -- look
        if params.look then
            mob:lookAt(player:getPos())
        end

        -- reappear the QM when all spawned mobs are dead, plus params.hide seconds
        if params.hide > 0 then
            local myId = mob:getID()
            mob:setLocalVar("qm", qmId)
            mob:addListener("DESPAWN", "QM_"..myId, function(m)
                m:removeListener("QM_"..myId)

                for _, v in pairs(mobs) do
                    if v:isAlive() then
                        return false
                    end
                end

                GetNPCByID(m:getLocalVar("qm")):updateNPCHideTime(params.hide)
            end)
        end
    end

    return true
end

--[[ *******************************************************************************
    Queue a position change for an NPC.  We do this because if you setPos() an NPC
    immediately after you setStatus(xi.status.DISAPPEAR) it, the QM does not hide
    on the players' screens.

    point may be any of the following formats:
    {x, y, z}
    {x, y, z, rot}
    {x = x, y = y, z = z}
    {x = x, y = y, z = z, rot = r}
******************************************************************************* --]]
local function doMove(x, y, z, r)
    if not r then
        r = 0
    end
    return function(entity)
        entity:setPos(x, y, z, r)
    end
end

function npcUtil.queueMove(npc, point, delay)
    if not delay then
        delay = 3000
    end
    if point.rot then
        point = {point.x, point.y, point.z, point.rot}
    elseif point.x then
        point = {point.x, point.y, point.z}
    end
    npc:queue(delay, doMove(unpack(point)))
end

-- Picks a new position for an NPC and excluding the current position.
-- INPUT: npc = npcID, position = 2D table with coords: index, {x, y, z}
-- RETURN: table index
function npcUtil.pickNewPosition(npcID, positionTable, allowCurrentPosition)
    local npc = GetNPCByID(npcID)
    local positionIndex = 1 -- Default to position one in the table if it can't be found.
    local tableSize = 0
    local newPosition = 0
    allowCurrentPosition = allowCurrentPosition or false

    for i, v in ipairs(positionTable) do   -- Looking for the current position

        if not allowCurrentPosition then
            -- Finding by comparing the NPC's coords
            if math.floor(v[1]) == math.floor(npc:getXPos()) and math.floor(v[2]) == math.floor(npc:getYPos()) and math.floor(v[3]) == math.floor(npc:getZPos()) then
                positionIndex = i -- Found where the NPC is!
            end
        end

        tableSize = tableSize + 1 -- Counting the array size
    end

    if not allowCurrentPosition then
        -- Pick a new pos that isn't the current
        repeat
            newPosition = math.random(1, tableSize)
        until (newPosition ~= positionIndex)
    else
        newPosition = math.random(1, tableSize)
    end

    return {["x"] = positionTable[newPosition][1], ["y"] = positionTable[newPosition][2], ["z"] = positionTable[newPosition][3]}
end

--[[ *******************************************************************************
    Give item(s) to player.
    If player has inventory space, give items, display message, and return true.
    If not, do not give items, display a message to indicate this, and return false.

    Examples of valid items parameter:
        640                 -- copper ore x1
        { 640, 641 }        -- copper ore x1, tin ore x1
        { {640, 2} }         -- copper ore x2
        { {640, 2}, 641 }    -- copper ore x2, tin ore x1

    params (table) can contain the following parameters:

    silent (boolean, default false)
        if set, displays no messages
    fromTrade (boolean, default false)
        if set, when player has no room for items, display
        "Try trading again after sorting your inventory"
        instead of
        "Come back again after sorting your inventory"
******************************************************************************* --]]
function npcUtil.giveItem(player, items, params)
    params = params or {}
    local ID = zones[player:getZoneID()]

    -- create table of items, with key/val of itemId/itemQty
    local givenItems = {}
    if type(items) == "number" then
        table.insert(givenItems, {items, 1})
    elseif type(items) == "table" then
        for _, v in pairs(items) do
            if type(v) == "number" then
                table.insert(givenItems, {v, 1})
            elseif type(v) == "table" and #v == 2 and type(v[1]) == "number" and type(v[2]) == "number" then
                table.insert(givenItems, {v[1], v[2]})
            else
                print(string.format("ERROR: invalid items parameter given to npcUtil.giveItem in zone %s.", player:getZoneName()))
                return false
            end
        end
    end

    -- does player have enough inventory space?
    if player:getFreeSlotsCount() < #givenItems then
        if not params.silent then
            local messageId = params.fromTrade and (ID.text.ITEM_CANNOT_BE_OBTAINED + 4) or ID.text.ITEM_CANNOT_BE_OBTAINED
            player:messageSpecial(messageId, givenItems[1][1])
        end
        return false
    end

    -- give items to player
    local messagedItems = {}
    for _, v in pairs(givenItems) do
        if player:addItem(v[1], v[2], true) then
            if not params.silent and not messagedItems[v[1]] then
                if v[2] > 1 then
                    player:messageSpecial(ID.text.ITEM_OBTAINED + 9, v[1], v[2])
                else
                    player:messageSpecial(ID.text.ITEM_OBTAINED, v[1])
                end
            end
            messagedItems[v[1]] = true
        elseif #givenItems == 1 then
            if not params.silent then
                player:messageSpecial(ID.text.ITEM_CANNOT_BE_OBTAINED, givenItems[1][1])
            end
            return false
        end
    end
    return true
end

--[[ *******************************************************************************
    Give currency to a player.
    Message is displayed showing currency obtained.

    Examples of valid parameters:
        gil, 500
        bayld, 1000
******************************************************************************* --]]
function npcUtil.giveCurrency(player, currency, amount)
    local ID = zones[player:getZoneID()]

    if (not type(currency) == "string") or (not type(amount) == "number") then
        print(string.format("ERROR: invalid parameter given to npcUtil.giveCurrency in zone %s.", player:getZoneName()))
        return false
    end

    currency = string.lower(currency)

    local currency_types =
    {
        ["gil"]   = {"GIL_OBTAINED", xi.settings.GIL_RATE},
        ["bayld"] = {"BAYLD_OBTAINED", xi.settings.BAYLD_RATE}
    }

    local currency_type = currency_types[currency]

    if not currency_type then
        print(string.format("ERROR: invalid currency '%s' given to npcUtil.giveCurrency in zone %s.", currency, player:getZoneName()))
        return false
    end

    local message_id = ID.text[currency_type[1]]
    if not message_id then
        print(string.format("ERROR: no message ID defined for currency '%s' given to npcUtil.giveCurrency in zone %s.", currency, player:getZoneName()))
        return false
    end

    amount = amount * currency_type[2]

    if currency == "gil" then
        player:addGil(amount)
    else
        player:addCurrency(currency, amount)
    end
    player:messageSpecial(message_id, amount)

    return true
end

--[[ *******************************************************************************
    Give key item(s) to player.
    Message is displayed showing key items obtained.

    Examples of valid keyitems parameter:
        xi.ki.ZERUHN_REPORT
        {xi.ki.PALBOROUGH_MINES_LOGS}
        {xi.ki.BLUE_ACIDITY_TESTER, xi.ki.RED_ACIDITY_TESTER}
******************************************************************************* --]]
function npcUtil.giveKeyItem(player, keyitems)
    local ID = zones[player:getZoneID()]

    -- create table of keyitems
    local givenKeyItems = {}
    if type(keyitems) == "number" then
        givenKeyItems = {keyitems}
    elseif type(keyitems) == "table" then
        givenKeyItems = keyitems
    else
        print(string.format("ERROR: invalid keyitems parameter given to npcUtil.giveKeyItem in zone %s.", player:getZoneName()))
        return false
    end

    -- give key items to player, with message
    for _, v in pairs(givenKeyItems) do
        if not player:hasKeyItem(v) then
            player:addKeyItem(v)
            player:messageSpecial(ID.text.KEYITEM_OBTAINED, v)
        end
    end
    return true
end

--[[ *******************************************************************************
    Complete a quest.
    If quest rewards items, and the player cannot carry them, return false.
    Otherwise, return true.

    Example of usage with params (all params are optional):
        npcUtil.completeQuest(player, SANDORIA, xi.quest.id.sandoria.ROSEL_THE_ARMORER, {
            item = { {640, 2}, 641 },   -- see npcUtil.giveItem for formats
            itemParams = {              -- see npcUtil.giveItem for formats
                fromTrade = true,
            },
            ki = xi.ki.ZERUHN_REPORT,   -- see npcUtil.giveKeyItem for formats
            fameArea = NORG,            -- only needed if the logId table passed as 2nd param doesn't have the fame_area you want
            fame = 120,                 -- fame defaults to 30 if not set
            bayld = 500,
            gil = 200,
            xp = 1000,
            title = xi.title.ENTRANCE_DENIED,
            var = {"foo1", "foo2"}      -- variable(s) to set to 0. string or table
        })
******************************************************************************* --]]
function npcUtil.completeQuest(player, area, quest, params)
    params = params or {}

    -- load text ids
    local ID = zones[player:getZoneID()]

    -- item(s) plus message. return false if player lacks inventory space.
    if params["item"] ~= nil then
        if not npcUtil.giveItem(player, params["item"], params["itemParams"]) then
            return false
        end
    end

    -- key item(s), fame, gil, bayld, xp, and title
    if params["ki"] ~= nil then
        npcUtil.giveKeyItem(player, params["ki"])
    elseif params["keyItem"] ~= nil then
        npcUtil.giveKeyItem(player, params["keyItem"])
    end

    if params["fame"] == nil then
        params["fame"] = 30
    end
    if params["fameArea"] ~= nil and params["fameArea"]["fame_area"] ~= nil and type(params["fame"]) == "number" then
        player:addFame(params["fameArea"], params["fame"])
    elseif (type(area) == "number" or area["fame_area"] ~= nil) and type(params["fame"]) == "number" then
        player:addFame(area, params["fame"])
    end

    if params["gil"] ~= nil and type(params["gil"]) == "number" then
        player:addGil(params["gil"] * xi.settings.GIL_RATE)
        player:messageSpecial(ID.text.GIL_OBTAINED, params["gil"] * xi.settings.GIL_RATE)
    end

    if params["bayld"] ~= nil and type(params["bayld"]) == "number" then
        player:addCurrency('bayld', params["bayld"] * xi.settings.BAYLD_RATE)
        player:messageSpecial(ID.text.BAYLD_OBTAINED, params["bayld"] * xi.settings.BAYLD_RATE)
    end

    if params["xp"] ~= nil and type(params["xp"]) == "number" then
        player:addExp(params["xp"] * xi.settings.EXP_RATE)
    end

    if params["title"] ~= nil then
        player:addTitle(params["title"])
    end

    if params["var"] ~= nil then
        local playerVarsToZero = {}
        if type(params["var"]) == "table" then
            playerVarsToZero = params["var"]
        elseif type(params["var"]) == "string" then
            table.insert(playerVarsToZero, params["var"])
        end
        for _, v in pairs(playerVarsToZero) do
            player:setCharVar(v, 0)
        end
    end

    local logId
    if type(area) == "number" then
        logId = area
    elseif area["quest_log"] then
        logId = area["quest_log"]
    end

    -- successfully complete the quest
    if logId then
        player:completeQuest(logId, quest)
    else
        print("ERROR: invalid logId encountered in npcUtil.completeQuest")
    end

    return true
end

--[[ *******************************************************************************
    Complete a Mission.
    If quest rewards items, and the player cannot carry them, return false.
    Otherwise, return true.

    Example of usage with params (all params are optional):
        npcUtil.completeMission(player, SANDORIA, xi.quest.id.sandoria.ROSEL_THE_ARMORER, {
            item = { {640, 2}, 641 },   -- see npcUtil.giveItem for formats
            itemParams = {              -- see npcUtil.giveItem for formats
                fromTrade = true,
            },
            ki = xi.ki.ZERUHN_REPORT,   -- see npcUtil.giveKeyItem for formats
            bayld = 500,
            gil = 200,
            xp = 1000,
            title = xi.title.ENTRANCE_DENIED,
        })
******************************************************************************* --]]
function npcUtil.completeMission(player, logId, missionId, params)
    params = params or {}

    -- load text ids
    local ID = zones[player:getZoneID()]

    -- item(s) plus message. return false if player lacks inventory space.
    if params["item"] ~= nil then
        if not npcUtil.giveItem(player, params["item"], params["itemParams"]) then
            return false
        end
    end

    -- key item(s), fame, gil, bayld, xp, and title
    if params["ki"] ~= nil then
        npcUtil.giveKeyItem(player, params["ki"])
    elseif params["keyItem"] ~= nil then
        npcUtil.giveKeyItem(player, params["keyItem"])
    end

    if params["gil"] ~= nil and type(params["gil"]) == "number" then
        player:addGil(params["gil"] * xi.settings.GIL_RATE)
        player:messageSpecial(ID.text.GIL_OBTAINED, params["gil"] * xi.settings.GIL_RATE)
    end

    if params["bayld"] ~= nil and type(params["bayld"]) == "number" then
        player:addCurrency('bayld', params["bayld"] * xi.settings.BAYLD_RATE)
        player:messageSpecial(ID.text.BAYLD_OBTAINED, params["bayld"] * xi.settings.BAYLD_RATE)
    end

    if params["xp"] ~= nil and type(params["xp"]) == "number" then
        player:addExp(params["xp"] * xi.settings.EXP_RATE)
    end

    if params["title"] ~= nil then
        player:addTitle(params["title"])
    end

    -- successfully complete the mission
    if logId then
        player:completeMission(logId, missionId)
    else
        print("ERROR: Invalid logId encountered in npcUtil.completeMission")
    end

    if params["rank"] ~= nil and type(params["rank"]) == "number" then
        player:setRank(params["rank"])
        player:setRankPoints(0)
    end

    if params["rankPoints"] ~= nil and type(params["rankPoints"]) == "number" then
        -- TODO: Verify 4000 cap, this was taken from missions.lua
        player:setRankPoints(math.min(player:getRankPoints() + params["rankPoints"], 4000))
    end

    -- TODO: Do we need to support multiple missions being set?
    if
        params["nextMission"] ~= nil and
        type(params["nextMission"][1]) == "number" and
        type(params["nextMission"][2]) == "number"
    then
        player:addMission(params["nextMission"][1], params["nextMission"][2])
    end

    return true
end

--[[ *******************************************************************************
    check whether trade has all required items
        if yes, confirm all the items and return true
        if no, return false

    valid examples of items:
        640                     -- copper ore x1
        { 640, 641 }            -- copper ore x1, tin ore x1
        { 640, 640 }            -- copper ore x2
        { {640, 2} }             -- copper ore x2
        { {640, 2}, 641 }        -- copper ore x2, tin ore x1
        { 640, {"gil", 200} }   -- copper ore x1, gil x200
******************************************************************************* --]]
function npcUtil.tradeHas(trade, items, exact)
    if type(exact) ~= "boolean" then exact = false end

    -- create table of traded items, with key/val of itemId/itemQty
    local tradedItems = {}
    local itemId
    local itemQty
    for i = 0, trade:getSlotCount()-1 do
        itemId = trade:getItemId(i)
        itemQty = trade:getItemQty(itemId)
        tradedItems[itemId] = itemQty
    end

    -- create table of needed items, with key/val of itemId/itemQty
    local neededItems = {}
    if type(items) == "number" then
        neededItems[items] = 1
    elseif type(items) == "table" then
        local itemIdNeeded
        local itemQtyNeeded
        for _, v in pairs(items) do
            if type(v) == "number" then
                itemIdNeeded = v
                itemQtyNeeded = 1
            elseif type(v) == "table" and #v == 2 and type(v[1]) == "number" and type(v[2]) == "number" then
                itemIdNeeded = v[1]
                itemQtyNeeded = v[2]
            elseif type(v) == "table" and #v == 2 and type(v[1]) == "string" and type(v[2]) == "number" and string.lower(v[1]) == "gil" then
                itemIdNeeded = 65535
                itemQtyNeeded = v[2]
            else
                print("ERROR: invalid value contained within items parameter given to npcUtil.tradeHas.")
                itemIdNeeded = nil
            end
            if itemIdNeeded ~= nil then
                neededItems[itemIdNeeded] = (neededItems[itemIdNeeded] == nil) and itemQtyNeeded or neededItems[itemIdNeeded] + itemQtyNeeded
            end
        end
    else
        print("ERROR: invalid items parameter given to npcUtil.tradeHas.")
        return false
    end

    -- determine whether all needed items have been traded. return false if not.
    for k, v in pairs(neededItems) do
        local tradedQty = (tradedItems[k] == nil) and 0 or tradedItems[k]
        if v > tradedQty then
            return false
        else
            tradedItems[k] = tradedQty - v
        end
    end

    -- if an exact trade was requested, check if any excess items were traded. if so, return false.
    if exact then
        for k, v in pairs(tradedItems) do
            if v > 0 then
                return false
            end
        end
    end

    -- confirm items
    for k, v in pairs(neededItems) do
        trade:confirmItem(k, v)
    end
    return true
end

--[[ *******************************************************************************
    check whether trade has exactly required items
        if yes, confirm all the items and return true
        if no, return false

    valid examples of items:
        640                     -- copper ore x1
        { 640, 641 }            -- copper ore x1, tin ore x1
        { 640, 640 }            -- copper ore x2
        { {640, 2} }             -- copper ore x2
        { {640, 2}, 641 }        -- copper ore x2, tin ore x1
        { 640, {"gil", 200} }   -- copper ore x1, gil x200
******************************************************************************* --]]
function npcUtil.tradeHasExactly(trade, items)
    return npcUtil.tradeHas(trade, items, true)
end


-- Checks to see if a trade only contains one item, but the total count can be variable
function npcUtil.tradeHasOnly(trade, itemID)
    return npcUtil.tradeHasExactly(trade, {{ itemID, trade:getItemCount() }})
end

-----------------------------------
-- UpdateNPCSpawnPoint
-----------------------------------

function npcUtil.UpdateNPCSpawnPoint(id, minTime, maxTime, posTable, serverVar)
    local npc = GetNPCByID(id)
    local respawnTime = math.random(minTime, maxTime)
    local newPosition = npcUtil.pickNewPosition(npc:getID(), posTable, true)
    serverVar = serverVar or nil -- serverVar is optional

    if serverVar then
        if GetServerVariable(serverVar) <= os.time() then
            npc:hideNPC(1) -- hide so the NPC is not "moving" through the zone
            npc:setPos(newPosition.x, newPosition.y, newPosition.z)
        end
    end

    npc:timer(respawnTime * 1000, function(npcArg)
        npcUtil.UpdateNPCSpawnPoint(id, minTime, maxTime, posTable, serverVar)
    end)
end

function npcUtil.fishingAnimation(npc, phaseDuration, func)
    func = func or function(npcArg)
        -- return true to not loop again
        return false
    end

    if func(npc) then
        return
    end
    npc:timer(phaseDuration * 1000, function(npcArg)
        local anims =
        {
            [xi.anim.FISHING_NPC] = { duration = 5, nextAnim = { xi.anim.FISHING_START } },
            [xi.anim.FISHING_START] = { duration = 10, nextAnim = { xi.anim.FISHING_FISH } },
            [xi.anim.FISHING_FISH] = { duration = 10,
                                            nextAnim =
                                            {
                                                xi.anim.FISHING_CAUGHT,
                                                xi.anim.FISHING_ROD_BREAK,
                                                xi.anim.FISHING_LINE_BREAK,
                                            }
                                       },
            [xi.anim.FISHING_ROD_BREAK] = { duration = 3, nextAnim = { xi.anim.FISHING_NPC } },
            [xi.anim.FISHING_LINE_BREAK] = { duration = 3, nextAnim = { xi.anim.FISHING_NPC } },
            [xi.anim.FISHING_CAUGHT] = { duration = 5, nextAnim = { xi.anim.FISHING_NPC } },
            [xi.anim.FISHING_STOP] = { duration = 3, nextAnim = { xi.anim.FISHING_NPC } },
        }

        local anim = anims[npcArg:getAnimation()]
        local nextAnimationId = xi.anim.FISHING_NPC
        local nextAnimationDuration = 10
        local nextAnim = nil
        if anim then
            nextAnim = anim.nextAnim[math.random(1, #anim.nextAnim)]
        end

        if nextAnim then
            nextAnimationId = nextAnim
            if anims[nextAnimationId] then
                nextAnimationDuration = anims[nextAnimationId].duration
            end
        end
        npcArg:setAnimation(nextAnimationId)
        npcUtil.fishingAnimation(npcArg, nextAnimationDuration, func)
    end)
end

function npcUtil.castingAnimation(npc, magicType, phaseDuration, func)
    func = func or function(npcArg)
        -- return true to not loop again
        return false
    end

    if func(npc) then
        return
    end
    npc:timer(phaseDuration * 1000, function(npcArg)
        local anims =
        {
            [xi.magic.spellGroup.BLACK] = { start = "cabk", duration = 2000, stop = "shbk" },
            [xi.magic.spellGroup.WHITE] = { start = "cawh", duration = 1800, stop = "shwh" },
        }
        npcArg:entityAnimationPacket(anims[magicType].start)
        npcArg:timer(anims[magicType].duration, function(npcTimerArg)
            npcTimerArg:entityAnimationPacket(anims[magicType].stop)
        end)
        npcUtil.castingAnimation(npcArg, magicType, phaseDuration, func)
    end)
end
