/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/required.h"

namespace Window {

class Controller;

void ShowTelebrainOnboardingAfterTelegramOnboarding(
	not_null<Controller*> controller);

} // namespace Window

