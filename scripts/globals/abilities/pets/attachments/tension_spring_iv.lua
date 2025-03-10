-----------------------------------
-- Attachment: Tension Spring IV
-----------------------------------
require("scripts/globals/automaton")
require("scripts/globals/status")
-----------------------------------
local attachment_object = {}

attachment_object.onEquip = function(pet)
    attachment_object.onUpdate(pet, 0)
end

attachment_object.onUnequip = function(pet)
    updateModPerformance(pet, xi.mod.ATTP, 'tension_iv_attp', 0)
    updateModPerformance(pet, xi.mod.RATTP, 'tension_iv_rattp', 0)
end

attachment_object.onManeuverGain = function(pet, maneuvers)
    attachment_object.onUpdate(pet, maneuvers)
end

attachment_object.onManeuverLose = function(pet, maneuvers)
    attachment_object.onUpdate(pet, maneuvers - 1)
end

attachment_object.onUpdate = function(pet, maneuvers)
    if maneuvers == 0 then
        updateModPerformance(pet, xi.mod.ATTP, 'tension_iv_attp', 15)
        updateModPerformance(pet, xi.mod.RATTP, 'tension_iv_rattp', 15)
    elseif maneuvers == 1 then
        updateModPerformance(pet, xi.mod.ATTP, 'tension_iv_attp', 18)
        updateModPerformance(pet, xi.mod.RATTP, 'tension_iv_rattp', 18)
    elseif maneuvers == 2 then
        updateModPerformance(pet, xi.mod.ATTP, 'tension_iv_attp', 21)
        updateModPerformance(pet, xi.mod.RATTP, 'tension_iv_rattp', 21)
    elseif maneuvers == 3 then
        updateModPerformance(pet, xi.mod.ATTP, 'tension_iv_attp', 24)
        updateModPerformance(pet, xi.mod.RATTP, 'tension_iv_rattp', 24)
    end
end

return attachment_object
