﻿#pragma once

#include "system/angband.h"

struct mind_type;
struct player_type;
class MindPowerGetter {
public:
    MindPowerGetter(player_type *caster_ptr);
    virtual ~MindPowerGetter() = default;
    bool get_mind_power(SPELL_IDX *sn, bool only_browse);

private:
    player_type *caster_ptr;
    SPELL_IDX index;
    int num;
    TERM_LEN y;
    TERM_LEN x;
    bool ask;
    char choice;
    concptr mind_description;
    const mind_type *spell;
    bool flag;
    bool redraw;
    int use_mind;
    int menu_line;

    void select_mind_description();
};
