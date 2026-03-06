/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#include "boxes/telebrain_setup_box.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"

void TelebrainSetupBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_telebrain_setup_title());
	box->setWidth(st::boxWideWidth);

	const auto padding = st::boxPadding;

	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_telebrain_setup_description(),
			st::boxLabel),
		QMargins(padding.left(), 0, padding.right(), padding.bottom()));

	const auto link = box->addRow(
		object_ptr<Ui::LinkButton>(
			box,
			tr::lng_telebrain_setup_get_key(tr::now)),
		QMargins(padding.left(), 0, padding.right(), padding.bottom()));
	link->setClickedCallback([] {
		File::OpenUrl(u"https://openrouter.ai/settings/keys"_q);
	});

	const auto apiKeyField = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_telebrain_setup_api_key_placeholder()),
		QMargins(padding.left(), 0, padding.right(), 0));

	const auto weak = base::make_weak(box);
	box->addButton(tr::lng_telebrain_setup_save(), [=] {
		const auto key = apiKeyField->getLastText().trimmed();
		if (!key.isEmpty()) {
			Core::App().settings().setAiChatApiKey(key);
			Core::App().saveSettingsDelayed();
		}
		if (weak) {
			weak->closeBox();
		}
	});
	box->addButton(tr::lng_telebrain_setup_skip(), [=] {
		if (weak) {
			weak->closeBox();
		}
	});
}
