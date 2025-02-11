﻿#include "object-hook/hook-quest.h"
#include "cmd-building/cmd-building.h"
#include "dungeon/quest.h"
#include "game-option/birth-options.h"
#include "monster-race/monster-race.h"
#include "monster-race/race-indice-types.h"
#include "object-enchant/trg-types.h"
#include "system/artifact-type-definition.h"
#include "system/floor-type-definition.h"
#include "system/monster-race-definition.h"
#include "system/object-type-definition.h"
#include "system/player-type-definition.h"
#include "world/world.h"

/*!
 * @brief オブジェクトが賞金首の報酬対象になるかを返す
 * @param o_ptr 対象のオブジェクト構造体ポインタ
 * @return オブジェクトが報酬対象になるならTRUEを返す
 */
bool object_is_bounty(PlayerType *player_ptr, ObjectType *o_ptr)
{
    if (o_ptr->tval != ItemKindType::CORPSE) {
        return false;
    }

    if (vanilla_town) {
        return false;
    }

    auto corpse_r_idx = i2enum<MonsterRaceId>(o_ptr->pval);
    if (player_ptr->knows_daily_bounty && (streq(r_info[corpse_r_idx].name.c_str(), r_info[w_ptr->today_mon].name.c_str()))) {
        return true;
    }

    if (corpse_r_idx == MonsterRaceId::TSUCHINOKO) {
        return true;
    }

    return MonsterRace(corpse_r_idx).is_bounty(true);
}

/*!
 * @brief オブジェクトがクエストの達成目的か否かを返す。
 * @param o_ptr 特性短縮表記を得たいオブジェクト構造体の参照ポインタ
 * @return 現在クエスト達成目的のアイテムならばTRUEを返す。
 */
bool object_is_quest_target(QuestId quest_idx, ObjectType *o_ptr)
{
    if (!inside_quest(quest_idx)) {
        return false;
    }

    const auto &quest_list = QuestList::get_instance();
    auto a_idx = quest_list[quest_idx].k_idx;
    if (a_idx == 0) {
        return false;
    }

    auto *a_ptr = &a_info[a_idx];
    if (a_ptr->gen_flags.has(ItemGenerationTraitType::INSTA_ART)) {
        return false;
    }

    return (o_ptr->tval == a_ptr->tval) && (o_ptr->sval == a_ptr->sval);
}
