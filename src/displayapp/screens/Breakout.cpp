#include "displayapp/screens/Breakout.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <FreeRTOS.h>
#include <task.h>

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr int16_t ScreenW = 240;
  constexpr int16_t ScreenH = 240;
  constexpr int16_t BorderTopY = 23;
  constexpr int16_t BrickOriginX = 7;
  constexpr int16_t BrickOriginY = 27;
  constexpr int16_t BrickPitchX = 19;
  constexpr int16_t BrickPitchY = 14;
  constexpr int16_t BrickW = 17;
  constexpr int16_t BrickH = 12;

  constexpr int16_t PaddleW = 52;
  constexpr int16_t PaddleH = 7;
  constexpr int16_t PaddleY = 226;
  constexpr int16_t PaddleMinX = 2;
  constexpr int16_t PaddleMaxX = ScreenW - 2 - PaddleW;

  constexpr int16_t BallSize = 8;
  constexpr int32_t LeftWallQ4 = 1 * 16;
  constexpr int32_t RightWallQ4 = (ScreenW - 1 - BallSize) * 16;
  constexpr int32_t TopWallQ4 = (BorderTopY + 1) * 16;

  constexpr int32_t DeadZoneMg = 50;
  constexpr int32_t MaxTiltExcessMg = 400;
  constexpr uint8_t SubStepsPerTick = 2;

  struct LevelDef {
    const char* name;
    uint16_t rows[Breakout::GridRows];
  };

  // 8 pixel-art levels (12x8), bit 11 = leftmost col 0, bit 0 = rightmost col 11
  constexpr LevelDef Levels[Breakout::NumLevels] = {
    {"SMILEY",
     {
       0b001111111100,
       0b010000000010,
       0b100100001001,
       0b100000000001,
       0b101000000101,
       0b100111111001,
       0b010000000010,
       0b001111111100,
     }},
    {"POO",
     {
       0b000001100000,
       0b000011110000,
       0b000111111000,
       0b001101101100,
       0b011111111110,
       0b011100001110,
       0b111111111111,
       0b011111111110,
     }},
    {"INVADER",
     {
       0b001000000100,
       0b000100001000,
       0b001111111100,
       0b011011110110,
       0b111111111111,
       0b101111111101,
       0b101000000101,
       0b000110011000,
     }},
    {"HEART",
     {
       0b011100001110,
       0b111110011111,
       0b111111111111,
       0b111111111111,
       0b011111111110,
       0b001111111100,
       0b000011110000,
       0b000001100000,
     }},
    {"GHOST",
     {
       0b000111111000,
       0b001111111100,
       0b011001100110,
       0b011001100110,
       0b011111111110,
       0b011111111110,
       0b011111111110,
       0b010110011010,
     }},
    {"SKULL",
     {
       0b001111111100,
       0b011111111110,
       0b011001100110,
       0b011001100110,
       0b011111111110,
       0b001110011100,
       0b001111111100,
       0b001010010100,
     }},
    {"PINE",
     {
       0b000001100000,
       0b000011110000,
       0b000111111000,
       0b001111111100,
       0b011111111110,
       0b111111111111,
       0b000001100000,
       0b000011110000,
     }},
    {"CROWN",
     {
       0b100001100001,
       0b110011110011,
       0b111011110111,
       0b111111111111,
       0b110110011011,
       0b111111111111,
       0b011111111110,
       0b011111111110,
     }},
  };

  // Pure black & white 17x12 masks for HP=2 (some cracks) and HP=1 (many cracks)
  constexpr uint32_t BrickMaskSomeCracks[BrickH] = {
    0b11111001111111111U,
    0b11111100111111111U,
    0b11111110111111111U,
    0b11111100111111111U,
    0b11111001111111111U,
    0b11110111111111111U,
    0b11111011111111111U,
    0b11111100111111111U,
    0b11111110011111111U,
    0b11111111011111111U,
    0b11111111100111111U,
    0b11111111110011111U,
  };

  constexpr uint32_t BrickMaskManyCracks[BrickH] = {
    0b11110001110001111U,
    0b11100110011100111U,
    0b11001111001100011U,
    0b10011001100111001U,
    0b00001110011000100U,
    0b11100110001110011U,
    0b11001110010011100U,
    0b10011100111001101U,
    0b00111001100111000U,
    0b11000110011000111U,
    0b11100110001100111U,
    0b11110001110001111U,
  };

  lv_design_res_t PlayfieldDesignCb(lv_obj_t* obj, const lv_area_t* clipArea, lv_design_mode_t mode) {
    if (mode == LV_DESIGN_COVER_CHK) {
      return LV_DESIGN_RES_COVER;
    }
    if (mode == LV_DESIGN_DRAW_MAIN) {
      auto* game = static_cast<Breakout*>(obj->user_data);
      if (game != nullptr) {
        game->DrawPlayfield(obj, clipArea);
      }
    }
    return LV_DESIGN_RES_OK;
  }

  struct SavedBreakoutState {
    bool valid = false;
    std::array<uint8_t, (Breakout::GridCols * Breakout::GridRows + 3) / 4> bricks {};
    uint8_t state = 0;
    uint8_t level = 0;
    uint8_t lives = 3;
    uint8_t bricksRemaining = 0;
    uint32_t score = 0;
    int32_t paddleXQ4 = 0;
    int32_t ballXQ4 = 0;
    int32_t ballYQ4 = 0;
    int32_t ballVxQ4 = 16;
    int32_t ballVyQ4 = -24;
  };
  SavedBreakoutState savedState {};
}

Breakout::Breakout(Controllers::MotionController& motionController,
                   Controllers::MotorController& motorController,
                   System::SystemTask& systemTask)
  : motionController {motionController}, motorController {motorController}, wakeLock(systemTask) {

  wakeLock.Lock();

  rngState = static_cast<uint32_t>(xTaskGetTickCount()) ^ (static_cast<uint32_t>(motionController.X()) << 16) ^
             static_cast<uint32_t>(motionController.Y()) ^ 0x9E3779B9U;
  if (rngState == 0) {
    rngState = 1;
  }

  playfieldObj = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(playfieldObj, ScreenW, ScreenH);
  lv_obj_set_pos(playfieldObj, 0, 0);
  lv_obj_set_click(playfieldObj, false);
  playfieldObj->user_data = this;
  lv_obj_set_design_cb(playfieldObj, PlayfieldDesignCb);

  hudLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_click(hudLabel, false);
  lv_obj_set_style_local_text_color(hudLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_pos(hudLabel, 4, 1);

  statusBox = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(statusBox, false);
  lv_obj_set_size(statusBox, 184, 54);
  lv_obj_set_pos(statusBox, (ScreenW - 184) / 2, 146);
  lv_obj_set_style_local_bg_color(statusBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_bg_opa(statusBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_border_color(statusBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_border_width(statusBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
  lv_obj_set_style_local_radius(statusBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 4);

  statusLabel = lv_label_create(statusBox, nullptr);
  lv_label_set_align(statusLabel, LV_LABEL_ALIGN_CENTER);
  lv_obj_set_style_local_text_color(statusLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_auto_realign(statusLabel, true);
  lv_obj_align(statusLabel, statusBox, LV_ALIGN_CENTER, 0, 0);

  if (savedState.valid) {
    bricks = savedState.bricks;
    state = static_cast<GameState>(savedState.state);
    level = savedState.level;
    lives = savedState.lives;
    bricksRemaining = savedState.bricksRemaining;
    score = savedState.score;
    paddleXQ4 = savedState.paddleXQ4;
    ballXQ4 = savedState.ballXQ4;
    ballYQ4 = savedState.ballYQ4;
    ballVxQ4 = savedState.ballVxQ4;
    ballVyQ4 = savedState.ballVyQ4;
    prevPaddleX = static_cast<int16_t>(paddleXQ4 >> 4);
    prevBallX = static_cast<int16_t>(ballXQ4 >> 4);
    prevBallY = static_cast<int16_t>(ballYQ4 >> 4);
    hudDirty = true;
    lv_obj_invalidate(playfieldObj);
  } else {
    paddleXQ4 = ((ScreenW - PaddleW) / 2) * 16;
    prevPaddleX = static_cast<int16_t>(paddleXQ4 >> 4);
    LoadLevel(0);
  }
  UpdateHud();
  UpdateBanner();

  taskRefresh = lv_task_create(RefreshTaskCallback, 30, LV_TASK_PRIO_MID, this);
}

Breakout::~Breakout() {
  if (state == GameState::GameOver || state == GameState::Victory) {
    savedState.valid = false;
  } else {
    savedState.valid = true;
    savedState.bricks = bricks;
    savedState.state = static_cast<uint8_t>((state == GameState::Playing) ? GameState::Paused : state);
    savedState.level = level;
    savedState.lives = lives;
    savedState.bricksRemaining = bricksRemaining;
    savedState.score = score;
    savedState.paddleXQ4 = paddleXQ4;
    savedState.ballXQ4 = ballXQ4;
    savedState.ballYQ4 = ballYQ4;
    savedState.ballVxQ4 = ballVxQ4;
    savedState.ballVyQ4 = ballVyQ4;
  }
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

uint8_t Breakout::GetBrick(uint8_t col, uint8_t row) const {
  const uint16_t idx = static_cast<uint16_t>(row) * GridCols + col;
  return static_cast<uint8_t>((bricks[idx >> 2] >> ((idx & 3U) * 2U)) & 0x03U);
}

void Breakout::SetBrick(uint8_t col, uint8_t row, uint8_t val) {
  const uint16_t idx = static_cast<uint16_t>(row) * GridCols + col;
  const uint8_t byteIdx = static_cast<uint8_t>(idx >> 2);
  const uint8_t shift = static_cast<uint8_t>((idx & 3U) * 2U);
  bricks[byteIdx] = static_cast<uint8_t>((bricks[byteIdx] & ~(0x03U << shift)) | ((val & 0x03U) << shift));
}

uint32_t Breakout::NextRand() {
  rngState = rngState * 1664525U + 1013904223U;
  return rngState;
}

void Breakout::InvalidateRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  lv_area_t area {
    .x1 = static_cast<lv_coord_t>(std::clamp<int32_t>(x1, 0, ScreenW - 1)),
    .y1 = static_cast<lv_coord_t>(std::clamp<int32_t>(y1, 0, ScreenH - 1)),
    .x2 = static_cast<lv_coord_t>(std::clamp<int32_t>(x2, 0, ScreenW - 1)),
    .y2 = static_cast<lv_coord_t>(std::clamp<int32_t>(y2, 0, ScreenH - 1)),
  };
  if (area.x1 <= area.x2 && area.y1 <= area.y2) {
    lv_obj_invalidate_area(playfieldObj, &area);
  }
}

void Breakout::LoadLevel(uint8_t idx) {
  level = idx % NumLevels;
  bricks.fill(0);
  bricksRemaining = 0;

  const LevelDef& def = Levels[level];
  for (uint8_t row = 0; row < GridRows; ++row) {
    const uint16_t mask = def.rows[row];
    for (uint8_t col = 0; col < GridCols; ++col) {
      if (((mask >> (11 - col)) & 1U) != 0) {
        const uint8_t hp = static_cast<uint8_t>(1U + ((NextRand() >> 16) % 3U));
        SetBrick(col, row, hp);
        bricksRemaining++;
      }
    }
  }

  // Fallback in case a mask ever had zero bits
  if (bricksRemaining == 0) {
    SetBrick(5, 3, 2);
    bricksRemaining = 1;
  }

  state = GameState::Ready;
  ResetBallOnPaddle();
  hudDirty = true;
  lv_obj_invalidate(playfieldObj);
}

void Breakout::ResetBallOnPaddle() {
  InvalidateRect(prevBallX, prevBallY, prevBallX + BallSize - 1, prevBallY + BallSize - 1);

  ballXQ4 = paddleXQ4 + ((PaddleW - BallSize) * 8);
  ballYQ4 = (PaddleY - BallSize) * 16;

  const int32_t speedBoost = static_cast<int32_t>(level);
  ballVxQ4 = ((NextRand() & 1U) != 0) ? (14 + speedBoost) : -(14 + speedBoost);
  ballVyQ4 = -(24 + speedBoost);

  prevBallX = static_cast<int16_t>(ballXQ4 >> 4);
  prevBallY = static_cast<int16_t>(ballYQ4 >> 4);
  InvalidateRect(prevBallX, prevBallY, prevBallX + BallSize - 1, prevBallY + BallSize - 1);
}

void Breakout::UpdateHud() {
  static constexpr const char* livesStr[] = {"", "*", "**", "***"};
  const uint8_t clampedLives = std::min<uint8_t>(lives, 3);
  lv_label_set_text_fmt(hudLabel,
                        "L%u/8 %05lu  %s",
                        static_cast<unsigned>(level + 1),
                        static_cast<unsigned long>(score),
                        livesStr[clampedLives]);
  hudDirty = false;
}

void Breakout::UpdateBanner() {
  if (state == GameState::Playing) {
    lv_obj_set_hidden(statusBox, true);
    return;
  }

  switch (state) {
    case GameState::Ready:
      lv_label_set_text_fmt(statusLabel, "%s\nTap to launch", Levels[level].name);
      break;
    case GameState::Paused:
      lv_label_set_text_static(statusLabel, "PAUSED\nTap to resume");
      break;
    case GameState::LevelClear:
      lv_label_set_text_fmt(statusLabel, "CLEAR!\nTap for Lv %u", static_cast<unsigned>(level + 2));
      break;
    case GameState::Victory:
      lv_label_set_text_static(statusLabel, "YOU WIN!\nTap to replay");
      break;
    case GameState::GameOver:
    default:
      lv_label_set_text_static(statusLabel, "GAME OVER\nTap to retry");
      break;
  }

  lv_obj_set_hidden(statusBox, false);
}

void Breakout::UpdatePaddleFromAccel() {
  const int32_t tiltX = static_cast<int32_t>(motionController.X());
  const int32_t absTilt = std::abs(tiltX);
  if (absTilt <= DeadZoneMg) {
    return;
  }

  const int32_t excess = std::min<int32_t>(absTilt - DeadZoneMg, MaxTiltExcessMg);
  const int32_t speedQ4 =
    (excess * 96) / MaxTiltExcessMg + (excess * excess * 176) / (MaxTiltExcessMg * MaxTiltExcessMg);
  if (speedQ4 == 0) {
    return;
  }

  paddleXQ4 += (tiltX > 0) ? speedQ4 : -speedQ4;
  paddleXQ4 = std::clamp<int32_t>(paddleXQ4, PaddleMinX * 16, PaddleMaxX * 16);
}

void Breakout::StepBall() {
  for (uint8_t step = 0; step < SubStepsPerTick; ++step) {
    ballXQ4 += ballVxQ4;
    ballYQ4 += ballVyQ4;

    // Left / right walls in Q4 fixed-point (keep 1px border clear at x=0 and x=239)
    if (ballXQ4 < LeftWallQ4) {
      ballXQ4 = 2 * LeftWallQ4 - ballXQ4;
      ballVxQ4 = std::abs(ballVxQ4);
    } else if (ballXQ4 > RightWallQ4) {
      ballXQ4 = 2 * RightWallQ4 - ballXQ4;
      ballVxQ4 = -std::abs(ballVxQ4);
    }

    // Top ceiling border in Q4 fixed-point
    if (ballYQ4 < TopWallQ4) {
      ballYQ4 = 2 * TopWallQ4 - ballYQ4;
      ballVyQ4 = std::abs(ballVyQ4);
    }

    int32_t bx1 = ballXQ4 >> 4;
    int32_t by1 = ballYQ4 >> 4;

    // Missed paddle at bottom
    if (by1 >= ScreenH) {
      if (lives > 1) {
        lives--;
        state = GameState::Ready;
        ResetBallOnPaddle();
      } else {
        lives = 0;
        state = GameState::GameOver;
      }
      motorController.RunForDuration(50);
      hudDirty = true;
      UpdateBanner();
      return;
    }

    // Paddle collision
    const int32_t px1 = paddleXQ4 >> 4;
    const int32_t px2 = px1 + PaddleW - 1;
    const int32_t bx2 = bx1 + BallSize - 1;
    const int32_t by2 = by1 + BallSize - 1;

    if (ballVyQ4 > 0 && by2 >= PaddleY && by1 <= PaddleY + PaddleH - 1 && bx2 >= px1 && bx1 <= px2) {
      by1 = PaddleY - BallSize;
      ballYQ4 = by1 * 16;

      // Smooth continuous reflection angle from hit offset (never zero horizontal velocity)
      const int32_t paddleCenterQ4 = paddleXQ4 + (PaddleW * 8);
      const int32_t ballCenterQ4 = ballXQ4 + (BallSize * 8);
      const int32_t halfPaddleQ4 = PaddleW * 8;
      const int32_t diffQ4 = std::clamp<int32_t>(ballCenterQ4 - paddleCenterQ4, -halfPaddleQ4, halfPaddleQ4);
      const int32_t speedBoost = static_cast<int32_t>(level);
      const int32_t minVx = 12 + speedBoost;
      const int32_t maxVx = 38 + speedBoost;
      const int32_t rawVx = (diffQ4 * maxVx) / halfPaddleQ4;
      if (diffQ4 == 0) {
        ballVxQ4 = (ballVxQ4 >= 0) ? minVx : -minVx;
      } else {
        const int32_t sign = (diffQ4 > 0) ? 1 : -1;
        ballVxQ4 = sign * std::max<int32_t>(std::abs(rawVx), minVx);
      }
      ballVyQ4 = -(26 + speedBoost - std::abs(ballVxQ4) / 6);
      motorController.RunForDuration(10);
      continue;
    }

    // Brick collision
    if (by2 >= BrickOriginY && by1 < BrickOriginY + GridRows * BrickPitchY && bx2 >= BrickOriginX &&
        bx1 < BrickOriginX + GridCols * BrickPitchX) {
      const int32_t rStart = std::max<int32_t>(0, (by1 - BrickOriginY) / BrickPitchY);
      const int32_t rEnd = std::min<int32_t>(GridRows - 1, (by2 - BrickOriginY) / BrickPitchY);
      const int32_t cStart = std::max<int32_t>(0, (bx1 - BrickOriginX) / BrickPitchX);
      const int32_t cEnd = std::min<int32_t>(GridCols - 1, (bx2 - BrickOriginX) / BrickPitchX);

      bool hitBrick = false;
      for (int32_t row = rStart; row <= rEnd && !hitBrick; ++row) {
        const int32_t ry1 = BrickOriginY + row * BrickPitchY;
        const int32_t ry2 = ry1 + BrickH - 1;
        if (by2 < ry1 || by1 > ry2) {
          continue;
        }
        for (int32_t col = cStart; col <= cEnd; ++col) {
          const uint8_t hp = GetBrick(static_cast<uint8_t>(col), static_cast<uint8_t>(row));
          if (hp == 0) {
            continue;
          }
          const int32_t rx1 = BrickOriginX + col * BrickPitchX;
          const int32_t rx2 = rx1 + BrickW - 1;
          if (bx2 < rx1 || bx1 > rx2) {
            continue;
          }

          const int32_t overlapLeft = (bx2 + 1) - rx1;
          const int32_t overlapRight = (rx2 + 1) - bx1;
          const int32_t overlapTop = (by2 + 1) - ry1;
          const int32_t overlapBottom = (ry2 + 1) - by1;
          const int32_t minX = std::min(overlapLeft, overlapRight);
          const int32_t minY = std::min(overlapTop, overlapBottom);

          if (minX < minY) {
            ballVxQ4 = -ballVxQ4;
            if (overlapLeft < overlapRight) {
              ballXQ4 = (rx1 - BallSize) * 16;
            } else {
              ballXQ4 = (rx2 + 1) * 16;
            }
          } else {
            ballVyQ4 = -ballVyQ4;
            if (overlapTop < overlapBottom) {
              ballYQ4 = (ry1 - BallSize) * 16;
            } else {
              ballYQ4 = (ry2 + 1) * 16;
            }
          }
          ballXQ4 = std::clamp<int32_t>(ballXQ4, LeftWallQ4, RightWallQ4);
          ballYQ4 = std::max<int32_t>(ballYQ4, TopWallQ4);

          const uint8_t newHp = static_cast<uint8_t>(hp - 1U);
          SetBrick(static_cast<uint8_t>(col), static_cast<uint8_t>(row), newHp);
          InvalidateRect(rx1, ry1, rx2, ry2);

          score += (newHp == 0) ? 30U : 10U;
          hudDirty = true;

          if (newHp == 0 && bricksRemaining > 0) {
            bricksRemaining--;
            if (bricksRemaining == 0) {
              state = (level + 1 >= NumLevels) ? GameState::Victory : GameState::LevelClear;
              motorController.RunForDuration(40);
              UpdateBanner();
              return;
            }
          }

          hitBrick = true;
          break;
        }
      }
    }
  }
}

void Breakout::DrawPlayfield(lv_obj_t* obj, const lv_area_t* clipArea) {
  lv_disp_t* disp = _lv_refr_get_disp_refreshing();
  lv_disp_buf_t* vdb = lv_disp_get_buf(disp);
  const lv_area_t* dispArea = &vdb->area;
  auto* dispBuf = static_cast<lv_color_t*>(vdb->buf_act);

  lv_area_t drawArea;
  if (!_lv_area_intersect(&drawArea, clipArea, &obj->coords)) {
    return;
  }

  const int32_t dispW = lv_area_get_width(dispArea);
  const int32_t drawW = drawArea.x2 - drawArea.x1 + 1;

  // 1. Clear invalidated area to pure black
  if (drawW == dispW) {
    lv_color_t* start = dispBuf + (drawArea.y1 - dispArea->y1) * dispW;
    const int32_t drawH = drawArea.y2 - drawArea.y1 + 1;
    std::memset(start, 0, static_cast<std::size_t>(drawW * drawH) * sizeof(lv_color_t));
  } else {
    for (int32_t y = drawArea.y1; y <= drawArea.y2; ++y) {
      lv_color_t* row = dispBuf + (y - dispArea->y1) * dispW + (drawArea.x1 - dispArea->x1);
      std::memset(row, 0, static_cast<std::size_t>(drawW) * sizeof(lv_color_t));
    }
  }

  auto setPx = [dispBuf, dispArea, dispW](int32_t x, int32_t y) {
    dispBuf[(y - dispArea->y1) * dispW + (x - dispArea->x1)] = LV_COLOR_WHITE;
  };

  // 2. Playfield top/side 1px white borders
  if (drawArea.y1 <= BorderTopY && drawArea.y2 >= BorderTopY) {
    lv_color_t* topRow = dispBuf + (BorderTopY - dispArea->y1) * dispW + (drawArea.x1 - dispArea->x1);
    for (int32_t x = drawArea.x1; x <= drawArea.x2; ++x) {
      *topRow++ = LV_COLOR_WHITE;
    }
  }
  const int32_t wallY1 = std::max<int32_t>(drawArea.y1, BorderTopY);
  if (wallY1 <= drawArea.y2) {
    if (drawArea.x1 <= 0 && drawArea.x2 >= 0) {
      for (int32_t y = wallY1; y <= drawArea.y2; ++y) {
        setPx(0, y);
      }
    }
    if (drawArea.x1 <= ScreenW - 1 && drawArea.x2 >= ScreenW - 1) {
      for (int32_t y = wallY1; y <= drawArea.y2; ++y) {
        setPx(ScreenW - 1, y);
      }
    }
  }

  // 3. Intersecting bricks with 3 visual damage states
  if (drawArea.y2 >= BrickOriginY && drawArea.y1 < BrickOriginY + GridRows * BrickPitchY && drawArea.x2 >= BrickOriginX &&
      drawArea.x1 < BrickOriginX + GridCols * BrickPitchX) {
    const int32_t rStart = std::max<int32_t>(0, (drawArea.y1 - BrickOriginY) / BrickPitchY);
    const int32_t rEnd = std::min<int32_t>(GridRows - 1, (drawArea.y2 - BrickOriginY) / BrickPitchY);
    const int32_t cStart = std::max<int32_t>(0, (drawArea.x1 - BrickOriginX) / BrickPitchX);
    const int32_t cEnd = std::min<int32_t>(GridCols - 1, (drawArea.x2 - BrickOriginX) / BrickPitchX);

    for (int32_t row = rStart; row <= rEnd; ++row) {
      const int32_t by1 = BrickOriginY + row * BrickPitchY;
      const int32_t by2 = by1 + BrickH - 1;
      const int32_t iy1 = std::max<int32_t>(by1, drawArea.y1);
      const int32_t iy2 = std::min<int32_t>(by2, drawArea.y2);
      if (iy1 > iy2) {
        continue;
      }

      for (int32_t col = cStart; col <= cEnd; ++col) {
        const uint8_t hp = GetBrick(static_cast<uint8_t>(col), static_cast<uint8_t>(row));
        if (hp == 0) {
          continue;
        }
        const int32_t bx1 = BrickOriginX + col * BrickPitchX;
        const int32_t bx2 = bx1 + BrickW - 1;
        const int32_t ix1 = std::max<int32_t>(bx1, drawArea.x1);
        const int32_t ix2 = std::min<int32_t>(bx2, drawArea.x2);
        if (ix1 > ix2) {
          continue;
        }

        if (hp >= 3) {
          for (int32_t y = iy1; y <= iy2; ++y) {
            lv_color_t* rowPtr = dispBuf + (y - dispArea->y1) * dispW + (ix1 - dispArea->x1);
            for (int32_t x = ix1; x <= ix2; ++x) {
              *rowPtr++ = LV_COLOR_WHITE;
            }
          }
        } else {
          const uint32_t* maskRows = (hp == 2) ? BrickMaskSomeCracks : BrickMaskManyCracks;
          for (int32_t y = iy1; y <= iy2; ++y) {
            const uint32_t mask = maskRows[y - by1];
            lv_color_t* rowPtr = dispBuf + (y - dispArea->y1) * dispW + (ix1 - dispArea->x1);
            for (int32_t x = ix1; x <= ix2; ++x) {
              const uint32_t bit = 1U << (BrickW - 1 - (x - bx1));
              if ((mask & bit) != 0) {
                *rowPtr = LV_COLOR_WHITE;
              }
              ++rowPtr;
            }
          }
        }
      }
    }
  }

  // 4. Paddle
  const int32_t px1 = paddleXQ4 >> 4;
  const int32_t px2 = px1 + PaddleW - 1;
  const int32_t py1 = PaddleY;
  const int32_t py2 = PaddleY + PaddleH - 1;
  const int32_t pix1 = std::max<int32_t>(px1, drawArea.x1);
  const int32_t pix2 = std::min<int32_t>(px2, drawArea.x2);
  const int32_t piy1 = std::max<int32_t>(py1, drawArea.y1);
  const int32_t piy2 = std::min<int32_t>(py2, drawArea.y2);
  for (int32_t y = piy1; y <= piy2; ++y) {
    const bool trimCorners = (y == py1 || y == py2);
    for (int32_t x = pix1; x <= pix2; ++x) {
      if (trimCorners && (x == px1 || x == px2)) {
        continue;
      }
      setPx(x, y);
    }
  }

  // 5. Ball (shown in Ready, Playing, and Paused states)
  if (state == GameState::Ready || state == GameState::Playing || state == GameState::Paused) {
    const int32_t bx1 = ballXQ4 >> 4;
    const int32_t bx2 = bx1 + BallSize - 1;
    const int32_t by1 = ballYQ4 >> 4;
    const int32_t by2 = by1 + BallSize - 1;
    const int32_t bix1 = std::max<int32_t>(bx1, drawArea.x1);
    const int32_t bix2 = std::min<int32_t>(bx2, drawArea.x2);
    const int32_t biy1 = std::max<int32_t>(by1, drawArea.y1);
    const int32_t biy2 = std::min<int32_t>(by2, drawArea.y2);
    for (int32_t y = biy1; y <= biy2; ++y) {
      const bool trimCorners = (y == by1 || y == by2);
      for (int32_t x = bix1; x <= bix2; ++x) {
        if (trimCorners && (x == bx1 || x == bx2)) {
          continue;
        }
        setPx(x, y);
      }
    }
  }
}

void Breakout::Refresh() {
  if (state == GameState::Ready || state == GameState::Playing) {
    UpdatePaddleFromAccel();
    const int16_t curPaddleX = static_cast<int16_t>(paddleXQ4 >> 4);
    if (curPaddleX != prevPaddleX) {
      InvalidateRect(prevPaddleX, PaddleY, prevPaddleX + PaddleW - 1, PaddleY + PaddleH - 1);
      InvalidateRect(curPaddleX, PaddleY, curPaddleX + PaddleW - 1, PaddleY + PaddleH - 1);
      prevPaddleX = curPaddleX;
    }

    if (state == GameState::Ready) {
      ballXQ4 = paddleXQ4 + ((PaddleW - BallSize) * 8);
      ballYQ4 = (PaddleY - BallSize) * 16;
    } else {
      StepBall();
    }

    const int16_t curBallX = static_cast<int16_t>(ballXQ4 >> 4);
    const int16_t curBallY = static_cast<int16_t>(ballYQ4 >> 4);
    if (curBallX != prevBallX || curBallY != prevBallY) {
      InvalidateRect(prevBallX, prevBallY, prevBallX + BallSize - 1, prevBallY + BallSize - 1);
      InvalidateRect(curBallX, curBallY, curBallX + BallSize - 1, curBallY + BallSize - 1);
      prevBallX = curBallX;
      prevBallY = curBallY;
    }
  }

  if (hudDirty) {
    UpdateHud();
  }
}

bool Breakout::OnTouchEvent(TouchEvents event) {
  if (event == TouchEvents::SwipeDown) {
    return false;
  }

  if (event == TouchEvents::LongTap) {
    score = 0;
    lives = 3;
    LoadLevel(0);
    UpdateHud();
    UpdateBanner();
    return true;
  }

  if (event == TouchEvents::Tap) {
    switch (state) {
      case GameState::Ready:
      case GameState::Paused:
        state = GameState::Playing;
        UpdateBanner();
        break;
      case GameState::Playing:
        state = GameState::Paused;
        UpdateBanner();
        break;
      case GameState::LevelClear:
        LoadLevel(static_cast<uint8_t>(level + 1U));
        UpdateHud();
        UpdateBanner();
        break;
      case GameState::GameOver:
      case GameState::Victory:
        score = 0;
        lives = 3;
        LoadLevel(0);
        UpdateHud();
        UpdateBanner();
        break;
    }
    return true;
  }

  if (state == GameState::Ready) {
    if (event == TouchEvents::SwipeLeft) {
      LoadLevel(static_cast<uint8_t>((level + 1U) % NumLevels));
      UpdateHud();
      UpdateBanner();
      return true;
    }
    if (event == TouchEvents::SwipeRight) {
      LoadLevel(static_cast<uint8_t>((level + NumLevels - 1U) % NumLevels));
      UpdateHud();
      UpdateBanner();
      return true;
    }
  }

  return true;
}
