/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "EventRecorder.h"
#include "Creature.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"

class EventRecorderWorldScript : public WorldScript
{
public:
    EventRecorderWorldScript() : WorldScript("EventRecorderWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD
    }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        sEventRecorder->LoadConfig();
    }
};

class EventRecorderUnitScript : public UnitScript
{
public:
    EventRecorderUnitScript() : UnitScript("EventRecorderUnitScript", true, {
        UNITHOOK_ON_DAMAGE,
        UNITHOOK_ON_HEAL,
        UNITHOOK_ON_AURA_APPLY,
        UNITHOOK_ON_AURA_REMOVE,
        UNITHOOK_ON_UNIT_ENTER_COMBAT,
        UNITHOOK_ON_UNIT_ENTER_EVADE_MODE,
        UNITHOOK_ON_UNIT_DEATH
    }) { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!sEventRecorder->IsActive())
            return;

        sEventRecorder->RecordDamage(attacker, victim, damage);
    }

    void OnHeal(Unit* healer, Unit* reciever, uint32& gain) override
    {
        if (!sEventRecorder->IsActive())
            return;

        sEventRecorder->RecordHeal(healer, reciever, gain);
    }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!sEventRecorder->IsActive())
            return;

        sEventRecorder->RecordAuraApply(unit, aura);
    }

    void OnAuraRemove(Unit* unit, AuraApplication* aurApp, AuraRemoveMode mode) override
    {
        if (!sEventRecorder->IsActive())
            return;

        sEventRecorder->RecordAuraRemove(unit, aurApp, static_cast<uint8>(mode));
    }

    void OnUnitEnterCombat(Unit* unit, Unit* victim) override
    {
        if (!sEventRecorder->IsActive())
            return;

        sEventRecorder->RecordEnterCombat(unit, victim);
    }

    void OnUnitEnterEvadeMode(Unit* unit, uint8 evadeReason) override
    {
        if (!sEventRecorder->IsActive())
            return;

        sEventRecorder->RecordEvade(unit, evadeReason);
    }

    void OnUnitDeath(Unit* unit, Unit* killer) override
    {
        if (!sEventRecorder->IsActive())
            return;

        sEventRecorder->RecordUnitDeath(unit, killer);
    }
};

class EventRecorderPlayerScript : public PlayerScript
{
public:
    EventRecorderPlayerScript() : PlayerScript("EventRecorderPlayerScript", {
        PLAYERHOOK_ON_SPELL_CAST
    }) { }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!sEventRecorder->IsActive())
            return;

        if (!spell)
            return;

        sEventRecorder->RecordSpellCast(player, spell->GetSpellInfo());
    }
};

void AddSC_event_recorder_hooks()
{
    new EventRecorderWorldScript();
    new EventRecorderUnitScript();
    new EventRecorderPlayerScript();
}
