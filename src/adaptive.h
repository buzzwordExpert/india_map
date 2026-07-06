#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#include <vector>
#include <algorithm>
#include <cmath>

enum Lang {
  LANG_HINDI,
  LANG_ENGLISH
};

extern float currentDifficulty;

// Difficulty for Q1-Q87
const float difficultyEnglish[87] = {
  // Q1-Q10
  1,1,1,1,1,1,1,1,1,1,

  // Q11-Q20
  2,2,2,2,2,2,2,2,2,2,

  // Q21-Q30
  3,3,3,3,3,3,3,3,3,3,

  // Q31-Q40
  4,4,4,4,4,4,4,4,4,4,

  // Q41-Q50
  5,5,5,5,5,5,5,5,5,5,

  // Q51-Q60
  6,6,6,6,6,6,6,6,6,6,

  // Q61-Q70
  7,7,7,7,7,7,7,7,7,7,

  // Q71-Q80
  8,8,8,8,8,8,8,8,8,8,

  // Q81-Q87
  9,9,9,9,9,9,9
};

int findAdaptiveUnaskedQuestion(
  Lang lang,
  bool* asked,
  int totalAsked,
  uint8_t total)
{
  if (totalAsked >= total)
    return -1;

  // Hindi → random
  if (lang == LANG_HINDI) {

    std::vector<int> available;

    for (int i = 0; i < total; i++) {
      if (!asked[i])
        available.push_back(i);
    }

    if (available.empty())
      return -1;

    return available[random(available.size())];
  }

  // English → adaptive
  std::vector<int> candidates;
  float delta = 0.0f;

  while (candidates.empty() && delta <= 10.0f) {

    for (int i = 0; i < total; i++) {

      if (asked[i])
        continue;

      if (fabs(difficultyEnglish[i] - currentDifficulty) <= delta)
        candidates.push_back(i);
    }

    delta += 0.5f;
  }

  if (candidates.empty()) {

    for (int i = 0; i < total; i++) {
      if (!asked[i])
        candidates.push_back(i);
    }
  }

  if (candidates.empty())
    return -1;

  return candidates[random(candidates.size())];
}

#endif