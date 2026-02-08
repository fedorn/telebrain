/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#pragma once

#include "settings/settings_common_session.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Settings {

class AI : public Section<AI> {
public:
	AI(QWidget *parent, not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent(not_null<Window::SessionController*> controller);

	const not_null<Window::SessionController*> _controller;

};

} // namespace Settings 