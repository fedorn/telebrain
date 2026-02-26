/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_telebrain_intro.h"

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

void SetTelebrainIntroShown() {
	auto &settings = Core::App().settings();
	if (!settings.telebrainIntroShown()) {
		settings.setTelebrainIntroShown(true);
		Core::App().saveSettingsDelayed();
	}
}

void TelebrainIntroBox(
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
		SetTelebrainIntroShown();
	}, box->lifetime());

	(void)controller;
}

} // namespace

void ShowTelebrainIntroAfterTelegramIntro(
		not_null<Controller*> controller) {
	if (Core::App().settings().telebrainIntroShown()) {
		return;
	}
	controller->show(Box<Ui::GenericBox>(TelebrainIntroBox, controller));
}

} // namespace Window

