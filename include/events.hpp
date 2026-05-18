#pragma once

#include "types.hpp"
#include <vector>

struct EventBus {
    struct AttackEvent {};
    struct ScoreEvent {
        bool good_hit;
        int enemy_count;
    };
    struct EnemyDeathEvent {
        Vec2 pos;
        Vec2 hit_dir;
    };
    
    std::vector<AttackEvent> attacks;
    std::vector<ScoreEvent> scores;
    std::vector<EnemyDeathEvent> deaths;
    
    void clear();
};
