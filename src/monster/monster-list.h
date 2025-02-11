﻿#pragma once

#include "system/angband.h"

#define GMN_ARENA 0x00000001 //!< 賭け闘技場向け生成

enum class MonsterRaceId : int16_t;
struct floor_type;
struct monster_race;
class PlayerType;
MONSTER_IDX m_pop(floor_type *floor_ptr);

MonsterRaceId get_mon_num(PlayerType *player_ptr, DEPTH min_level, DEPTH max_level, BIT_FLAGS option);
void choose_new_monster(PlayerType *player_ptr, MONSTER_IDX m_idx, bool born, MonsterRaceId r_idx);
byte get_mspeed(floor_type *player_ptr, monster_race *r_ptr);
int get_monster_crowd_number(floor_type *floor_ptr, MONSTER_IDX m_idx);
