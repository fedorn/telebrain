/*
This file is part of Telebrain, a fork of Telegram Desktop with AI Copilot.

For license and copyright information please follow this link:
https://github.com/fedorn/telebrain/blob/dev/LEGAL
*/
#include "settings/sections/settings_ai.h"

#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "styles/style_settings.h"

namespace Settings {

AI::AI(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _controller(controller) {
	setupContent(controller);
}

rpl::producer<QString> AI::title() {
	return tr::lng_settings_ai();
}

void AI::setupContent(not_null<Window::SessionController*> controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_ai_configuration());

	// Base URL setting
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_ai_base_url());

	const auto baseUrlField = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			tr::lng_settings_ai_base_url_placeholder(),
			Core::App().settings().aiChatBaseUrl()),
		st::settingsSendTypePadding);

	baseUrlField->changes(
	) | rpl::on_next([=] {
		const auto value = baseUrlField->getLastText();
		Core::App().settings().setAiChatBaseUrl(value);
		Core::App().saveSettingsDelayed();
	}, baseUrlField->lifetime());

	// API Key setting
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_ai_api_key());

	const auto apiKeyField = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			tr::lng_settings_ai_api_key_placeholder(),
			Core::App().settings().aiChatApiKey()),
		st::settingsSendTypePadding);

	apiKeyField->changes(
	) | rpl::on_next([=] {
		const auto value = apiKeyField->getLastText();
		Core::App().settings().setAiChatApiKey(value);
		Core::App().saveSettingsDelayed();
	}, apiKeyField->lifetime());

	// Model setting
	Ui::AddSkip(content);
	Ui::AddSubsectionTitle(content, tr::lng_settings_ai_model());

	const auto modelField = content->add(
		object_ptr<Ui::InputField>(
			content,
			st::defaultInputField,
			tr::lng_settings_ai_model_placeholder(),
			Core::App().settings().aiChatModel()),
		st::settingsSendTypePadding);

	modelField->changes(
	) | rpl::on_next([=] {
		const auto value = modelField->getLastText();
		Core::App().settings().setAiChatModel(value);
		Core::App().saveSettingsDelayed();
	}, modelField->lifetime());

	// Help text
	Ui::AddSkip(content);
	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_settings_ai_help());
	Ui::AddSkip(content);

	Ui::ResizeFitChild(this, content);
}

} // namespace Settings

template <>
struct Settings::SectionFactory<Settings::AI> : Settings::AbstractSectionFactory {
	object_ptr<Settings::AbstractSection> create(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<Settings::Container> containerValue
	) const final override {
		return object_ptr<Settings::AI>(parent, controller);
	}

	[[nodiscard]] static const std::shared_ptr<SectionFactory> &Instance() {
		static const auto result = std::make_shared<SectionFactory>();
		return result;
	}
}; 