#pragma once
#include <array>
#include <unordered_map>

static const std::unordered_map<int, std::array<float, 5>> Q_TABLE = {
    {1, {1.000000000f, 0.410230100f, 0.390550792f, 0.322812796f, 0.331335813f}},
};

static const int Q_TABLE_MAX_STATE = 1;
static const int Q_TABLE_NUM_ACTIONS = 5;