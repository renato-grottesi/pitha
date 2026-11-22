#include "displayapp/screens/Paddle.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <FreeRTOS.h>
#include <task.h>

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr int16_t ScreenW = 240;
  constexpr int16_t ScreenH = 240;

  constexpr int16_t PaddleW = 6;
  constexpr int16_t PaddleH = 48;
  constexpr int16_t PlayerPaddleX = 6;
  constexpr int16_t AiPaddleX = ScreenW - 6 - PaddleW;
  constexpr int16_t PaddleMinY = 2;
  constexpr int16_t PaddleMaxY = ScreenH - 2 - PaddleH;

  constexpr int16_t BallSize = 8;
  constexpr int32_t TopWallQ4 = 1 * 16;
  constexpr int32_t BottomWallQ4 = (ScreenH - 1 - BallSize) * 16;

  constexpr uint8_t SubStepsPerTick = 2;
  constexpr uint8_t ServeDelayTicks = 18;

  constexpr int32_t DeadZoneMg = 50;
  constexpr int32_t MaxTiltExcessMg = 400;
  constexpr int32_t MinNeutralY = -512;
  constexpr int32_t MaxNeutralY = 256;

  constexpr int16_t AiReactX = 64;
  constexpr int32_t AiMaxSpeedQ4 = 60;
  constexpr int32_t AiReturnSpeedQ4 = 32;
  constexpr int32_t AiDeadBandQ4 = 48;

  lv_design_res_t PlayfieldDesignCb(lv_obj_t* obj, const lv_area_t* clipArea, lv_design_mode_t mode) {
    if (mode == LV_DESIGN_COVER_CHK) {
      return LV_DESIGN_RES_COVER;
    }
    if (mode == LV_DESIGN_DRAW_MAIN) {
      auto* game = static_cast<Paddle*>(obj->user_data);
      if (game != nullptr) {
        game->DrawPlayfield(obj, clipArea);
      }
    }
    return LV_DESIGN_RES_OK;
  }

  struct SavedPaddleState {
    bool valid = false;
    uint8_t playerScore = 0;
    uint8_t aiScore = 0;
    uint8_t rallyHits = 0;
    uint8_t serveDelayTicks = 0;
    int32_t playerYQ4 = 0;
    int32_t aiYQ4 = 0;
    int32_t aiAimOffsetQ4 = 0;
    int32_t ballXQ4 = 0;
    int32_t ballYQ4 = 0;
    int32_t ballVxQ4 = 26;
    int32_t ballVyQ4 = 10;
  };
  SavedPaddleState savedState {};
}

Paddle::Paddle(Controllers::MotionController& motionController,
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

  auto makeScoreLabel = [](lv_coord_t offsetX) {
    lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_click(label, false);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_42);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    lv_label_set_text_static(label, "0");
    lv_obj_set_auto_realign(label, true);
    lv_obj_align(label, lv_scr_act(), LV_ALIGN_IN_TOP_MID, offsetX, 8);
    return label;
  };
  playerScoreLabel = makeScoreLabel(-56);
  aiScoreLabel = makeScoreLabel(56);

  pauseBox = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(pauseBox, false);
  lv_obj_set_size(pauseBox, 196, 84);
  lv_obj_align(pauseBox, lv_scr_act(), LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_local_bg_color(pauseBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_bg_opa(pauseBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_border_color(pauseBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_obj_set_style_local_border_width(pauseBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
  lv_obj_set_style_local_radius(pauseBox, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 4);

  pauseLabel = lv_label_create(pauseBox, nullptr);
  lv_label_set_align(pauseLabel, LV_LABEL_ALIGN_CENTER);
  lv_obj_set_style_local_text_color(pauseLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_WHITE);
  lv_label_set_text_static(pauseLabel, "PAUSED\nTap to resume\nHold to reset");
  lv_obj_set_auto_realign(pauseLabel, true);
  lv_obj_align(pauseLabel, pauseBox, LV_ALIGN_CENTER, 0, 0);

  if (savedState.valid) {
    CalibrateNeutralY();
    playerScore = savedState.playerScore;
    aiScore = savedState.aiScore;
    rallyHits = savedState.rallyHits;
    serveDelayTicks = savedState.serveDelayTicks;
    playerYQ4 = savedState.playerYQ4;
    aiYQ4 = savedState.aiYQ4;
    aiAimOffsetQ4 = savedState.aiAimOffsetQ4;
    ballXQ4 = savedState.ballXQ4;
    ballYQ4 = savedState.ballYQ4;
    ballVxQ4 = savedState.ballVxQ4;
    ballVyQ4 = savedState.ballVyQ4;
    prevPlayerY = static_cast<int16_t>(playerYQ4 >> 4);
    prevAiY = static_cast<int16_t>(aiYQ4 >> 4);
    prevBallX = static_cast<int16_t>(ballXQ4 >> 4);
    prevBallY = static_cast<int16_t>(ballYQ4 >> 4);
    paused = true;
    scoreDirty = true;
    UpdateScores();
    UpdatePauseOverlay();
    lv_obj_invalidate(playfieldObj);
  } else {
    ResetMatch();
  }

  taskRefresh = lv_task_create(RefreshTaskCallback, 30, LV_TASK_PRIO_MID, this);
}

Paddle::~Paddle() {
  savedState.valid = true;
  savedState.playerScore = playerScore;
  savedState.aiScore = aiScore;
  savedState.rallyHits = rallyHits;
  savedState.serveDelayTicks = serveDelayTicks;
  savedState.playerYQ4 = playerYQ4;
  savedState.aiYQ4 = aiYQ4;
  savedState.aiAimOffsetQ4 = aiAimOffsetQ4;
  savedState.ballXQ4 = ballXQ4;
  savedState.ballYQ4 = ballYQ4;
  savedState.ballVxQ4 = ballVxQ4;
  savedState.ballVyQ4 = ballVyQ4;
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

uint32_t Paddle::NextRand() {
  rngState = rngState * 1664525U + 1013904223U;
  return rngState;
}

void Paddle::CalibrateNeutralY() {
  neutralY = std::clamp<int32_t>(static_cast<int32_t>(motionController.Y()), MinNeutralY, MaxNeutralY);
}

void Paddle::RollAiAimOffset() {
  const int32_t offsetPx = static_cast<int32_t>((NextRand() >> 16) % 33U) - 16;
  aiAimOffsetQ4 = offsetPx * 16;
}

void Paddle::InvalidateRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  if (x2 < 0 || x1 >= ScreenW || y2 < 0 || y1 >= ScreenH) {
    return;
  }
  lv_area_t area {
    .x1 = static_cast<lv_coord_t>(std::max<int32_t>(x1, 0)),
    .y1 = static_cast<lv_coord_t>(std::max<int32_t>(y1, 0)),
    .x2 = static_cast<lv_coord_t>(std::min<int32_t>(x2, ScreenW - 1)),
    .y2 = static_cast<lv_coord_t>(std::min<int32_t>(y2, ScreenH - 1)),
  };
  if (area.x1 <= area.x2 && area.y1 <= area.y2) {
    lv_obj_invalidate_area(playfieldObj, &area);
  }
}

void Paddle::ResetServe(bool towardAi) {
  InvalidateRect(prevBallX, prevBallY, prevBallX + BallSize - 1, prevBallY + BallSize - 1);

  ballXQ4 = ((ScreenW - BallSize) / 2) * 16;
  ballYQ4 = ((ScreenH - BallSize) / 2) * 16;

  static constexpr int8_t serveVyOptions[4] = {-14, -8, 8, 14};
  ballVxQ4 = towardAi ? 26 : -26;
  ballVyQ4 = serveVyOptions[(NextRand() >> 16) & 3U];
  rallyHits = 0;
  serveDelayTicks = ServeDelayTicks;
  RollAiAimOffset();

  prevBallX = static_cast<int16_t>(ballXQ4 >> 4);
  prevBallY = static_cast<int16_t>(ballYQ4 >> 4);
  InvalidateRect(prevBallX, prevBallY, prevBallX + BallSize - 1, prevBallY + BallSize - 1);
}

void Paddle::ResetMatch() {
  playerScore = 0;
  aiScore = 0;
  paused = false;
  scoreDirty = true;

  CalibrateNeutralY();
  playerYQ4 = ((ScreenH - PaddleH) / 2) * 16;
  aiYQ4 = ((ScreenH - PaddleH) / 2) * 16;
  prevPlayerY = static_cast<int16_t>(playerYQ4 >> 4);
  prevAiY = static_cast<int16_t>(aiYQ4 >> 4);

  ResetServe((NextRand() & 1U) != 0);
  UpdateScores();
  UpdatePauseOverlay();
  lv_obj_invalidate(playfieldObj);
}

void Paddle::UpdateScores() {
  lv_label_set_text_fmt(playerScoreLabel, "%u", static_cast<unsigned>(playerScore));
  lv_label_set_text_fmt(aiScoreLabel, "%u", static_cast<unsigned>(aiScore));
  scoreDirty = false;
}

void Paddle::UpdatePauseOverlay() {
  lv_obj_set_hidden(pauseBox, !paused);
}

void Paddle::UpdatePlayerFromAccel() {
  const int32_t deltaY = static_cast<int32_t>(motionController.Y()) - neutralY;
  const int32_t absDelta = std::abs(deltaY);
  if (absDelta <= DeadZoneMg) {
    return;
  }

  const int32_t excess = std::min<int32_t>(absDelta - DeadZoneMg, MaxTiltExcessMg);
  const int32_t speedQ4 =
    (excess * 96) / MaxTiltExcessMg + (excess * excess * 176) / (MaxTiltExcessMg * MaxTiltExcessMg);
  if (speedQ4 == 0) {
    return;
  }

  // Positive Y points toward top-downhill (screen UP), negative Y points toward bottom-downhill (screen DOWN)
  playerYQ4 += (deltaY > 0) ? -speedQ4 : speedQ4;
  playerYQ4 = std::clamp<int32_t>(playerYQ4, PaddleMinY * 16, PaddleMaxY * 16);
}

void Paddle::UpdateAiPaddle() {
  const int32_t aiCenterQ4 = aiYQ4 + (PaddleH * 8);
  const bool trackBall = (serveDelayTicks == 0 && ballVxQ4 > 0 && (ballXQ4 >> 4) >= AiReactX);
  const int32_t targetCenterQ4 = trackBall ? (ballYQ4 + (BallSize * 8) + aiAimOffsetQ4) : ((ScreenH / 2) * 16);
  const int32_t maxStep = trackBall ? AiMaxSpeedQ4 : AiReturnSpeedQ4;

  const int32_t errQ4 = targetCenterQ4 - aiCenterQ4;
  if (std::abs(errQ4) <= AiDeadBandQ4) {
    return;
  }

  const int32_t step = std::clamp<int32_t>(errQ4, -maxStep, maxStep);
  aiYQ4 = std::clamp<int32_t>(aiYQ4 + step, PaddleMinY * 16, PaddleMaxY * 16);
}

void Paddle::StepBall() {
  auto bounceOffPaddle = [this](int32_t paddleTopYQ4, int32_t newBallXQ4, int8_t vxDir, uint8_t hapticMs) {
    ballXQ4 = newBallXQ4;
    if (rallyHits < 12) {
      rallyHits++;
    }
    ballVxQ4 = vxDir * (28 + static_cast<int32_t>(rallyHits) * 2);

    const int32_t paddleCenterQ4 = paddleTopYQ4 + (PaddleH * 8);
    const int32_t ballCenterQ4 = ballYQ4 + (BallSize * 8);
    const int32_t halfPaddleQ4 = PaddleH * 8;
    const int32_t diffQ4 = std::clamp<int32_t>(ballCenterQ4 - paddleCenterQ4, -halfPaddleQ4, halfPaddleQ4);
    const int32_t maxVy = 34 + static_cast<int32_t>(rallyHits);
    ballVyQ4 = (diffQ4 * maxVy) / halfPaddleQ4;
    if (ballVyQ4 == 0) {
      ballVyQ4 = ((NextRand() & 1U) != 0) ? 2 : -2;
    }
    motorController.RunForDuration(hapticMs);
  };

  for (uint8_t step = 0; step < SubStepsPerTick; ++step) {
    ballXQ4 += ballVxQ4;
    ballYQ4 += ballVyQ4;

    // Top / bottom walls in Q4 fixed-point (keep 1px borders clear at y=0 and y=239)
    if (ballYQ4 < TopWallQ4) {
      ballYQ4 = 2 * TopWallQ4 - ballYQ4;
      ballVyQ4 = std::abs(ballVyQ4);
    } else if (ballYQ4 > BottomWallQ4) {
      ballYQ4 = 2 * BottomWallQ4 - ballYQ4;
      ballVyQ4 = -std::abs(ballVyQ4);
    }

    const int32_t bx1 = ballXQ4 >> 4;
    const int32_t by1 = ballYQ4 >> 4;
    const int32_t bx2 = bx1 + BallSize - 1;
    const int32_t by2 = by1 + BallSize - 1;

    // Player paddle (left)
    const int32_t py1 = playerYQ4 >> 4;
    const int32_t py2 = py1 + PaddleH - 1;
    if (ballVxQ4 < 0 && bx1 <= PlayerPaddleX + PaddleW - 1 && bx2 >= PlayerPaddleX && by2 >= py1 && by1 <= py2) {
      bounceOffPaddle(playerYQ4, (PlayerPaddleX + PaddleW) * 16, 1, 10);
      RollAiAimOffset();
      continue;
    }

    // AI paddle (right)
    const int32_t ay1 = aiYQ4 >> 4;
    const int32_t ay2 = ay1 + PaddleH - 1;
    if (ballVxQ4 > 0 && bx2 >= AiPaddleX && bx1 <= AiPaddleX + PaddleW - 1 && by2 >= ay1 && by1 <= ay2) {
      bounceOffPaddle(aiYQ4, (AiPaddleX - BallSize) * 16, -1, 8);
      continue;
    }

    // Scoring once ball exits left or right edge
    if (ballXQ4 < -BallSize * 16) {
      if (aiScore < 99) {
        aiScore++;
      }
      scoreDirty = true;
      motorController.RunForDuration(40);
      ResetServe(false);
      return;
    }
    if (ballXQ4 > ScreenW * 16) {
      if (playerScore < 99) {
        playerScore++;
      }
      scoreDirty = true;
      motorController.RunForDuration(25);
      ResetServe(true);
      return;
    }
  }
}

void Paddle::DrawPlayfield(lv_obj_t* obj, const lv_area_t* clipArea) {
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

  // 1. Clear invalidated area to black
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

  auto fillWhiteRect = [dispBuf, dispArea, dispW, &drawArea](int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    const int32_t ix1 = std::max<int32_t>(x1, drawArea.x1);
    const int32_t ix2 = std::min<int32_t>(x2, drawArea.x2);
    const int32_t iy1 = std::max<int32_t>(y1, drawArea.y1);
    const int32_t iy2 = std::min<int32_t>(y2, drawArea.y2);
    if (ix1 > ix2 || iy1 > iy2) {
      return;
    }
    for (int32_t y = iy1; y <= iy2; ++y) {
      lv_color_t* row = dispBuf + (y - dispArea->y1) * dispW + (ix1 - dispArea->x1);
      for (int32_t x = ix1; x <= ix2; ++x) {
        *row++ = LV_COLOR_WHITE;
      }
    }
  };

  // 2. Top and bottom 1px court borders
  fillWhiteRect(0, 0, ScreenW - 1, 0);
  fillWhiteRect(0, ScreenH - 1, ScreenW - 1, ScreenH - 1);

  // 3. Dashed center net (2px wide at x=119..120, 6px on / 6px off)
  if (drawArea.x1 <= 120 && drawArea.x2 >= 119) {
    const int32_t ny1 = std::max<int32_t>(drawArea.y1, 4);
    const int32_t ny2 = std::min<int32_t>(drawArea.y2, ScreenH - 5);
    const int32_t nx1 = std::max<int32_t>(drawArea.x1, 119);
    const int32_t nx2 = std::min<int32_t>(drawArea.x2, 120);
    for (int32_t y = ny1; y <= ny2; ++y) {
      if (((y - 4) % 12) < 6) {
        lv_color_t* row = dispBuf + (y - dispArea->y1) * dispW + (nx1 - dispArea->x1);
        for (int32_t x = nx1; x <= nx2; ++x) {
          *row++ = LV_COLOR_WHITE;
        }
      }
    }
  }

  // 4. Player and AI paddles
  const int32_t py1 = playerYQ4 >> 4;
  fillWhiteRect(PlayerPaddleX, py1, PlayerPaddleX + PaddleW - 1, py1 + PaddleH - 1);

  const int32_t ay1 = aiYQ4 >> 4;
  fillWhiteRect(AiPaddleX, ay1, AiPaddleX + PaddleW - 1, ay1 + PaddleH - 1);

  // 5. Ball
  const int32_t bx1 = ballXQ4 >> 4;
  const int32_t by1 = ballYQ4 >> 4;
  fillWhiteRect(bx1, by1, bx1 + BallSize - 1, by1 + BallSize - 1);
}

void Paddle::Refresh() {
  if (!paused) {
    UpdatePlayerFromAccel();
    UpdateAiPaddle();

    auto syncPaddleRect = [this](int32_t paddleX, int32_t yQ4, int16_t& prevY) {
      const int16_t curY = static_cast<int16_t>(yQ4 >> 4);
      if (curY != prevY) {
        InvalidateRect(paddleX, prevY, paddleX + PaddleW - 1, prevY + PaddleH - 1);
        InvalidateRect(paddleX, curY, paddleX + PaddleW - 1, curY + PaddleH - 1);
        prevY = curY;
      }
    };
    syncPaddleRect(PlayerPaddleX, playerYQ4, prevPlayerY);
    syncPaddleRect(AiPaddleX, aiYQ4, prevAiY);

    if (serveDelayTicks > 0) {
      serveDelayTicks--;
    } else {
      StepBall();
      const int16_t curBallX = static_cast<int16_t>(ballXQ4 >> 4);
      const int16_t curBallY = static_cast<int16_t>(ballYQ4 >> 4);
      if (curBallX != prevBallX || curBallY != prevBallY) {
        InvalidateRect(prevBallX, prevBallY, prevBallX + BallSize - 1, prevBallY + BallSize - 1);
        InvalidateRect(curBallX, curBallY, curBallX + BallSize - 1, curBallY + BallSize - 1);
        prevBallX = curBallX;
        prevBallY = curBallY;
      }
    }
  }

  if (scoreDirty) {
    UpdateScores();
  }
}

bool Paddle::OnTouchEvent(TouchEvents event) {
  switch (event) {
    case TouchEvents::SwipeDown:
      return false;
    case TouchEvents::Tap:
      paused = !paused;
      if (!paused) {
        CalibrateNeutralY();
      }
      UpdatePauseOverlay();
      return true;
    case TouchEvents::LongTap:
      ResetMatch();
      return true;
    default:
      return true;
  }
}
