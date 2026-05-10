#pragma once

#include <vector>

struct EventBus {
    struct AttackEvent {};
    struct ScoreEvent {
        bool good_hit;
        int enemy_count;
    };
    
    std::vector<AttackEvent> attacks;
    std::vector<ScoreEvent> scores;
    
    void clear();
};
