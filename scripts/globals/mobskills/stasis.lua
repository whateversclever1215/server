-----------------------------------
-- Stasis
--
-- Description: Paralyzes targets in an area of effect.
-- Type: Enfeebling
-- Utsusemi/Blink absorb: Ignores shadows
-- Range: 10' radial
-- Notes:
-----------------------------------
require("scripts/globals/monstertpmoves")
require("scripts/globals/settings")
require("scripts/globals/status")
-----------------------------------
local mobskill_object = {}

mobskill_object.onMobSkillCheck = function(target, mob, skill)
    return 0
end

mobskill_object.onMobWeaponSkill = function(target, mob, skill)
    local shadows = MOBPARAM_1_SHADOW
    -- local dmg = MobFinalAdjustments(10, mob, skill, target, xi.attackType.PHYSICAL, xi.damageType.BLUNT, shadows)
    local typeEffect = xi.effect.PARALYSIS

    mob:resetEnmity(target)

    if (MobPhysicalHit(skill)) then
        skill:setMsg(MobStatusEffectMove(mob, target, typeEffect, 40, 0, 60))
        return typeEffect
    end

    return shadows
end

return mobskill_object
