﻿/*!
 * @brief 武器/防具/アクセサリアイテムにおける、耐性やスレイ等の表記
 * @date 2020/07/06
 * @author Hourier
 */

#include "flavor/flavor-describer.h"
#include "combat/shoot.h"
#include "flavor/flag-inscriptions-table.h"
#include "flavor/flavor-util.h"
#include "flavor/named-item-describer.h"
#include "flavor/object-flavor-types.h"
#include "game-option/text-display-options.h"
#include "grid/trap.h"
#include "inventory/inventory-slot-types.h"
#include "mind/mind-sniper.h"
#include "mind/mind-weaponsmith.h"
#include "object-enchant/object-ego.h"
#include "object-enchant/special-object-flags.h"
#include "object-enchant/tr-types.h"
#include "object-enchant/trg-types.h"
#include "object-hook/hook-quest.h"
#include "object/object-flags.h"
#include "object/object-kind.h"
#include "perception/object-perception.h"
#include "player-base/player-class.h"
#include "player/player-status-table.h"
#include "player/player-status.h"
#include "smith/object-smith.h"
#include "smith/smith-types.h"
#include "specific-object/bow.h"
#include "sv-definition/sv-lite-types.h"
#include "sv-definition/sv-weapon-types.h"
#include "system/floor-type-definition.h"
#include "system/object-type-definition.h"
#include "system/player-type-definition.h"
#include "util/bit-flags-calculator.h"
#include "util/string-processor.h"
#include "window/display-sub-window-items.h"

static void describe_chest_trap(flavor_type *flavor_ptr)
{
    auto trap_kinds = chest_traps[flavor_ptr->o_ptr->pval];
    if (trap_kinds.count() >= 2) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(マルチ・トラップ)", " (Multiple Traps)"));
        return;
    }

    auto trap_kind = trap_kinds.first();
    if (!trap_kind.has_value()) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(施錠)", " (Locked)"));
        return;
    }

    switch (trap_kind.value()) {
    case ChestTrapType::LOSE_STR:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(毒針)", " (Poison Needle)"));
        break;
    case ChestTrapType::LOSE_CON:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(毒針)", " (Poison Needle)"));
        break;
    case ChestTrapType::POISON:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(ガス・トラップ)", " (Gas Trap)"));
        break;
    case ChestTrapType::PARALYZE:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(ガス・トラップ)", " (Gas Trap)"));
        break;
    case ChestTrapType::EXPLODE:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(爆発装置)", " (Explosive Device)"));
        break;
    case ChestTrapType::SUMMON:
    case ChestTrapType::BIRD_STORM:
    case ChestTrapType::E_SUMMON:
    case ChestTrapType::H_SUMMON:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(召喚のルーン)", " (Summoning Runes)"));
        break;
    case ChestTrapType::RUNES_OF_EVIL:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(邪悪なルーン)", " (Gleaming Black Runes)"));
        break;
    case ChestTrapType::ALARM:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(警報装置)", " (Alarm)"));
        break;
    case ChestTrapType::SCATTER:
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(アイテム散乱)", " (Scatter)"));
        break;
    case ChestTrapType::MAX:
        throw("Invalid chest trap type is specified!");
    }
}

static void describe_chest(flavor_type *flavor_ptr)
{
    if (flavor_ptr->o_ptr->tval != ItemKindType::CHEST) {
        return;
    }

    if (!flavor_ptr->known) {
        return;
    }

    if (!flavor_ptr->o_ptr->pval) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(空)", " (empty)"));
        return;
    }

    if (flavor_ptr->o_ptr->pval < 0) {
        if (chest_traps[0 - flavor_ptr->o_ptr->pval].any()) {
            flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(解除済)", " (disarmed)"));
        } else {
            flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(非施錠)", " (unlocked)"));
        }

        return;
    }

    describe_chest_trap(flavor_ptr);
}

static void decide_tval_show(flavor_type *flavor_ptr)
{
    if (flavor_ptr->tr_flags.has(TR_SHOW_MODS)) {
        flavor_ptr->show_weapon = true;
    }

    if (flavor_ptr->o_ptr->is_smith() && (Smith::object_effect(flavor_ptr->o_ptr) == SmithEffectType::SLAY_GLOVE)) {
        flavor_ptr->show_weapon = true;
    }

    if (flavor_ptr->o_ptr->to_h && flavor_ptr->o_ptr->to_d) {
        flavor_ptr->show_weapon = true;
    }

    if (flavor_ptr->o_ptr->ac) {
        flavor_ptr->show_armour = true;
    }
}

static void describe_weapon_dice(PlayerType *player_ptr, flavor_type *flavor_ptr)
{
    if (!flavor_ptr->known && object_is_quest_target(player_ptr->current_floor_ptr->quest_number, flavor_ptr->o_ptr)) {
        return;
    }

    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
    auto is_bonus = (player_ptr->riding > 0) && flavor_ptr->o_ptr->is_lance();
    auto bonus = is_bonus ? 2 : 0;
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->o_ptr->dd + bonus);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, 'd');
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->o_ptr->ds);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
}

static void describe_bow(PlayerType *player_ptr, flavor_type *flavor_ptr)
{
    flavor_ptr->power = bow_tmul(flavor_ptr->o_ptr->sval);
    if (flavor_ptr->tr_flags.has(TR_XTRA_MIGHT)) {
        flavor_ptr->power++;
    }

    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, 'x');
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->power);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);

    int num_fire = 100;
    if (!(flavor_ptr->mode & OD_DEBUG)) {
        num_fire = calc_num_fire(player_ptr, flavor_ptr->o_ptr);
    } else {
        auto flgs = object_flags(flavor_ptr->o_ptr);
        if (flgs.has(TR_XTRA_SHOTS)) {
            num_fire += 100;
        }
    }
    if ((num_fire == 0) || (flavor_ptr->power <= 0) || !flavor_ptr->known) {
        return;
    }

    flavor_ptr->fire_rate = bow_energy(flavor_ptr->o_ptr->sval) / num_fire;
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->fire_rate / 100);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, '.');
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->fire_rate % 100);
    flavor_ptr->t = object_desc_str(flavor_ptr->t, "turn");
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
}

static void describe_tval(PlayerType *player_ptr, flavor_type *flavor_ptr)
{
    switch (flavor_ptr->o_ptr->tval) {
    case ItemKindType::SHOT:
    case ItemKindType::BOLT:
    case ItemKindType::ARROW:
    case ItemKindType::HAFTED:
    case ItemKindType::POLEARM:
    case ItemKindType::SWORD:
    case ItemKindType::DIGGING:
        describe_weapon_dice(player_ptr, flavor_ptr);
        break;
    case ItemKindType::BOW:
        describe_bow(player_ptr, flavor_ptr);
        break;

    default:
        break;
    }
}

static void describe_named_item_tval(flavor_type *flavor_ptr)
{
    if (!flavor_ptr->known) {
        return;
    }

    if (flavor_ptr->show_weapon) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
        flavor_ptr->t = object_desc_int(flavor_ptr->t, flavor_ptr->o_ptr->to_h);
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, ',');
        flavor_ptr->t = object_desc_int(flavor_ptr->t, flavor_ptr->o_ptr->to_d);
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
        return;
    }

    if (flavor_ptr->o_ptr->to_h != 0) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
        flavor_ptr->t = object_desc_int(flavor_ptr->t, flavor_ptr->o_ptr->to_h);
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
        return;
    }

    if (flavor_ptr->o_ptr->to_d != 0) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
        flavor_ptr->t = object_desc_int(flavor_ptr->t, flavor_ptr->o_ptr->to_d);
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
    }
}

static void describe_fire_energy(PlayerType *player_ptr, flavor_type *flavor_ptr)
{
    ENERGY energy_fire = bow_energy(flavor_ptr->bow_ptr->sval);
    if (player_ptr->num_fire == 0) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, '0');
        return;
    }

    flavor_ptr->avgdam *= (player_ptr->num_fire * 100);
    flavor_ptr->avgdam /= energy_fire;
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->avgdam);
    flavor_ptr->t = object_desc_str(flavor_ptr->t, show_ammo_detail ? "/turn" : "");
    if (!show_ammo_crit_ratio) {
        return;
    }

    int percent = calc_crit_ratio_shot(player_ptr, flavor_ptr->known ? flavor_ptr->o_ptr->to_h : 0, flavor_ptr->known ? flavor_ptr->bow_ptr->to_h : 0);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, '/');
    flavor_ptr->t = object_desc_num(flavor_ptr->t, percent / 100);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, '.');
    if (percent % 100 < 10) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, '0');
    }

    flavor_ptr->t = object_desc_num(flavor_ptr->t, percent % 100);
    flavor_ptr->t = object_desc_str(flavor_ptr->t, show_ammo_detail ? "% crit" : "%");
}

static void describe_bow_power(PlayerType *player_ptr, flavor_type *flavor_ptr)
{
    flavor_ptr->avgdam = flavor_ptr->o_ptr->dd * (flavor_ptr->o_ptr->ds + 1) * 10 / 2;
    int tmul = bow_tmul(flavor_ptr->bow_ptr->sval);
    if (flavor_ptr->bow_ptr->is_known()) {
        flavor_ptr->avgdam += (flavor_ptr->bow_ptr->to_d * 10);
    }

    if (flavor_ptr->known) {
        flavor_ptr->avgdam += (flavor_ptr->o_ptr->to_d * 10);
    }

    if (player_ptr->xtra_might) {
        tmul++;
    }

    tmul = tmul * (100 + (int)(adj_str_td[player_ptr->stat_index[A_STR]]) - 128);
    flavor_ptr->avgdam *= tmul;
    flavor_ptr->avgdam /= (100 * 10);
    flavor_ptr->avgdam = boost_concentration_damage(player_ptr, flavor_ptr->avgdam);

    if (flavor_ptr->avgdam < 0) {
        flavor_ptr->avgdam = 0;
    }

    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
    if (show_ammo_no_crit) {
        flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->avgdam);
        flavor_ptr->t = object_desc_str(flavor_ptr->t, show_ammo_detail ? "/shot " : "/");
    }

    flavor_ptr->avgdam = calc_expect_crit_shot(player_ptr, flavor_ptr->o_ptr->weight, flavor_ptr->o_ptr->to_h, flavor_ptr->bow_ptr->to_h, flavor_ptr->avgdam);
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->avgdam);
    flavor_ptr->t = show_ammo_no_crit ? object_desc_str(flavor_ptr->t, show_ammo_detail ? "/crit " : "/")
                                      : object_desc_str(flavor_ptr->t, show_ammo_detail ? "/shot " : "/");
    describe_fire_energy(player_ptr, flavor_ptr);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
}

static void describe_spike_power(PlayerType *player_ptr, flavor_type *flavor_ptr)
{
    int avgdam = player_ptr->mighty_throw ? (1 + 3) : 1;
    int16_t energy_fire = 100 - player_ptr->lev;
    avgdam += ((player_ptr->lev + 30) * (player_ptr->lev + 30) - 900) / 55;
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
    flavor_ptr->t = object_desc_num(flavor_ptr->t, avgdam);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, '/');
    avgdam = 100 * avgdam / energy_fire;
    flavor_ptr->t = object_desc_num(flavor_ptr->t, avgdam);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
}

static void describe_known_item_ac(flavor_type *flavor_ptr)
{
    if (flavor_ptr->show_armour) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->b1);
        flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->o_ptr->ac);
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, ',');
        flavor_ptr->t = object_desc_int(flavor_ptr->t, flavor_ptr->o_ptr->to_a);
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->b2);
        return;
    }

    if (flavor_ptr->o_ptr->to_a == 0) {
        return;
    }

    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->b1);
    flavor_ptr->t = object_desc_int(flavor_ptr->t, flavor_ptr->o_ptr->to_a);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->b2);
}

static void describe_ac(flavor_type *flavor_ptr)
{
    if (flavor_ptr->known) {
        describe_known_item_ac(flavor_ptr);
        return;
    }

    if (!flavor_ptr->show_armour) {
        return;
    }

    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->b1);
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->o_ptr->ac);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->b2);
}

static void describe_charges_staff_wand(flavor_type *flavor_ptr)
{
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
    if ((flavor_ptr->o_ptr->tval == ItemKindType::STAFF) && (flavor_ptr->o_ptr->number > 1)) {
        flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->o_ptr->number);
        flavor_ptr->t = object_desc_str(flavor_ptr->t, "x ");
    }

    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->o_ptr->pval);
    flavor_ptr->t = object_desc_str(flavor_ptr->t, _("回分", " charge"));
#ifdef JP
#else
    if (flavor_ptr->o_ptr->pval != 1) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, 's');
    }
#endif

    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
}

static void describe_charges_rod(flavor_type *flavor_ptr)
{
    if (flavor_ptr->o_ptr->timeout == 0) {
        return;
    }

    if (flavor_ptr->o_ptr->number <= 1) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(充填中)", " (charging)"));
        return;
    }

    if (flavor_ptr->k_ptr->pval == 0) {
        flavor_ptr->k_ptr->pval = 1;
    }

    flavor_ptr->power = (flavor_ptr->o_ptr->timeout + (flavor_ptr->k_ptr->pval - 1)) / flavor_ptr->k_ptr->pval;
    if (flavor_ptr->power > flavor_ptr->o_ptr->number) {
        flavor_ptr->power = flavor_ptr->o_ptr->number;
    }

    flavor_ptr->t = object_desc_str(flavor_ptr->t, " (");
    flavor_ptr->t = object_desc_num(flavor_ptr->t, flavor_ptr->power);
    flavor_ptr->t = object_desc_str(flavor_ptr->t, _("本 充填中)", " charging)"));
}

static void describe_specific_pval(flavor_type *flavor_ptr)
{
    if (flavor_ptr->tr_flags.has(TR_SPEED)) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("加速", " to speed"));
        return;
    }

    if (flavor_ptr->tr_flags.has(TR_BLOWS)) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("攻撃", " attack"));
#ifdef JP
#else
        if (std::abs(flavor_ptr->o_ptr->pval) != 1) {
            flavor_ptr->t = object_desc_chr(flavor_ptr->t, 's');
        }
#endif

        return;
    }

    if (flavor_ptr->tr_flags.has(TR_STEALTH)) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("隠密", " to stealth"));
        return;
    }

    if (flavor_ptr->tr_flags.has(TR_SEARCH)) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("探索", " to searching"));
        return;
    }

    if (flavor_ptr->tr_flags.has(TR_INFRA)) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("赤外線視力", " to infravision"));
    }
}

static void describe_pval(flavor_type *flavor_ptr)
{
    if (flavor_ptr->tr_flags.has_none_of(TR_PVAL_FLAG_MASK)) {
        return;
    }

    flavor_ptr->t = object_desc_chr(flavor_ptr->t, ' ');
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p1);
    flavor_ptr->t = object_desc_int(flavor_ptr->t, flavor_ptr->o_ptr->pval);
    if (flavor_ptr->tr_flags.has(TR_HIDE_TYPE)) {
        flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
        return;
    }

    describe_specific_pval(flavor_ptr);
    flavor_ptr->t = object_desc_chr(flavor_ptr->t, flavor_ptr->p2);
}

static void describe_lamp_life(flavor_type *flavor_ptr)
{
    if ((flavor_ptr->o_ptr->tval != ItemKindType::LITE) || (flavor_ptr->o_ptr->is_fixed_artifact() || (flavor_ptr->o_ptr->sval == SV_LITE_FEANOR))) {
        return;
    }

    flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(", " (with "));
    auto fuel_magnification = flavor_ptr->o_ptr->ego_idx == EgoType::LITE_LONG ? 2 : 1;
    flavor_ptr->t = object_desc_num(flavor_ptr->t, fuel_magnification * flavor_ptr->o_ptr->fuel);
    flavor_ptr->t = object_desc_str(flavor_ptr->t, _("ターンの寿命)", " turns of light)"));
}

/*!
 * @brief 杖や光源等、寿命のあるアイテムの残り回数やターン表記
 * @param アイテム表記への参照ポインタ
 */
static void describe_remaining(flavor_type *flavor_ptr)
{
    if (!flavor_ptr->known) {
        return;
    }

    if (((flavor_ptr->o_ptr->tval == ItemKindType::STAFF) || (flavor_ptr->o_ptr->tval == ItemKindType::WAND))) {
        describe_charges_staff_wand(flavor_ptr);
    } else if (flavor_ptr->o_ptr->tval == ItemKindType::ROD) {
        describe_charges_rod(flavor_ptr);
    }

    describe_pval(flavor_ptr);
    describe_lamp_life(flavor_ptr);
    if (flavor_ptr->o_ptr->timeout && (flavor_ptr->o_ptr->tval != ItemKindType::ROD)) {
        flavor_ptr->t = object_desc_str(flavor_ptr->t, _("(充填中)", " (charging)"));
    }
}

static void decide_item_feeling(flavor_type *flavor_ptr)
{
    flavor_ptr->fake_insc_buf[0] = '\0';
    if (flavor_ptr->o_ptr->feeling) {
        strcpy(flavor_ptr->fake_insc_buf, game_inscriptions[flavor_ptr->o_ptr->feeling]);
        return;
    }

    if (flavor_ptr->o_ptr->is_cursed() && (flavor_ptr->known || (flavor_ptr->o_ptr->ident & IDENT_SENSE))) {
        strcpy(flavor_ptr->fake_insc_buf, _("呪われている", "cursed"));
        return;
    }

    if (((flavor_ptr->o_ptr->tval == ItemKindType::RING) || (flavor_ptr->o_ptr->tval == ItemKindType::AMULET) || (flavor_ptr->o_ptr->tval == ItemKindType::LITE) || (flavor_ptr->o_ptr->tval == ItemKindType::FIGURINE)) && flavor_ptr->aware && !flavor_ptr->known && !(flavor_ptr->o_ptr->ident & IDENT_SENSE)) {
        strcpy(flavor_ptr->fake_insc_buf, _("未鑑定", "unidentified"));
        return;
    }

    if (!flavor_ptr->known && (flavor_ptr->o_ptr->ident & IDENT_EMPTY)) {
        strcpy(flavor_ptr->fake_insc_buf, _("空", "empty"));
        return;
    }

    if (!flavor_ptr->aware && flavor_ptr->o_ptr->is_tried()) {
        strcpy(flavor_ptr->fake_insc_buf, _("未判明", "tried"));
    }
}

/*!
 * @brief オブジェクトの各表記を返すメイン関数 / Creates a description of the item "o_ptr", and stores it in "out_val".
 * @param player_ptr プレイヤーへの参照ポインタ
 * @param buf 表記を返すための文字列参照ポインタ
 * @param o_ptr 特性短縮表記を得たいオブジェクト構造体の参照ポインタ
 * @param mode 表記に関するオプション指定
 * @return 現在クエスト達成目的のアイテムならばTRUEを返す
 */
void describe_flavor(PlayerType *player_ptr, char *buf, ObjectType *o_ptr, BIT_FLAGS mode)
{
    flavor_type tmp_flavor;
    flavor_type *flavor_ptr = initialize_flavor_type(&tmp_flavor, buf, o_ptr, mode);
    describe_named_item(player_ptr, flavor_ptr);
    if (flavor_ptr->mode & OD_NAME_ONLY || o_ptr->k_idx == 0) {
        angband_strcpy(flavor_ptr->buf, flavor_ptr->tmp_val, MAX_NLEN);
        return;
    }

    describe_chest(flavor_ptr);
    decide_tval_show(flavor_ptr);
    describe_tval(player_ptr, flavor_ptr);
    describe_named_item_tval(flavor_ptr);
    if (!(mode & OD_DEBUG)) {
        flavor_ptr->bow_ptr = &player_ptr->inventory_list[INVEN_BOW];
        if ((flavor_ptr->bow_ptr->k_idx != 0) && (flavor_ptr->o_ptr->tval == bow_tval_ammo(flavor_ptr->bow_ptr))) {
            describe_bow_power(player_ptr, flavor_ptr);
        } else if (PlayerClass(player_ptr).equals(PlayerClassType::NINJA) && (flavor_ptr->o_ptr->tval == ItemKindType::SPIKE)) {
            describe_spike_power(player_ptr, flavor_ptr);
        }
    }

    describe_ac(flavor_ptr);
    if (flavor_ptr->mode & OD_NAME_AND_ENCHANT) {
        angband_strcpy(flavor_ptr->buf, flavor_ptr->tmp_val, MAX_NLEN);
        return;
    }

    describe_remaining(flavor_ptr);
    if (flavor_ptr->mode & OD_OMIT_INSCRIPTION) {
        angband_strcpy(flavor_ptr->buf, flavor_ptr->tmp_val, MAX_NLEN);
        return;
    }

    display_short_flavors(flavor_ptr);
    decide_item_feeling(flavor_ptr);
    display_item_discount(flavor_ptr);
    display_item_fake_inscription(flavor_ptr);
    angband_strcpy(flavor_ptr->buf, flavor_ptr->tmp_val, MAX_NLEN);
}
