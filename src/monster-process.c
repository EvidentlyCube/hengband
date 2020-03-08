﻿/*!
 * @file monster-process.c
 * @brief モンスターの特殊技能とターン経過処理 (移動等)/ Monster spells and movement for passaging a turn
 * @date 2014/01/17
 * @author
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke\n
 * This software may be copied and distributed for educational, research,\n
 * and not for profit purposes provided that this copyright and statement\n
 * are included in all such copies.  Other copyrights may also apply.\n
 * 2014 Deskull rearranged comment for Doxygen.\n
 * @details
 * This file has several additions to it by Keldon Jones (keldon@umr.edu)
 * to improve the general quality of the AI (version 0.1.1).
 */

#include "angband.h"
#include "util.h"
#include "monster/monster-attack.h"
#include "monster/monster-direction.h"
#include "monster/monster-object.h"
#include "monster/monster-move.h"
#include "monster/monster-runaway.h"
#include "monster/monster-safety-hiding.h"
#include "monster/monster-util.h"
#include "monster/monster-update.h"
#include "monster/quantum-effect.h"

#include "cmd-dump.h"
#include "cmd-pet.h"
#include "creature.h"
#include "melee.h"
#include "spells.h"
#include "spells-floor.h"
#include "spells-summon.h"
#include "avatar.h"
#include "realm-hex.h"
#include "feature.h"
#include "grid.h"
#include "player-move.h"
#include "monster-status.h"
#include "monster-spell.h"
#include "monster-process.h"
#include "monsterrace-hook.h"
#include "floor.h"
#include "files.h"
#include "view-mainwindow.h"

void decide_drop_from_monster(player_type *target_ptr, MONSTER_IDX m_idx, bool is_riding_mon);
bool process_stealth(player_type *target_ptr, MONSTER_IDX m_idx);
bool vanish_summoned_children(player_type *target_ptr, MONSTER_IDX m_idx, bool see_m);
void awake_monster(player_type *target_ptr, MONSTER_IDX m_idx);
void process_angar(player_type *target_ptr, MONSTER_IDX m_idx, bool see_m);
bool explode_grenade(player_type *target_ptr, MONSTER_IDX m_idx);
bool decide_monster_multiplication(player_type *target_ptr, MONSTER_IDX m_idx, POSITION oy, POSITION ox);
void process_special(player_type *target_ptr, MONSTER_IDX m_idx);
void process_speak_sound(player_type *target_ptr, MONSTER_IDX m_idx, POSITION oy, POSITION ox, bool aware);
bool cast_spell(player_type *target_ptr, MONSTER_IDX m_idx, bool aware);

bool process_monster_movement(player_type *target_ptr, turn_flags *turn_flags_ptr, MONSTER_IDX m_idx, DIRECTION *mm, POSITION oy, POSITION ox, int *count);
bool process_post_dig_wall(player_type *target_ptr, turn_flags *turn_flags_ptr, monster_type *m_ptr, POSITION ny, POSITION nx);
bool process_monster_fear(player_type *target_ptr, turn_flags *turn_flags_ptr, MONSTER_IDX m_idx);

void sweep_monster_process(player_type *target_ptr);
bool decide_process_continue(player_type *target_ptr, monster_type *m_ptr);

/*!
 * @brief モンスターがプレイヤーから逃走するかどうかを返す /
 * Returns whether a given monster will try to run from the player.
 * @param m_idx 逃走するモンスターの参照ID
 * @return モンスターがプレイヤーから逃走するならばTRUEを返す。
 * @details
 * Monsters will attempt to avoid very powerful players.  See below.\n
 *\n
 * Because this function is called so often, little details are important\n
 * for efficiency.  Like not using "mod" or "div" when possible.  And\n
 * attempting to check the conditions in an optimal order.  Note that\n
 * "(x << 2) == (x * 4)" if "x" has enough bits to hold the result.\n
 *\n
 * Note that this function is responsible for about one to five percent\n
 * of the processor use in normal conditions...\n
 */
static bool mon_will_run(player_type *target_ptr, MONSTER_IDX m_idx)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];

	if (is_pet(m_ptr))
	{
		return ((target_ptr->pet_follow_distance < 0) &&
			(m_ptr->cdis <= (0 - target_ptr->pet_follow_distance)));
	}

	if (m_ptr->cdis > MAX_SIGHT + 5) return FALSE;
	if (MON_MONFEAR(m_ptr)) return TRUE;
	if (m_ptr->cdis <= 5) return FALSE;

	PLAYER_LEVEL p_lev = target_ptr->lev;
	DEPTH m_lev = r_ptr->level + (m_idx & 0x08) + 25;
	if (m_lev > p_lev + 4) return FALSE;
	if (m_lev + 4 <= p_lev) return TRUE;

	HIT_POINT p_chp = target_ptr->chp;
	HIT_POINT p_mhp = target_ptr->mhp;
	HIT_POINT m_chp = m_ptr->hp;
	HIT_POINT m_mhp = m_ptr->maxhp;
	u32b p_val = (p_lev * p_mhp) + (p_chp << 2);
	u32b m_val = (m_lev * m_mhp) + (m_chp << 2);
	if (p_val * m_mhp > m_val * p_mhp) return TRUE;

	return FALSE;
}


/*!
 * @brief モンスターがプレイヤーに向けて遠距離攻撃を行うことが可能なマスを走査する /
 * Search spell castable grid
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターの参照ID
 * @param yp 適したマスのY座標を返す参照ポインタ
 * @param xp 適したマスのX座標を返す参照ポインタ
 * @return 有効なマスがあった場合TRUEを返す
 */
static bool sweep_ranged_attack_grid(player_type *target_ptr, MONSTER_IDX m_idx, POSITION *yp, POSITION *xp)
{
	floor_type *floor_ptr = target_ptr->current_floor_ptr;
	monster_type *m_ptr = &floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];

	POSITION y1 = m_ptr->fy;
	POSITION x1 = m_ptr->fx;

	if (projectable(target_ptr, y1, x1, target_ptr->y, target_ptr->x)) return FALSE;

	int now_cost = floor_ptr->grid_array[y1][x1].cost;
	if (now_cost == 0) now_cost = 999;

	bool can_open_door = FALSE;
	if (r_ptr->flags2 & (RF2_BASH_DOOR | RF2_OPEN_DOOR))
	{
		can_open_door = TRUE;
	}

	int best = 999;
	for (int i = 7; i >= 0; i--)
	{
		POSITION y = y1 + ddy_ddd[i];
		POSITION x = x1 + ddx_ddd[i];
		if (!in_bounds2(floor_ptr, y, x)) continue;
		if (player_bold(target_ptr, y, x)) return FALSE;

		grid_type *g_ptr;
		g_ptr = &floor_ptr->grid_array[y][x];
		int cost = g_ptr->cost;
		if (!(((r_ptr->flags2 & RF2_PASS_WALL) && ((m_idx != target_ptr->riding) || target_ptr->pass_wall)) || ((r_ptr->flags2 & RF2_KILL_WALL) && (m_idx != target_ptr->riding))))
		{
			if (cost == 0) continue;
			if (!can_open_door && is_closed_door(target_ptr, g_ptr->feat)) continue;
		}

		if (cost == 0) cost = 998;

		if (now_cost < cost) continue;
		if (!projectable(target_ptr, y, x, target_ptr->y, target_ptr->x)) continue;
		if (best < cost) continue;

		best = cost;
		*yp = y1 + ddy_ddd[i];
		*xp = x1 + ddx_ddd[i];
	}

	if (best == 999) return FALSE;

	return TRUE;
}


/*!
 * @brief モンスターがプレイヤーに向けて接近することが可能なマスを走査する /
 * Choose the "best" direction for "flowing"
 * @param m_idx モンスターの参照ID
 * @param yp 移動先のマスのY座標を返す参照ポインタ
 * @param xp 移動先のマスのX座標を返す参照ポインタ
 * @param no_flow モンスターにFLOWフラグが経っていない状態でTRUE
 * @return なし
 * @details
 * Note that ghosts and rock-eaters are never allowed to "flow",\n
 * since they should move directly towards the player.\n
 *\n
 * Prefer "non-diagonal" directions, but twiddle them a little\n
 * to angle slightly towards the player's actual location.\n
 *\n
 * Allow very perceptive monsters to track old "spoor" left by\n
 * previous locations occupied by the player.  This will tend\n
 * to have monsters end up either near the player or on a grid\n
 * recently occupied by the player (and left via "teleport").\n
 *\n
 * Note that if "smell" is turned on, all monsters get vicious.\n
 *\n
 * Also note that teleporting away from a location will cause\n
 * the monsters who were chasing you to converge on that location\n
 * as long as you are still near enough to "annoy" them without\n
 * being close enough to chase directly.  I have no idea what will\n
 * happen if you combine "smell" with low "aaf" values.\n
 */
static void sweep_movable_grid(player_type *target_ptr, MONSTER_IDX m_idx, POSITION *yp, POSITION *xp, bool no_flow)
{
	grid_type *g_ptr;
	floor_type *floor_ptr = target_ptr->current_floor_ptr;
	monster_type *m_ptr = &floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];

	if (r_ptr->flags4 & (RF4_ATTACK_MASK) ||
		r_ptr->a_ability_flags1 & (RF5_ATTACK_MASK) ||
		r_ptr->a_ability_flags2 & (RF6_ATTACK_MASK))
	{
		if (sweep_ranged_attack_grid(target_ptr, m_idx, yp, xp)) return;
	}

	if (no_flow) return;
	if ((r_ptr->flags2 & RF2_PASS_WALL) && ((m_idx != target_ptr->riding) || target_ptr->pass_wall)) return;
	if ((r_ptr->flags2 & RF2_KILL_WALL) && (m_idx != target_ptr->riding)) return;

	POSITION y1 = m_ptr->fy;
	POSITION x1 = m_ptr->fx;
	if (player_has_los_bold(target_ptr, y1, x1) && projectable(target_ptr, target_ptr->y, target_ptr->x, y1, x1)) return;

	g_ptr = &floor_ptr->grid_array[y1][x1];

	int best;
	bool use_scent = FALSE;
	if (g_ptr->cost)
	{
		best = 999;
	}
	else if (g_ptr->when)
	{
		if (floor_ptr->grid_array[target_ptr->y][target_ptr->x].when - g_ptr->when > 127) return;

		use_scent = TRUE;
		best = 0;
	}
	else
	{
		return;
	}

	for (int i = 7; i >= 0; i--)
	{
		POSITION y = y1 + ddy_ddd[i];
		POSITION x = x1 + ddx_ddd[i];

		if (!in_bounds2(floor_ptr, y, x)) continue;

		g_ptr = &floor_ptr->grid_array[y][x];
		if (use_scent)
		{
			int when = g_ptr->when;
			if (best > when) continue;

			best = when;
		}
		else
		{
			int cost;
			if (r_ptr->flags2 & (RF2_BASH_DOOR | RF2_OPEN_DOOR))
			{
				cost = g_ptr->dist;
			}
			else
			{
				cost = g_ptr->cost;
			}

			if ((cost == 0) || (best < cost)) continue;

			best = cost;
		}

		*yp = target_ptr->y + 16 * ddy_ddd[i];
		*xp = target_ptr->x + 16 * ddx_ddd[i];
	}
}


/*!
 * @brief モンスターがプレイヤーから逃走することが可能なマスを走査する /
 * Provide a location to flee to, but give the player a wide berth.
 * @param m_idx モンスターの参照ID
 * @param yp 移動先のマスのY座標を返す参照ポインタ
 * @param xp 移動先のマスのX座標を返す参照ポインタ
 * @return 有効なマスがあった場合TRUEを返す
 * @details
 * A monster may wish to flee to a location that is behind the player,\n
 * but instead of heading directly for it, the monster should "swerve"\n
 * around the player so that he has a smaller chance of getting hit.\n
 */
static bool sweep_runnable_away_grid(floor_type *floor_ptr, MONSTER_IDX m_idx, POSITION *yp, POSITION *xp)
{
	POSITION gy = 0, gx = 0;

	monster_type *m_ptr = &floor_ptr->m_list[m_idx];
	POSITION fy = m_ptr->fy;
	POSITION fx = m_ptr->fx;

	POSITION y1 = fy - (*yp);
	POSITION x1 = fx - (*xp);

	int score = -1;
	for (int i = 7; i >= 0; i--)
	{
		POSITION y = fy + ddy_ddd[i];
		POSITION x = fx + ddx_ddd[i];
		if (!in_bounds2(floor_ptr, y, x)) continue;

		POSITION dis = distance(y, x, y1, x1);
		POSITION s = 5000 / (dis + 3) - 500 / (floor_ptr->grid_array[y][x].dist + 1);
		if (s < 0) s = 0;

		if (s < score) continue;

		score = s;
		gy = y;
		gx = x;
	}

	if (score == -1) return FALSE;

	(*yp) = fy - gy;
	(*xp) = fx - gx;

	return TRUE;
}


/*!
 * todo 分割したいが条件が多すぎて適切な関数名と詳細処理を追いきれない……
 * @brief モンスターの移動方向を返す /
 * Choose "logical" directions for monster movement
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターの参照ID
 * @param mm 移動方向を返す方向IDの参照ポインタ
 * @return 有効方向があった場合TRUEを返す
 */
static bool get_movable_grid(player_type *target_ptr, MONSTER_IDX m_idx, DIRECTION *mm)
{
	floor_type *floor_ptr = target_ptr->current_floor_ptr;
	monster_type *m_ptr = &floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	POSITION y = 0, x = 0;
	POSITION y2 = target_ptr->y;
	POSITION x2 = target_ptr->x;
	bool done = FALSE;
	bool will_run = mon_will_run(target_ptr, m_idx);
	grid_type *g_ptr;
	bool no_flow = ((m_ptr->mflag2 & MFLAG2_NOFLOW) != 0) && (floor_ptr->grid_array[m_ptr->fy][m_ptr->fx].cost > 2);
	bool can_pass_wall = ((r_ptr->flags2 & RF2_PASS_WALL) != 0) && ((m_idx != target_ptr->riding) || target_ptr->pass_wall);

	if (!will_run && m_ptr->target_y)
	{
		int t_m_idx = floor_ptr->grid_array[m_ptr->target_y][m_ptr->target_x].m_idx;
		if ((t_m_idx > 0) &&
			are_enemies(target_ptr, m_ptr, &floor_ptr->m_list[t_m_idx]) &&
			los(target_ptr, m_ptr->fy, m_ptr->fx, m_ptr->target_y, m_ptr->target_x) &&
			projectable(target_ptr, m_ptr->fy, m_ptr->fx, m_ptr->target_y, m_ptr->target_x))
		{
			y = m_ptr->fy - m_ptr->target_y;
			x = m_ptr->fx - m_ptr->target_x;
			done = TRUE;
		}
	}

	if (!done && !will_run && is_hostile(m_ptr) &&
		(r_ptr->flags1 & RF1_FRIENDS) &&
		((los(target_ptr, m_ptr->fy, m_ptr->fx, target_ptr->y, target_ptr->x) && projectable(target_ptr, m_ptr->fy, m_ptr->fx, target_ptr->y, target_ptr->x)) ||
		(floor_ptr->grid_array[m_ptr->fy][m_ptr->fx].dist < MAX_SIGHT / 2)))
	{
		if ((r_ptr->flags3 & RF3_ANIMAL) && !can_pass_wall &&
			!(r_ptr->flags2 & RF2_KILL_WALL))
		{
			int room = 0;
			for (int i = 0; i < 8; i++)
			{
				int xx = target_ptr->x + ddx_ddd[i];
				int yy = target_ptr->y + ddy_ddd[i];

				if (!in_bounds2(floor_ptr, yy, xx)) continue;

				g_ptr = &floor_ptr->grid_array[yy][xx];
				if (monster_can_cross_terrain(target_ptr, g_ptr->feat, r_ptr, 0))
				{
					room++;
				}
			}

			if (floor_ptr->grid_array[target_ptr->y][target_ptr->x].info & CAVE_ROOM) room -= 2;
			if (!r_ptr->flags4 && !r_ptr->a_ability_flags1 && !r_ptr->a_ability_flags2) room -= 2;

			if (room < (8 * (target_ptr->chp + target_ptr->csp)) /
				(target_ptr->mhp + target_ptr->msp))
			{
				if (find_hiding(target_ptr, m_idx, &y, &x)) done = TRUE;
			}
		}

		if (!done && (floor_ptr->grid_array[m_ptr->fy][m_ptr->fx].dist < 3))
		{
			for (int i = 0; i < 8; i++)
			{
				y2 = target_ptr->y + ddy_ddd[(m_idx + i) & 7];
				x2 = target_ptr->x + ddx_ddd[(m_idx + i) & 7];
				if ((m_ptr->fy == y2) && (m_ptr->fx == x2))
				{
					y2 = target_ptr->y;
					x2 = target_ptr->x;
					break;
				}

				if (!in_bounds2(floor_ptr, y2, x2)) continue;
				if (!monster_can_enter(target_ptr, y2, x2, r_ptr, 0)) continue;

				break;
			}

			y = m_ptr->fy - y2;
			x = m_ptr->fx - x2;
			done = TRUE;
		}
	}

	if (!done)
	{
		sweep_movable_grid(target_ptr, m_idx, &y2, &x2, no_flow);
		y = m_ptr->fy - y2;
		x = m_ptr->fx - x2;
	}

	if (is_pet(m_ptr) && will_run)
	{
		y = (-y), x = (-x);
	}
	else
	{
		if (!done && will_run)
		{
			int tmp_x = (-x);
			int tmp_y = (-y);
			if (find_safety(target_ptr, m_idx, &y, &x) && !no_flow)
			{
				if (sweep_runnable_away_grid(target_ptr->current_floor_ptr, m_idx, &y, &x))
					done = TRUE;
			}

			if (!done)
			{
				y = tmp_y;
				x = tmp_x;
			}
		}
	}

	if (!x && !y) return FALSE;

	store_moves_val(mm, y, x);
	return TRUE;
}


/*!
 * @brief モンスター単体の1ターン行動処理メインルーチン /
 * Process a monster
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx 行動モンスターの参照ID
 * @return なし
 * @details
 * The monster is known to be within 100 grids of the player\n
 *\n
 * In several cases, we directly update the monster lore\n
 *\n
 * Note that a monster is only allowed to "reproduce" if there\n
 * are a limited number of "reproducing" monsters on the current\n
 * level.  This should prevent the level from being "swamped" by\n
 * reproducing monsters.  It also allows a large mass of mice to\n
 * prevent a louse from multiplying, but this is a small price to\n
 * pay for a simple multiplication method.\n
 *\n
 * XXX Monster fear is slightly odd, in particular, monsters will\n
 * fixate on opening a door even if they cannot open it.  Actually,\n
 * the same thing happens to normal monsters when they hit a door\n
 *\n
 * In addition, monsters which *cannot* open or bash\n
 * down a door will still stand there trying to open it...\n
 *\n
 * XXX Technically, need to check for monster in the way\n
 * combined with that monster being in a wall (or door?)\n
 *\n
 * A "direction" of "5" means "pick a random direction".\n
 */
void process_monster(player_type *target_ptr, MONSTER_IDX m_idx)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	turn_flags tmp_flags;
	turn_flags *turn_flags_ptr = init_turn_flags(target_ptr->riding, m_idx, &tmp_flags);
	turn_flags_ptr->see_m = is_seen(m_ptr);

	decide_drop_from_monster(target_ptr, m_idx, turn_flags_ptr->is_riding_mon);
	if ((m_ptr->mflag2 & MFLAG2_CHAMELEON) && one_in_(13) && !MON_CSLEEP(m_ptr))
	{
		choose_new_monster(target_ptr, m_idx, FALSE, 0);
		r_ptr = &r_info[m_ptr->r_idx];
	}

	turn_flags_ptr->aware = process_stealth(target_ptr, m_idx);
	if (vanish_summoned_children(target_ptr, m_idx, turn_flags_ptr->see_m)) return;
	if (process_quantum_effect(target_ptr, m_idx, turn_flags_ptr->see_m)) return;
	if (explode_grenade(target_ptr, m_idx)) return;
	if (runaway_monster(target_ptr, turn_flags_ptr, m_idx)) return;

	awake_monster(target_ptr, m_idx);
	if (MON_STUNNED(m_ptr))
	{
		if (one_in_(2)) return;
	}

	if (turn_flags_ptr->is_riding_mon)
	{
		target_ptr->update |= (PU_BONUS);
	}

	process_angar(target_ptr, m_idx, turn_flags_ptr->see_m);

	POSITION oy = m_ptr->fy;
	POSITION ox = m_ptr->fx;
	if (decide_monster_multiplication(target_ptr, m_idx, oy, ox)) return;

	process_special(target_ptr, m_idx);
	process_speak_sound(target_ptr, m_idx, oy, ox, turn_flags_ptr->aware);
	if (cast_spell(target_ptr, m_idx, turn_flags_ptr->aware)) return;

	DIRECTION mm[8];
	mm[0] = mm[1] = mm[2] = mm[3] = 0;
	mm[4] = mm[5] = mm[6] = mm[7] = 0;

	if (!decide_monster_movement_direction(target_ptr, mm, m_idx, turn_flags_ptr->aware, get_movable_grid)) return;

	int count = 0;
	if (!process_monster_movement(target_ptr, turn_flags_ptr, m_idx, mm, oy, ox, &count)) return;

	/*
	 *  Forward movements failed, but now received LOS attack!
	 *  Try to flow by smell.
	 */
	if (target_ptr->no_flowed && count > 2 && m_ptr->target_y)
		m_ptr->mflag2 &= ~MFLAG2_NOFLOW;

	if (!turn_flags_ptr->do_turn && !turn_flags_ptr->do_move && !MON_MONFEAR(m_ptr) && !turn_flags_ptr->is_riding_mon && turn_flags_ptr->aware)
	{
		if (r_ptr->freq_spell && randint1(100) <= r_ptr->freq_spell)
		{
			if (make_attack_spell(m_idx, target_ptr)) return;
		}
	}

	update_player_type(target_ptr, turn_flags_ptr, r_ptr);
	update_monster_race_flags(target_ptr, turn_flags_ptr, m_ptr);

	if (!process_monster_fear(target_ptr, turn_flags_ptr, m_idx)) return;

	if (m_ptr->ml) chg_virtue(target_ptr, V_COMPASSION, -1);
}


/*!
 * @brief 超隠密処理
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @return モンスターがプレーヤーに気付いているならばTRUE、超隠密状態ならばFALSE
 */
bool process_stealth(player_type *target_ptr, MONSTER_IDX m_idx)
{
	if ((target_ptr->special_defense & NINJA_S_STEALTH) == 0) return TRUE;

	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	int tmp = target_ptr->lev * 6 + (target_ptr->skill_stl + 10) * 4;
	if (target_ptr->monlite) tmp /= 3;
	if (target_ptr->cursed & TRC_AGGRAVATE) tmp /= 2;
	if (r_ptr->level > (target_ptr->lev * target_ptr->lev / 20 + 10)) tmp /= 3;
	return (randint0(tmp) <= (r_ptr->level + 20));
}


/*!
 * @brief 死亡したモンスターが乗馬中のモンスターだった場合に落馬処理を行う
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @param is_riding_mon 騎乗中であればTRUE
 * @return なし
 */
void decide_drop_from_monster(player_type *target_ptr, MONSTER_IDX m_idx, bool is_riding_mon)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	if (!is_riding_mon || ((r_ptr->flags7 & RF7_RIDING) != 0)) return;

	if (rakuba(target_ptr, 0, TRUE))
	{
#ifdef JP
		msg_print("地面に落とされた。");
#else
		GAME_TEXT m_name[MAX_NLEN];
		monster_desc(target_ptr, m_name, &target_ptr->current_floor_ptr->m_list[target_ptr->riding], 0);
		msg_format("You have fallen from %s.", m_name);
#endif
	}
}


/*!
 * @brief 召喚の親元が消滅した時、子供も消滅させる
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @param see_m モンスターが視界内にいたらTRUE
 * @return 召喚モンスターが消滅したらTRUE
 */
bool vanish_summoned_children(player_type *target_ptr, MONSTER_IDX m_idx, bool see_m)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	if ((m_ptr->parent_m_idx == 0) || (target_ptr->current_floor_ptr->m_list[m_ptr->parent_m_idx].r_idx > 0))
		return FALSE;

	if (see_m)
	{
		GAME_TEXT m_name[MAX_NLEN];
		monster_desc(target_ptr, m_name, m_ptr, 0);
		msg_format(_("%sは消え去った！", "%^s disappears!"), m_name);
	}

	if (record_named_pet && is_pet(m_ptr) && m_ptr->nickname)
	{
		GAME_TEXT m_name[MAX_NLEN];
		monster_desc(target_ptr, m_name, m_ptr, MD_INDEF_VISIBLE);
		exe_write_diary(target_ptr, DIARY_NAMED_PET, RECORD_NAMED_PET_LOSE_PARENT, m_name);
	}

	delete_monster_idx(target_ptr, m_idx);
	return TRUE;
}


/*!
 * @brief 寝ているモンスターの起床を判定する
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @return なし
 */
void awake_monster(player_type *target_ptr, MONSTER_IDX m_idx)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	if (!MON_CSLEEP(m_ptr)) return;
	if (!(target_ptr->cursed & TRC_AGGRAVATE)) return;

	(void)set_monster_csleep(target_ptr, m_idx, 0);
	if (m_ptr->ml)
	{
		GAME_TEXT m_name[MAX_NLEN];
		monster_desc(target_ptr, m_name, m_ptr, 0);
		msg_format(_("%^sが目を覚ました。", "%^s wakes up."), m_name);
	}

	if (is_original_ap_and_seen(target_ptr, m_ptr) && (r_ptr->r_wake < MAX_UCHAR))
	{
		r_ptr->r_wake++;
	}
}


/*!
 * @brief モンスターの怒り状態を判定する (起こっていたら敵に回す)
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @param see_m モンスターが視界内にいたらTRUE
 * @return なし
 */
void process_angar(player_type *target_ptr, MONSTER_IDX m_idx, bool see_m)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	bool gets_angry = FALSE;
	if (is_friendly(m_ptr) && (target_ptr->cursed & TRC_AGGRAVATE))
		gets_angry = TRUE;

	if (is_pet(m_ptr) && ((((r_ptr->flags1 & RF1_UNIQUE) || (r_ptr->flags7 & RF7_NAZGUL)) &&
		monster_has_hostile_align(target_ptr, NULL, 10, -10, r_ptr)) || (r_ptr->flagsr & RFR_RES_ALL)))
	{
		gets_angry = TRUE;
	}

	if (target_ptr->phase_out || !gets_angry) return;

	if (is_pet(m_ptr) || see_m)
	{
		GAME_TEXT m_name[MAX_NLEN];
		monster_desc(target_ptr, m_name, m_ptr, is_pet(m_ptr) ? MD_ASSUME_VISIBLE : 0);
		msg_format(_("%^sは突然敵にまわった！", "%^s suddenly becomes hostile!"), m_name);
	}

	set_hostile(target_ptr, m_ptr);
}


/*!
 * @brief 手榴弾の爆発処理
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @return 爆死したらTRUE
 */
bool explode_grenade(player_type *target_ptr, MONSTER_IDX m_idx)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	if (m_ptr->r_idx != MON_GRENADE) return FALSE;

	bool fear, dead;
	mon_take_hit_mon(target_ptr, m_idx, 1, &dead, &fear, _("は爆発して粉々になった。", " explodes into tiny shreds."), m_idx);
	return dead;
}


/*!
 * @brief モンスター依存の特別な行動を取らせる
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @return なし
 */
void process_special(player_type *target_ptr, MONSTER_IDX m_idx)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	if ((r_ptr->a_ability_flags2 & RF6_SPECIAL) == 0) return;
	if (m_ptr->r_idx != MON_OHMU) return;
	if (target_ptr->current_floor_ptr->inside_arena || target_ptr->phase_out) return;
	if ((r_ptr->freq_spell == 0) || !(randint1(100) <= r_ptr->freq_spell)) return;

	int count = 0;
	DEPTH rlev = ((r_ptr->level >= 1) ? r_ptr->level : 1);
	BIT_FLAGS p_mode = is_pet(m_ptr) ? PM_FORCE_PET : 0L;

	for (int k = 0; k < A_MAX; k++)
	{
		if (summon_specific(target_ptr, m_idx, m_ptr->fy, m_ptr->fx, rlev, SUMMON_MOLD, (PM_ALLOW_GROUP | p_mode)))
		{
			if (target_ptr->current_floor_ptr->m_list[hack_m_idx_ii].ml) count++;
		}
	}

	if (count && is_original_ap_and_seen(target_ptr, m_ptr)) r_ptr->r_flags6 |= (RF6_SPECIAL);
}


/*!
 * @brief モンスターを喋らせたり足音を立てたりする
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @param oy モンスターが元々いたY座標
 * @param ox モンスターが元々いたX座標
 * @param aware モンスターがプレーヤーに気付いているならばTRUE、超隠密状態ならばFALSE
 * @return なし
 */
void process_speak_sound(player_type *target_ptr, MONSTER_IDX m_idx, POSITION oy, POSITION ox, bool aware)
{
	if (target_ptr->phase_out) return;

	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *ap_r_ptr = &r_info[m_ptr->ap_r_idx];
	if (m_ptr->ap_r_idx == MON_CYBER &&
		one_in_(CYBERNOISE) &&
		!m_ptr->ml && (m_ptr->cdis <= MAX_SIGHT))
	{
		if (disturb_minor) disturb(target_ptr, FALSE, FALSE);
		msg_print(_("重厚な足音が聞こえた。", "You hear heavy steps."));
	}

	if (((ap_r_ptr->flags2 & RF2_CAN_SPEAK) == 0) || !aware ||
		!one_in_(SPEAK_CHANCE) ||
		!player_has_los_bold(target_ptr, oy, ox) ||
		!projectable(target_ptr, oy, ox, target_ptr->y, target_ptr->x))
		return;

	GAME_TEXT m_name[MAX_NLEN];
	char monmessage[1024];
	concptr filename;

	if (m_ptr->ml)
		monster_desc(target_ptr, m_name, m_ptr, 0);
	else
		strcpy(m_name, _("それ", "It"));

	if (MON_MONFEAR(m_ptr))
		filename = _("monfear_j.txt", "monfear.txt");
	else if (is_pet(m_ptr))
		filename = _("monpet_j.txt", "monpet.txt");
	else if (is_friendly(m_ptr))
		filename = _("monfrien_j.txt", "monfrien.txt");
	else
		filename = _("monspeak_j.txt", "monspeak.txt");

	if (get_rnd_line(filename, m_ptr->ap_r_idx, monmessage) == 0)
	{
		msg_format(_("%^s%s", "%^s %s"), m_name, monmessage);
	}
}


/*!
 * @brief モンスターを分裂させるかどうかを決定する (分裂もさせる)
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @param oy 分裂元モンスターのY座標
 * @param ox 分裂元モンスターのX座標
 * @return 実際に分裂したらTRUEを返す
 */
bool decide_monster_multiplication(player_type *target_ptr, MONSTER_IDX m_idx, POSITION oy, POSITION ox)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	if (((r_ptr->flags2 & RF2_MULTIPLY) == 0) || (target_ptr->current_floor_ptr->num_repro >= MAX_REPRO))
		return FALSE;

	int k = 0;
	for (POSITION y = oy - 1; y <= oy + 1; y++)
	{
		for (POSITION x = ox - 1; x <= ox + 1; x++)
		{
			if (!in_bounds2(target_ptr->current_floor_ptr, y, x)) continue;
			if (target_ptr->current_floor_ptr->grid_array[y][x].m_idx) k++;
		}
	}

	if (multiply_barrier(target_ptr, m_idx)) k = 8;

	if ((k < 4) && (!k || !randint0(k * MON_MULT_ADJ)))
	{
		if (multiply_monster(target_ptr, m_idx, FALSE, (is_pet(m_ptr) ? PM_FORCE_PET : 0)))
		{
			if (target_ptr->current_floor_ptr->m_list[hack_m_idx_ii].ml && is_original_ap_and_seen(target_ptr, m_ptr))
			{
				r_ptr->r_flags2 |= (RF2_MULTIPLY);
			}

			return TRUE;
		}
	}

	return FALSE;
}


/*!
 * @brief モンスターに魔法を試行させる
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_idx モンスターID
 * @param aware モンスターがプレーヤーに気付いているならばTRUE、超隠密状態ならばFALSE
 * @return 魔法を唱えられなければ強制的にFALSE、その後モンスターが実際に魔法を唱えればTRUE
 */
bool cast_spell(player_type *target_ptr, MONSTER_IDX m_idx, bool aware)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	monster_race *r_ptr = &r_info[m_ptr->r_idx];
	if ((r_ptr->freq_spell == 0) || (randint1(100) > r_ptr->freq_spell))
		return FALSE;

	bool counterattack = FALSE;
	if (m_ptr->target_y)
	{
		MONSTER_IDX t_m_idx = target_ptr->current_floor_ptr->grid_array[m_ptr->target_y][m_ptr->target_x].m_idx;
		if (t_m_idx && are_enemies(target_ptr, m_ptr, &target_ptr->current_floor_ptr->m_list[t_m_idx]) &&
			projectable(target_ptr, m_ptr->fy, m_ptr->fx, m_ptr->target_y, m_ptr->target_x))
		{
			counterattack = TRUE;
		}
	}

	if (counterattack)
	{
		if (monst_spell_monst(target_ptr, m_idx)) return TRUE;
		if (aware && make_attack_spell(m_idx, target_ptr)) return TRUE;
	}
	else
	{
		if (aware && make_attack_spell(m_idx, target_ptr)) return TRUE;
		if (monst_spell_monst(target_ptr, m_idx)) return TRUE;
	}

	return FALSE;
}


/*!
 * todo 少し長いが、これといってブロックとしてまとまった部分もないので暫定でこのままとする
 * @brief モンスターの移動に関するメインルーチン
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param turn_flags_ptr ターン経過処理フラグへの参照ポインタ
 * @param m_idx モンスターID
 * @param mm モンスターの移動方向
 * @param oy 移動前の、モンスターのY座標
 * @param ox 移動前の、モンスターのX座標
 * @param count 移動回数 (のはず todo)
 * @return 移動が阻害される何か (ドア等)があったらFALSE
 */
bool process_monster_movement(player_type *target_ptr, turn_flags *turn_flags_ptr, MONSTER_IDX m_idx, DIRECTION *mm, POSITION oy, POSITION ox, int *count)
{
	for (int i = 0; mm[i]; i++)
	{
		int d = mm[i];
		if (d == 5) d = ddd[randint0(8)];

		POSITION ny = oy + ddy[d];
		POSITION nx = ox + ddx[d];
		if (!in_bounds2(target_ptr->current_floor_ptr, ny, nx)) continue;

		grid_type *g_ptr;
		g_ptr = &target_ptr->current_floor_ptr->grid_array[ny][nx];
		monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
		monster_race *r_ptr = &r_info[m_ptr->r_idx];
		bool can_cross = monster_can_cross_terrain(target_ptr, g_ptr->feat, r_ptr, turn_flags_ptr->is_riding_mon ? CEM_RIDING : 0);

		if (!process_wall(target_ptr, turn_flags_ptr, m_ptr, ny, nx, can_cross))
		{
			if (!process_door(target_ptr, turn_flags_ptr, m_ptr, ny, nx))
				return FALSE;
		}

		if (!process_protection_rune(target_ptr, turn_flags_ptr, m_ptr, ny, nx))
		{
			if (!process_explosive_rune(target_ptr, turn_flags_ptr, m_ptr, ny, nx))
				return FALSE;
		}

		exe_monster_attack_to_player(target_ptr, turn_flags_ptr, m_idx, ny, nx);
		if (process_monster_attack_to_monster(target_ptr, turn_flags_ptr, m_idx, g_ptr, can_cross)) return FALSE;

		if (turn_flags_ptr->is_riding_mon)
		{
			if (!target_ptr->riding_ryoute && !MON_MONFEAR(&target_ptr->current_floor_ptr->m_list[target_ptr->riding])) turn_flags_ptr->do_move = FALSE;
		}

		if (!process_post_dig_wall(target_ptr, turn_flags_ptr, m_ptr, ny, nx)) return FALSE;

		if (turn_flags_ptr->must_alter_to_move && (r_ptr->flags7 & RF7_AQUATIC))
		{
			if (!monster_can_cross_terrain(target_ptr, g_ptr->feat, r_ptr, turn_flags_ptr->is_riding_mon ? CEM_RIDING : 0))
				turn_flags_ptr->do_move = FALSE;
		}

		if (turn_flags_ptr->do_move && !can_cross && !turn_flags_ptr->did_kill_wall && !turn_flags_ptr->did_bash_door)
			turn_flags_ptr->do_move = FALSE;

		if (turn_flags_ptr->do_move && (r_ptr->flags1 & RF1_NEVER_MOVE))
		{
			if (is_original_ap_and_seen(target_ptr, m_ptr))
				r_ptr->r_flags1 |= (RF1_NEVER_MOVE);

			turn_flags_ptr->do_move = FALSE;
		}

		if (!turn_flags_ptr->do_move)
		{
			if (turn_flags_ptr->do_turn) break;

			continue;
		}

		turn_flags_ptr->do_turn = TRUE;
		feature_type *f_ptr;
		f_ptr = &f_info[g_ptr->feat];
		if (have_flag(f_ptr->flags, FF_TREE))
		{
			if (!(r_ptr->flags7 & RF7_CAN_FLY) && !(r_ptr->flags8 & RF8_WILD_WOOD))
			{
				m_ptr->energy_need += ENERGY_NEED();
			}
		}

		if (!update_riding_monster(target_ptr, turn_flags_ptr, m_idx, oy, ox, ny, nx)) break;

		monster_race *ap_r_ptr = &r_info[m_ptr->ap_r_idx];
		if (m_ptr->ml &&
			(disturb_move ||
			(disturb_near && (m_ptr->mflag & MFLAG_VIEW) && projectable(target_ptr, target_ptr->y, target_ptr->x, m_ptr->fy, m_ptr->fx)) ||
				(disturb_high && ap_r_ptr->r_tkills && ap_r_ptr->level >= target_ptr->lev)))
		{
			if (is_hostile(m_ptr))
				disturb(target_ptr, FALSE, TRUE);
		}

		bool is_takable_or_killable = g_ptr->o_idx > 0;
		is_takable_or_killable &= (r_ptr->flags2 & (RF2_TAKE_ITEM | RF2_KILL_ITEM)) != 0;

		bool is_pickup_items = (target_ptr->pet_extra_flags & PF_PICKUP_ITEMS) != 0;
		is_pickup_items &= (r_ptr->flags2 & RF2_TAKE_ITEM) != 0;

		is_takable_or_killable &= !is_pet(m_ptr) || is_pickup_items;
		if (!is_takable_or_killable)
		{
			if (turn_flags_ptr->do_turn) break;

			continue;
		}

		update_object_by_monster_movement(target_ptr, turn_flags_ptr, m_idx, ny, nx);
		if (turn_flags_ptr->do_turn) break;

		(*count)++;
	}

	return TRUE;
}


/*!
 * @brief モンスターの恐怖状態を処理する
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param turn_flags_ptr ターン経過処理フラグへの参照ポインタ
 * @param m_idx モンスターID
 * @param aware モンスターがプレーヤーに気付いているならばTRUE、超隠密状態ならばFALSE
 * @return モンスターが戦いを決意したらTRUE
 */
bool process_monster_fear(player_type *target_ptr, turn_flags *turn_flags_ptr, MONSTER_IDX m_idx)
{
	monster_type *m_ptr = &target_ptr->current_floor_ptr->m_list[m_idx];
	bool is_battle_determined = !turn_flags_ptr->do_turn && !turn_flags_ptr->do_move && MON_MONFEAR(m_ptr) && turn_flags_ptr->aware;
	if (!is_battle_determined) return FALSE;

	(void)set_monster_monfear(target_ptr, m_idx, 0);
	if (!turn_flags_ptr->see_m) return TRUE;

	GAME_TEXT m_name[MAX_NLEN];
	monster_desc(target_ptr, m_name, m_ptr, 0);
	msg_format(_("%^sは戦いを決意した！", "%^s turns to fight!"), m_name);
	return TRUE;
}


/*!
 * @brief 全モンスターのターン管理メインルーチン /
 * Process all the "live" monsters, once per game turn.
 * @return なし
 * @details
 * During each game current game turn, we scan through the list of all the "live" monsters,\n
 * (backwards, so we can excise any "freshly dead" monsters), energizing each\n
 * monster, and allowing fully energized monsters to move, attack, pass, etc.\n
 *\n
 * Note that monsters can never move in the monster array (except when the\n
 * "compact_monsters()" function is called by "dungeon()" or "save_player()").\n
 *\n
 * This function is responsible for at least half of the processor time\n
 * on a normal system with a "normal" amount of monsters and a player doing\n
 * normal things.\n
 *\n
 * When the player is resting, virtually 90% of the processor time is spent\n
 * in this function, and its children, "process_monster()" and "make_move()".\n
 *\n
 * Most of the rest of the time is spent in "update_view()" and "lite_spot()",\n
 * especially when the player is running.\n
 *\n
 * Note the special "MFLAG_BORN" flag, which allows us to ignore "fresh"\n
 * monsters while they are still being "born".  A monster is "fresh" only\n
 * during the game turn in which it is created, and we use the "hack_m_idx" to\n
 * determine if the monster is yet to be processed during the game turn.\n
 *\n
 * Note the special "MFLAG_NICE" flag, which allows the player to get one\n
 * move before any "nasty" monsters get to use their spell attacks.\n
 *\n
 * Note that when the "knowledge" about the currently tracked monster\n
 * changes (flags, attacks, spells), we induce a redraw of the monster\n
 * recall window.\n
 */
void process_monsters(player_type *target_ptr)
{
	old_race_flags tmp_flags;
	old_race_flags *old_race_flags_ptr = init_old_race_flags(&tmp_flags);

	floor_type *floor_ptr = target_ptr->current_floor_ptr;
	floor_ptr->monster_noise = FALSE;

	MONRACE_IDX old_monster_race_idx = target_ptr->monster_race_idx;
	save_old_race_flags(target_ptr->monster_race_idx, old_race_flags_ptr);
	sweep_monster_process(target_ptr);

	hack_m_idx = 0;
	if (!target_ptr->monster_race_idx || (target_ptr->monster_race_idx != old_monster_race_idx))
		return;

	update_player_window(target_ptr, old_race_flags_ptr);
}


/*!
 * @brief フロア内のモンスターについてターン終了時の処理を繰り返す
 * @param target_ptr プレーヤーへの参照ポインタ
 */
void sweep_monster_process(player_type *target_ptr)
{
	floor_type *floor_ptr = target_ptr->current_floor_ptr;
	for (MONSTER_IDX i = floor_ptr->m_max - 1; i >= 1; i--)
	{
		monster_type *m_ptr;
		m_ptr = &floor_ptr->m_list[i];

		if (target_ptr->leaving) return;
		if (!monster_is_valid(m_ptr)) continue;
		if (target_ptr->wild_mode) continue;

		if (m_ptr->mflag & MFLAG_BORN)
		{
			m_ptr->mflag &= ~(MFLAG_BORN);
			continue;
		}

		if (m_ptr->cdis >= AAF_LIMIT) continue;
		if (!decide_process_continue(target_ptr, m_ptr)) continue;

		SPEED speed = (target_ptr->riding == i) ? target_ptr->pspeed : decide_monster_speed(m_ptr);
		m_ptr->energy_need -= SPEED_TO_ENERGY(speed);
		if (m_ptr->energy_need > 0) continue;

		m_ptr->energy_need += ENERGY_NEED();
		hack_m_idx = i;
		process_monster(target_ptr, i);
		reset_target(m_ptr);

		if (target_ptr->no_flowed && one_in_(3))
			m_ptr->mflag2 |= MFLAG2_NOFLOW;

		if (!target_ptr->playing || target_ptr->is_dead) return;
		if (target_ptr->leaving) return;
	}
}


/*!
 * @brief 後続のモンスター処理が必要かどうか判定する (要調査)
 * @param target_ptr プレーヤーへの参照ポインタ
 * @param m_ptr モンスターへの参照ポインタ
 * @return 後続処理が必要ならTRUE
 */
bool decide_process_continue(player_type *target_ptr, monster_type *m_ptr)
{
	monster_race *r_ptr;
	r_ptr = &r_info[m_ptr->r_idx];
	if (!target_ptr->no_flowed)
	{
		m_ptr->mflag2 &= ~MFLAG2_NOFLOW;
	}

	if (m_ptr->cdis <= (is_pet(m_ptr) ? (r_ptr->aaf > MAX_SIGHT ? MAX_SIGHT : r_ptr->aaf) : r_ptr->aaf))
		return TRUE;

	if ((m_ptr->cdis <= MAX_SIGHT || target_ptr->phase_out) &&
		(player_has_los_bold(target_ptr, m_ptr->fy, m_ptr->fx) || (target_ptr->cursed & TRC_AGGRAVATE)))
		return TRUE;

	if (m_ptr->target_y)
		return TRUE;

	return FALSE;
}
