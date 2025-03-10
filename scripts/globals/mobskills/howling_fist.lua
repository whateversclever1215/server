-----------------------------------
-- Howling Fist
-- Description: Damage varies with TP.
-- Type: Physical
-----------------------------------
require("scripts/globals/settings")
require("scripts/globals/status")
require("scripts/globals/monstertpmoves")
-----------------------------------
local mobskill_object = {}

mobskill_object.onMobSkillCheck = function(target,mob,skill)
    return 0
end

mobskill_object.onMobWeaponSkill = function(target, mob, skill)
    local numhits = 2
    local accmod = 1
    local dmgmod = 1
    local info = MobPhysicalMove(mob,target,skill,numhits,accmod,dmgmod,TP_DMG_VARIES,2.5,2.75,3)
    local dmg = MobFinalAdjustments(info.dmg,mob,skill,target, xi.attackType.PHYSICAL, xi.damageType.H2H,info.hitslanded)

    target:takeDamage(dmg, mob, xi.attackType.PHYSICAL, xi.damageType.H2H)
    return dmg
end

return mobskill_object
