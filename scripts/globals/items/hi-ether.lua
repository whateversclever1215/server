-----------------------------------
-- ID: 4132
-- Item: Hi-Ether
-- Item Effect: Restores 50 MP
-----------------------------------
require("scripts/globals/settings")
require("scripts/globals/msg")
-----------------------------------
local item_object = {}

item_object.onItemCheck = function(target)
    if (target:getMP() == target:getMaxMP()) then
        return xi.msg.basic.ITEM_UNABLE_TO_USE
    end
    return 0
end

item_object.onItemUse = function(target)
    target:messageBasic(xi.msg.basic.RECOVERS_MP, 0, target:addMP(50*xi.settings.ITEM_POWER))
end

return item_object
