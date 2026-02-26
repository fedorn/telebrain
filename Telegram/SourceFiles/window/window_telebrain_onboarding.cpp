/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_telebrain_onboarding.h"

#include "window/window_controller.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

namespace Window {
namespace {

void SetTelebrainOnboardingShown() {
	auto &settings = Core::App().settings();
	if (!settings.telebrainOnboardingShown()) {
		settings.setTelebrainOnboardingShown(true);
		Core::App().saveSettingsDelayed();
	}
}

void TelebrainOnboardingBox(
		not_null<Ui::GenericBox*> box,
		not_null<Controller*> controller) {
	box->setTitle(rpl::single(u"Welcome to Telebrain"_q));
	box->setCloseByEscape(true);
	box->setCloseByOutsideClick(true);

	const auto layout = box->verticalLayout();
	Ui::AddSkip(layout);
	layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		u"Telebrain is Telegram with an AI assistant built in."_q,
		st::boxLabel));
	Ui::AddSkip(layout);
	layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		u"Next steps:"_q,
		st::boxLabel));
	Ui::AddSkip(layout);
	layout->add(object_ptr<Ui::FlatLabel>(
		layout,
		u"1) Open the AI chat from Settings.\n"
		u"2) Add your API key.\n"
		u"3) Start a conversation."_q,
		st::boxLabel));
	Ui::AddSkip(layout);

	box->addButton(tr::lng_continue(), [=] {
		box->closeBox();
	});

	box->boxClosing(
	) | rpl::take(1) | rpl::on_next([=] {
		SetTelebrainOnboardingShown();
	}, box->lifetime());

	// Avoid unused warnings in some configs.
	(void)controller;
}

} // namespace

void ShowTelebrainOnboardingAfterTelegramOnboarding(
		not_null<Controller*> controller) {
	if (Core::App().settings().telebrainOnboardingShown()) {
		return;
	}
	controller->show(Box<Ui::GenericBox>(TelebrainOnboardingBox, controller));
}

} // namespace Window

